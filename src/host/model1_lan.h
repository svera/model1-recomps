#pragma once
#include "lan_session.h"
#include "../machine/link_packet.h"
#include "../machine/model1.h"

// Two independently paced cabinets. Room ownership never reassigns a running cabinet's ID.
class model1_lan
{
public:
	bool active() const { return m_player != 0; }
	int player() const { return m_player; }
	bool running() const { return m_running; }
	bool host() const { return m_host; }
	bool paused() const { return active() && m_running && m_pause_step != 0; }
	int countdown() const { return paused() && m_pause_step >= 2 ? 5 - m_pause_step : 0; }
	void pause(bool value, uint64_t now)
	{
		if (!active() || !m_host) return;
		if (value) {
			m_resume_requested = false;
			if (m_pause_step != 1) set_pause_step(1, now);
		} else if (m_pause_step) m_resume_requested = true;
	}

	bool begin(lan_session &session, uint32_t generation)
	{
		const auto members = session.members(true);
		if (members.size() != 2 || members[0].gone || members[1].gone)
			return session.fail("A linked race needs two connected racers.");
		m_player = members[0].own ? 1 : members[1].own ? 2 : 0;
		if (!m_player) return session.fail("This device is not in the racing roster.");
		m_ids = {members[0].id, members[1].id};
		m_generation = generation;
		m_sequence = 0;
		m_peer.reset(generation, uint8_t(3 - m_player));
		m_running = false;
		m_received = false;
		m_host = session.hosting();
		m_pause_step = 0;
		m_pause_epoch = 0;
		m_resume_requested = false;
		m_next_publish = 0;
		return true;
	}
	void attach(model1 &machine)
	{
		machine.enable_link([this](const auto &, auto &incoming) {
			incoming = m_peer.memory();
			return m_peer.available();
		}, true);
	}
	bool update(lan_session &session, uint64_t now, model1 &machine)
	{
		if (!active()) return true;
		auto *board = machine.link();
		if (board && board->ticks()) m_running = true;
		const auto members = session.members(true);
		if (members.size() != 2 || members[0].id != m_ids[0] || members[1].id != m_ids[1] ||
			members[0].gone || members[1].gone || session.interrupted())
			return session.fail("A racer disconnected. The linked race has ended.");
		if (!m_received) { m_last_packet = now; m_received = true; }
		const auto bytes = session.block(2 - m_player);
		if (!bytes.empty() && m_peer.take(bytes)) {
			m_last_packet = now;
			if (!m_host) {
				m_pause_step = m_peer.pause_step();
				m_pause_epoch = m_peer.pause_epoch();
			}
		}
		if (now > m_last_packet + 3000)
			return session.fail("No cabinet data for three seconds. The linked race has ended.");
		// Each countdown step is repeated until acknowledged. Networking continues
		// independently of simulation, including throughout an arbitrarily long pause.
		if (m_host && m_running && m_pause_step && m_peer.available() &&
			m_peer.pause_epoch() == m_pause_epoch && m_peer.pause_step() == m_pause_step) {
			if (m_pause_step == 1 && m_resume_requested) set_pause_step(2, now);
			else if (m_pause_step >= 2 && now - m_step_started >= 1000)
				set_pause_step(m_pause_step == 4 ? 0 : m_pause_step + 1, now);
		}
		if (now >= m_next_publish && board && board->ready() && board->snapshot()[1] == m_player) {
			session.publish(model1_link::encode(board->snapshot(), m_generation, m_sequence++,
				m_pause_step, m_pause_epoch));
			m_next_publish = now + 16;
		}
		return true;
	}
	void run_frame(model1 &machine)
	{
		for (int line = 0; line < model1::FRAME_LINES; line++) {
			auto *board = machine.link();
			if (line == model1::FRAME_VBLANK && board && board->ready() &&
				board->snapshot()[1] == m_player) {
				if (m_peer.available()) { board->tick(); m_running = true; }
			}
			machine.run_scanline();
		}
	}
	void stop(model1 &machine)
	{
		machine.disable_link();
		m_player = 0;
		m_running = false;
	}
private:
	void set_pause_step(uint8_t step, uint64_t now)
	{
		m_pause_step = step;
		++m_pause_epoch;
		m_step_started = now;
	}
	int m_player = 0;
	uint32_t m_generation = 0, m_sequence = 0;
	std::array<uint32_t, 2> m_ids{};
	model1_link::receiver m_peer;
	uint64_t m_last_packet = 0;
	bool m_running = false, m_received = false;
	bool m_host = false, m_resume_requested = false;
	uint8_t m_pause_step = 0;
	uint32_t m_pause_epoch = 0;
	uint64_t m_step_started = 0, m_next_publish = 0;
};
