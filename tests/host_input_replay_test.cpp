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
	{
		const auto path = std::filesystem::temp_directory_path() / "m1_input_replay_cheats_test.inputs";
		data.frames.resize(5);
		data.frames[0].cheats = 0;
		data.frames[1].cheats = 1;
		data.frames[2].cheats = 3;
		data.frames[3].cheats = 2;
		data.frames[4].cheats = 0;
		for (bool eeprom : {false, true}) {
			data.eeprom.assign(eeprom ? model1::EEPROM_SIZE : 0, 0xa5);
			assert(input_replay::save(path.string(), game::id{"wingwar"}, data).empty());
			input_replay::recording again;
			assert(input_replay::load(path.string(), game::id{"wingwar"}, again).empty());
			assert(again.eeprom == data.eeprom && again.expected_ram == data.expected_ram);
			assert(again.frames.size() == 5 && again.frames[0].cheats == 0 &&
				again.frames[1].cheats == 1 && again.frames[2].cheats == 3 &&
				again.frames[3].cheats == 2 && again.frames[4].cheats == 0);
		}
		std::filesystem::remove(path);
		assert(!read("M1INPUT 3 swa 1 abc -\n" + row + "-1").empty());
		assert(!read("M1INPUT 3 swa 1 abc -\n" + row + "4294967296").empty());
		assert(!read("M1INPUT 3 swa 1 abc -\n" + row).empty());
		assert(read("M1INPUT 3 swa 1 abc -\n" + row + "4294967295").empty());
		assert(data.frames[0].cheats == UINT32_MAX);
		assert(!read("M1INPUT 4 swa 1 abc -\n" + row + "1").empty());
		assert(!read("M1INPUT 3 swa 1 abc invalid\n" + row + "1").empty());
	}
	std::cout << "Input replay: versions 1/2/3, strict validation, EEPROM and per-frame cheat transitions passed\n";
}
