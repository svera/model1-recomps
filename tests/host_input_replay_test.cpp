#include "../src/host/input_replay.h"
#include <cassert>
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
	assert(data.frames.size() == 1 && data.expected_ram == 0xabc);
	std::cout << "Input replay: strict header, frame counts, buttons, axes and trailing-data checks passed\n";
}
