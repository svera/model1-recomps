#pragma once

#include "input_replay.h"
#include <new>
#include <optional>
#include <string>
#include <vector>

namespace race_replay {

struct replay_sound_event
{
	int scanline = 0;
	uint8_t value = 0;
};

struct replay_frame
{
	uint64_t number = 0;
	game::script_inputs input;
	bool rear_view = false;
	std::unique_ptr<m1comm::state> link;
	std::vector<replay_sound_event> sound;
	uint64_t ram_hash = 0;
};

class race_recording
{
public:
	// Recording payload only; checkpoints and allocator overhead are separate.
	static constexpr size_t MAX_BYTES = 128 * 1024 * 1024;
	std::vector<replay_frame> frames;
};

class race_recorder
{
public:
	explicit race_recorder(model1 &machine, size_t max_bytes = race_recording::MAX_BYTES)
		: m_machine(machine), m_max_bytes(std::min(max_bytes, race_recording::MAX_BYTES)) {}
	race_recorder(const race_recorder &) = delete;
	race_recorder &operator=(const race_recorder &) = delete;
	~race_recorder() { stop(); }

	std::string start()
	{
		if (m_active || m_machine.replay_link_observer || m_machine.replay_link_override ||
			m_machine.replay_sound_observer || m_machine.replay_sound_reply)
			return "replay recording hooks are already in use";
		m_data.reset();
		m_bytes = 0;
		m_error.clear();
		try {
			m_data = std::make_shared<race_recording>();
			m_active = true;
			m_machine.replay_link_observer = [this](uint64_t number, const m1comm &link) {
				if (!current(number)) return;
				if (m_data->frames.back().link) {
					m_error = "duplicate replay link event";
					return;
				}
				if (!can_allocate(sizeof(m1comm::state))) return;
				try {
					m_data->frames.back().link = std::make_unique<m1comm::state>(link.save_state());
					m_bytes += sizeof(m1comm::state);
				} catch (const std::bad_alloc &) {
					m_error = "replay recording allocation failed";
				}
			};
			m_machine.replay_sound_observer = [this](uint64_t number, int line, uint8_t value) {
				if (!current(number) || !grow(m_data->frames.back().sound)) return;
				m_data->frames.back().sound.push_back({line, value});
			};
		} catch (const std::bad_alloc &) {
			stop();
			return "replay recording allocation failed";
		}
		return {};
	}

	std::string begin_frame()
	{
		if (!m_active || m_in_frame || m_machine.scanline()) return "invalid replay frame boundary";
		if (!grow(m_data->frames)) return m_error;
		m_data->frames.emplace_back();
		auto &frame = m_data->frames.back();
		frame.number = m_machine.frame_number();
		frame.input = input_replay::capture(m_machine);
		frame.rear_view = m_machine.rear_view();
		m_in_frame = true;
		return {};
	}

	std::string end_frame()
	{
		if (!m_active || !m_in_frame || m_machine.scanline() ||
			m_machine.frame_number() != m_data->frames.back().number + 1)
			return "invalid replay frame completion";
		m_in_frame = false;
		if (m_error.empty() && bool(m_machine.link()) != bool(m_data->frames.back().link))
			m_error = "replay link event was not captured";
		if (m_error.empty()) m_data->frames.back().ram_hash = input_replay::ram_hash(m_machine);
		return m_error;
	}

	void stop()
	{
		if (!m_active) return;
		m_machine.replay_link_observer = {};
		m_machine.replay_sound_observer = {};
		m_active = false;
		m_in_frame = false;
	}

	const race_recording &data() const { return *m_data; }
	std::shared_ptr<const race_recording> share() const { return m_data; }
	const std::string &error() const { return m_error; }

private:
	bool current(uint64_t number)
	{
		if (m_in_frame && m_error.empty() && !m_data->frames.empty() &&
			m_data->frames.back().number == number) return true;
		if (m_error.empty()) m_error = "replay event outside its frame";
		return false;
	}
	bool can_allocate(size_t bytes)
	{
		if (!m_error.empty()) return false;
		if (m_bytes > m_max_bytes || bytes > m_max_bytes - m_bytes) {
			m_error = "replay recording memory limit reached";
			return false;
		}
		return true;
	}
	template <typename T> bool grow(std::vector<T> &values)
	{
		if (!m_error.empty()) return false;
		if (values.size() < values.capacity()) return true;
		const size_t previous = values.capacity();
		const size_t capacity = previous ? previous * 2 : 1;
		if (capacity < previous || capacity > m_max_bytes / sizeof(T) ||
			!can_allocate(capacity * sizeof(T))) {
			if (m_error.empty()) m_error = "replay recording memory limit reached";
			return false;
		}
		try {
			values.reserve(capacity);
		} catch (const std::bad_alloc &) {
			m_error = "replay recording allocation failed";
			return false;
		}
		const size_t allocated = (values.capacity() - previous) * sizeof(T);
		if (allocated > m_max_bytes - m_bytes) {
			m_error = "replay recording memory limit reached";
			return false;
		}
		m_bytes += allocated;
		return true;
	}
	model1 &m_machine;
	std::shared_ptr<race_recording> m_data;
	const size_t m_max_bytes;
	size_t m_bytes = 0;
	bool m_active = false, m_in_frame = false;
	std::string m_error;
};

inline std::string replay_race_frame(model1 &machine, const replay_frame &frame)
{
	if (machine.scanline() || machine.frame_number() != frame.number)
		return "replay frame does not match the checkpoint";
	if (machine.replay_link_observer || machine.replay_link_override ||
		machine.replay_sound_observer || machine.replay_sound_reply)
		return "replay playback hooks are already in use";
	if (machine.audio()) return "replay playback requires an isolated silent sound board";
	if (bool(machine.link()) != bool(frame.link)) return "replay link configuration differs";
	if (const auto error = input_replay::apply(machine, frame.input); !error.empty()) return error;
	machine.set_rear_view(frame.rear_view);
	size_t sound_index = 0;
	bool link_seen = false;
	struct clear_hooks {
		model1 &machine;
		~clear_hooks() { machine.replay_link_override = {}; machine.replay_sound_reply = {}; }
	} cleanup{machine};
	try {
		machine.replay_link_override = [&](uint64_t, m1comm &link) {
			link_seen = true;
			link.load_state(*frame.link);
		};
		machine.replay_sound_reply = [&](uint64_t, int line) -> std::optional<uint8_t> {
			if (sound_index < frame.sound.size() && frame.sound[sound_index].scanline == line)
				return frame.sound[sound_index++].value;
			return std::nullopt;
		};
	} catch (const std::bad_alloc &) {
		return "replay playback allocation failed";
	}
	machine.run_frame();
	if (link_seen != bool(frame.link) || sound_index != frame.sound.size())
		return "replay external events diverged";
	if (input_replay::ram_hash(machine) != frame.ram_hash) return "replay simulation diverged";
	return {};
}

class race_player
{
public:
	race_player(model1 &machine, std::shared_ptr<const race_recording> recording, std::function<std::string()> restore)
		: m_machine(machine), m_recording(std::move(recording)), m_restore(std::move(restore)) {}

	std::string start(size_t target)
	{
		if (!m_recording) return "replay recording is unavailable";
		if (target > m_recording->frames.size()) return "replay seek is beyond the recording";
		m_started = false;
		if (const auto error = m_restore(); !error.empty()) return error;
		m_position = 0;
		m_target = target;
		m_started = true;
		return {};
	}

	std::string seek(size_t target)
	{
		if (!m_started) return "replay has not started";
		if (target > m_recording->frames.size()) return "replay seek is beyond the recording";
		if (target < m_position) {
			if (const auto error = m_restore(); !error.empty()) {
				m_started = false;
				return error;
			}
			m_position = 0;
		}
		m_target = target;
		return {};
	}

	std::string advance(size_t budget)
	{
		if (!m_started) return "replay has not started";
		while (m_position < m_target && budget--) {
			if (const auto error = replay_race_frame(m_machine, m_recording->frames[m_position]); !error.empty()) {
				m_started = false;
				return error;
			}
			++m_position;
		}
		return {};
	}

	std::string step()
	{
		if (!m_started) return "replay has not started";
		if (seeking()) return "replay seek is still in progress";
		if (m_position >= m_recording->frames.size()) return "replay has ended";
		if (const auto error = replay_race_frame(m_machine, m_recording->frames[m_position]); !error.empty()) {
			m_started = false;
			return error;
		}
		m_target = ++m_position;
		return {};
	}

	bool seeking() const { return m_position != m_target; }
	size_t position() const { return m_position; }
	size_t length() const { return m_recording ? m_recording->frames.size() : 0; }

private:
	model1 &m_machine;
	std::shared_ptr<const race_recording> m_recording;
	std::function<std::string()> m_restore;
	size_t m_position = 0, m_target = 0;
	bool m_started = false;
};

}
