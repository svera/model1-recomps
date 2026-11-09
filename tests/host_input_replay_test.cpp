#include "../src/host/input_replay.h"
#include <cassert>
#include <filesystem>
#include <iostream>

int main()
{
	const game::id swa{ "swa" };
	const std::string row = "16 3 0 127 80 28 -1 120 130 -1 -1\n";
	input_replay::recording data;
	auto read = [&](const std::string &text) {
		std::istringstream in(text);
		return input_replay::read(in, swa, data);
	};
	assert(read("M1INPUT 1 swa 1 abc\n" + row).empty());
	assert(data.frames.size() == 1 && data.expected_ram == 0xabc);
	{
		input_replay::recording wingwar;
		std::istringstream in("M1INPUT 1 wingwar 1 abc\n0 0 0 128 128 1 -1 -1 -1 -1 -1\n");
		assert(input_replay::read(in, game::id{"wingwar"}, wingwar).empty());
		assert(wingwar.frames[0].analog[0] == 128 && wingwar.frames[0].analog[2] == 1);
		std::istringstream wrong("M1INPUT 1 swa 1 abc\n" + row);
		assert(!input_replay::read(wrong, game::id{"wingwar"}, wingwar).empty());
	}
	assert(data.frames[0].pressed[0] == 16 && data.frames[0].analog[2] == 28);
	assert(!read("M1INPUT 1 vf 1 abc\n" + row).empty());
	assert(!read("M1INPUT 2 swa 1 abc\n" + row).empty());
	assert(!read("M1INPUT 1 swa 0 abc\n").empty());
	assert(!read("M1INPUT 1 swa 1000001 abc\n").empty());
	assert(!read("M1INPUT 1 swa -1 abc\n").empty());
	assert(!read("M1INPUT 1 swa 2 abc\n" + row).empty());
	assert(!read("M1INPUT 1 swa 1 abc\n256 0 0 0 0 0 0 0 0 0 0").empty());
	assert(!read("M1INPUT 1 swa 1 abc\n0 0 0 -2 0 0 0 0 0 0 0").empty());
	assert(!read("M1INPUT 1 swa 1 abc\n0 0 0 256 0 0 0 0 0 0 0").empty());
	assert(!read("M1INPUT 1 swa 1 abc\n" + row + "junk").empty());
	assert(data.frames.size() == 1 && data.expected_ram == 0xabc && data.eeprom.empty());
	{
		std::string hex;
		for (size_t i = 0; i < model1::EEPROM_SIZE; i++) hex += i == 1 ? "a5" : "00";
		assert(read("M1INPUT 2 swa 1 abc " + hex + "\n" + row).empty());
		assert(data.eeprom.size() == model1::EEPROM_SIZE && data.eeprom[1] == 0xa5 && data.frames.size() == 1);
		assert(!read("M1INPUT 2 swa 1 abc " + hex.substr(2) + "\n" + row).empty());
		assert(!read("M1INPUT 2 swa 1 abc " + hex.substr(2) + "g\n" + row).empty());
		const auto path = std::filesystem::temp_directory_path() / "m1_input_replay_test.inputs";
		assert(input_replay::save(path.string(), swa, data).empty());
		input_replay::recording again;
		assert(input_replay::load(path.string(), swa, again).empty());
		std::filesystem::remove(path);
		assert(again.eeprom == data.eeprom && again.expected_ram == 0xabc && again.frames.size() == 1);
	}
	std::cout << "Input replay: strict header, frame counts, buttons, axes and trailing-data checks and the starting EEPROM passed\n";
}
