#pragma once
#include "../games/script.h"
#include "../machine/model1.h"
#include <fstream>
#include <sstream>

namespace input_replay {

constexpr size_t MAX_FRAMES = 1000000;

struct recording
{
	std::vector<game::script_inputs> frames;
	uint64_t expected_ram = 0;
};

inline uint64_t ram_hash(model1 &m)
{
	uint64_t h = 1469598103934665603ull;
	for (auto *ram : { &m.nvram(), &m.work_ram() })
		for (uint8_t b : *ram) h = (h ^ b) * 1099511628211ull;
	return h;
}

inline std::string read(std::istream &in, game::id title, recording &out)
{
	std::string magic, name;
	int version = 0;
	size_t count = 0;
	recording candidate;
	if (!(in >> magic >> version >> name >> count >> std::hex >> candidate.expected_ram >> std::dec) ||
		magic != "M1INPUT" || version != 1 || name != title.name || count == 0 || count > MAX_FRAMES)
		return "invalid input replay header, game or frame count";
	candidate.frames.reserve(count);
	for (size_t f = 0; f < count; f++) {
		game::script_inputs frame;
		for (auto &bits : frame.pressed) {
			int value;
			if (!(in >> value) || value < 0 || value > 255)
				return "invalid replay buttons at frame " + std::to_string(f + 1);
			bits = uint8_t(value);
		}
		for (auto &value : frame.analog)
			if (!(in >> value) || value < -1 || value > 255)
				return "invalid replay analog input at frame " + std::to_string(f + 1);
		candidate.frames.push_back(frame);
	}
	in >> std::ws;
	if (in.bad() || !in.eof()) return "unexpected data after input replay";
	out = std::move(candidate);
	return {};
}

inline std::string load(const std::string &path, game::id title, recording &out)
{
	std::ifstream in(path);
	if (!in) return "cannot open input replay: " + path;
	return read(in, title, out);
}

inline std::string save(const std::string &path, game::id title, const recording &data)
{
	std::ofstream out(path);
	if (!out) return "cannot create input replay: " + path;
	out << "M1INPUT 1 " << title.name << ' ' << data.frames.size() << ' '
		<< std::hex << data.expected_ram << std::dec << '\n';
	for (const auto &frame : data.frames) {
		for (auto bits : frame.pressed) out << unsigned(bits) << ' ';
		for (auto value : frame.analog) out << value << ' ';
		out << '\n';
	}
	out.close();
	if (!out) return "cannot write input replay: " + path;
	return {};
}

// The cabinet inputs the next frame runs with, in the form apply() replays.
inline game::script_inputs capture(const model1 &m)
{
	game::script_inputs frame;
	for (size_t i = 0; i < frame.pressed.size(); i++) frame.pressed[i] = uint8_t(~m.m_inputs[i]);
	for (size_t i = 0; i < frame.analog.size(); i++)
		frame.analog[i] = m.has_analog_input(int(i)) ? int(m.m_analog_inputs[i]) : -1;
	return frame;
}

inline std::string apply(model1 &m, const game::script_inputs &frame)
{
	for (size_t i = 0; i < frame.analog.size(); i++)
		if (frame.analog[i] >= 0 && !m.set_analog_input(int(i), uint8_t(frame.analog[i])))
			return "replay uses unavailable analog channel " + std::to_string(i);
	for (size_t i = 0; i < frame.pressed.size(); i++) m.m_inputs[i] = uint8_t(~frame.pressed[i]);
	return {};
}

}
