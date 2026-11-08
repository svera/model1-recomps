#include "../../../src/games/wingwar/profile.h"
#include "../../../src/games/script.h"
#include "../../../src/host/regions.h"
#include "../../../src/host/input_replay.h"
#include <filesystem>
#include <cassert>
#include <cstdio>
#include <set>

int main(int argc, char **argv)
{
	if (argc != 2 && argc != 3) {
		fprintf(stderr, "usage: %s regions-dir [new-boot-recording.inputs] (requires wingwar.zip)\n", argv[0]);
		return 2;
	}
	if (argc == 3 && std::filesystem::exists(argv[2])) {
		fprintf(stderr, "Refusing to overwrite recording %s\n", argv[2]);
		return 2;
	}
	using game::wingwar::ID;
	assert(!game::make_auto_play(ID, "unknown", 1, true));
	auto boot = game::make_auto_play(ID, "boot", 1, true);
	auto attract = game::make_auto_play(ID, "attract", 1, true);
	auto repeat = game::make_auto_play(ID, "", 98765, false);
	assert(boot && attract && repeat);
	assert(boot->min_frames() == 700 && attract->min_frames() == 7200);
	assert(!boot->finish().empty() && !attract->finish().empty());
	assert(repeat->min_frames() == 0 && repeat->finish().empty());
	model1 m(load_regions(argv[1], ID));
	m.reset();
	input_replay::recording recording;
	std::set<uint32_t> rom, ram;
	m.on_instruction = [&](uint32_t pc) {
		if (pc >= 0x400000 && pc < 0x410000) ram.insert(pc);
		else rom.insert(pc);
	};
	for (int f = 1; f <= attract->min_frames(); f++) {
		const auto in = attract->frame(f);
		const auto other = repeat->frame(f);
		const auto start = boot->frame(f);
		assert(in.pressed == other.pressed && in.analog == other.analog);
		assert(in.pressed == start.pressed && in.analog == start.analog);
		assert((in.pressed == std::array<uint8_t, 3>{}));
		assert(in.analog[0] == 0x80 && in.analog[1] == 0x80 && in.analog[2] == 1);
		for (size_t c = 0; c < in.analog.size(); c++)
			if (in.analog[c] >= 0) assert(m.set_analog_input(c, uint8_t(in.analog[c])));
		for (int p = 0; p < 3; p++) m.m_inputs[p] = uint8_t(~in.pressed[p]);
		if (argc == 3 && f <= 700) recording.frames.push_back(input_replay::capture(m));
		m.run_frame();
		if (argc == 3 && f == 700) recording.expected_ram = input_replay::ram_hash(m);
		if (f <= boot->min_frames()) {
			const auto error = boot->check(f, m);
			if (!error.empty()) { fprintf(stderr, "%s\n", error.c_str()); return 1; }
		}
		const auto error = attract->check(f, m);
		if (!error.empty()) { fprintf(stderr, "%s\n", error.c_str()); return 1; }
	}
	for (auto *script : { boot.get(), attract.get() }) {
		const auto error = script->finish();
		if (!error.empty()) { fprintf(stderr, "%s\n", error.c_str()); return 1; }
	}
	assert(!rom.empty());
	if (argc == 3) {
		const auto error = input_replay::save(argv[2], ID, recording);
		if (!error.empty()) { fprintf(stderr, "%s\n", error.c_str()); return 1; }
		printf("Verified Wing War boot recording saved to %s\n", argv[2]);
	}
	printf("Wing War boot/attract scripts passed: 7200 frames, %zu ROM/non-work-RAM PCs, %zu work-RAM PCs\n",
		rom.size(), ram.size());
}
