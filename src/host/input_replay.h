#pragma once
#include "../games/script.h"
#include "../machine/model1.h"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace input_replay {

constexpr size_t MAX_FRAMES = 1000000;

struct recording
{
	std::vector<game::script_inputs> frames;
	uint64_t expected_ram = 0;
	std::vector<uint8_t> eeprom;   // the cold boot's saves (versions 2/3); empty: blank
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
		magic != "M1INPUT" || (version < 1 || version > 3) || name != title.name || count == 0 || count > MAX_FRAMES)
		return "invalid input replay header, game or frame count";
	if (version >= 2) {
		std::string hex;
		if (!(in >> hex)) return "missing input replay EEPROM";
		if (version == 3 && hex == "-") hex.clear();
		else if (hex.size() != 2 * model1::EEPROM_SIZE ||
			hex.find_first_not_of("0123456789abcdef") != std::string::npos)
			return "invalid input replay EEPROM";
		for (size_t i = 0; i < hex.size(); i += 2) candidate.eeprom.push_back(uint8_t(std::stoi(hex.substr(i, 2), nullptr, 16)));
	}
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
		if (version == 3) {
			int64_t flags;
			if (!(in >> flags) || flags < 0 || flags > UINT32_MAX)
				return "invalid replay cheats at frame " + std::to_string(f + 1);
			frame.cheats = uint32_t(flags);
		}
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
	if (!data.eeprom.empty() && data.eeprom.size() != model1::EEPROM_SIZE) return "invalid input recording EEPROM";
	const bool cheats = std::any_of(data.frames.begin(), data.frames.end(), [](const auto &f) { return f.cheats != 0; });
	out << "M1INPUT " << (cheats ? 3 : data.eeprom.empty() ? 1 : 2) << ' ' << title.name << ' ' << data.frames.size() << ' '
		<< std::hex << data.expected_ram;
	if (!data.eeprom.empty()) {
		out << ' ' << std::setfill('0');
		for (uint8_t b : data.eeprom) out << std::setw(2) << unsigned(b);
	} else if (cheats) out << " -";
	out << std::dec << '\n';
	for (const auto &frame : data.frames) {
		for (auto bits : frame.pressed) out << unsigned(bits) << ' ';
		for (auto value : frame.analog) out << value << ' ';
		if (cheats) out << frame.cheats << ' ';
		out << '\n';
	}
	out.close();
	if (!out) return "cannot write input replay: " + path;
	return {};
}

// Before the cold boot (the machine's reset) of a new machine: the saves the recording started
// from, or none (version 1).
inline void start(model1 &m, const recording &data)
{
	if (!data.eeprom.empty()) memcpy(m.eeprom(), data.eeprom.data(), model1::EEPROM_SIZE);
}

// The cabinet inputs the next frame runs with, in the form apply() replays.
inline game::script_inputs capture(const model1 &m)
{
	game::script_inputs frame;
	frame.cheats = m.cheats();
	for (size_t i = 0; i < frame.pressed.size(); i++) frame.pressed[i] = uint8_t(~m.m_inputs[i]);
	for (size_t i = 0; i < frame.analog.size(); i++)
		frame.analog[i] = m.has_analog_input(int(i)) ? int(m.m_analog_inputs[i]) : -1;
	return frame;
}

inline std::string apply(model1 &m, const game::script_inputs &frame)
{
	if (!m.set_cheats(frame.cheats)) return "replay uses unsupported cheats";
	for (size_t i = 0; i < frame.analog.size(); i++)
		if (frame.analog[i] >= 0 && !m.set_analog_input(int(i), uint8_t(frame.analog[i])))
			return "replay uses unavailable analog channel " + std::to_string(i);
	for (size_t i = 0; i < frame.pressed.size(); i++) m.m_inputs[i] = uint8_t(~frame.pressed[i]);
	return {};
}

}
