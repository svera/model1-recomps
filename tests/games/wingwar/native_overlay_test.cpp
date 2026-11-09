#include "../../../src/games/wingwar/profile.h"
#include "../../../src/games/wingwar/gameplay.h"
#include "../../../src/games/script.h"
#include "../../../src/host/regions.h"
#include "../../../src/rt/native.h"
#include "../../../src/host/input_replay.h"
#include <cassert>
#include <cstring>
#include <cstdio>

struct model1_native_test
{
	static void lists(const model1 &a, const model1 &b)
	{
		assert(!memcmp(a.m_display_list0, b.m_display_list0, 0x10000));
		assert(!memcmp(a.m_display_list1, b.m_display_list1, 0x10000));
	}
};

namespace {

model1::regions synthetic_regions()
{
	model1::regions r;
	r.title = game::wingwar::ID;
	r.maincpu.resize(0x2000000);
	r.maincpu[0xfffff0] = 0xca;
	return r;
}

void guarded_versions()
{
	model1 m(synthetic_regions());
	m.reset();
	const uint32_t base = 0x40d000;
	const std::array<uint8_t, 8> original{0x2d, 0x20, 0xe1, 0x6a, 0xfd, 0, 0, 0};
	for (size_t i = 0; i < original.size(); i++) m.write8(base + i, original[i]);
	m.cpu().set_reg(32, base);
	rec::context runtime;
	runtime.attach(m.cpu(), game::wingwar::ID.name);
	rec::hints h;
	h.ram.push_back({base, base + 16});
	rec::native_stats st;
	assert(rec::build_native(m.maincpu_rom(), h, &st));
	assert(st.overlays);
	runtime.run(8);
	assert(m.cpu().reg(0) == 1 && m.cpu().pc() == base + 3);
	// Replace the next instruction while its old native caller is suspended at the slice boundary.
	const std::array<uint8_t, 5> replacement{0x2d, 0x21, 0xe2, 0x6a, 0xfa};
	for (size_t i = 0; i < replacement.size(); i++) m.write8(base + 3 + i, replacement[i]);
	runtime.run(8);
	assert(m.cpu().reg(1) == 2 && m.cpu().pc() == base + 6);
	assert(st.overlays->stale_instructions > 0 && st.overlays->compilations == 2);
	runtime.run(8);
	assert(m.cpu().pc() == base);
	for (size_t i = 0; i < original.size(); i++) m.write8(base + i, original[i]);
	runtime.run(8);
	assert(m.cpu().reg(0) == 1 && m.cpu().pc() == base + 3);
	runtime.attach(m.cpu(), game::wingwar::ID.name);
	m.cpu().set_reg(32, base);
	runtime.run(8);
	assert(m.cpu().pc() == base + 3 && !rec::fallback_count());
	assert(st.overlays->cache_hits > 0);
	puts("Native RAM guards: suspended instruction replacement, retained tails, cached versions and stack reset passed");
}

void context_isolation()
{
	model1 a(synthetic_regions()), b(synthetic_regions());
	a.reset();
	b.reset();
	rec::context first, second;
	rec::native_stats one, two;
	rec::hints h;
	h.ram.push_back({0x40d000, 0x40d010});
	for (auto *m : {&a, &b}) {
		m->write8(0x40d000, 0x2d);
		m->write8(0x40d001, 0x20);
		m->write8(0x40d002, m == &a ? 0xe1 : 0xe2);
		m->write8(0x40d003, 0x6a);
		m->write8(0x40d004, 0xfd);
		m->cpu().set_reg(32, 0x40d000);
	}
	first.attach(a.cpu(), game::wingwar::ID.name);
	assert(rec::build_native(a.maincpu_rom(), h, &one));
	second.attach(b.cpu(), game::wingwar::ID.name);
	assert(rec::build_native(b.maincpu_rom(), h, &two));
	for (int i = 0; i < 4; i++) {
		first.run(8);
		second.run(8);
		assert(a.cpu().reg(0) == 1 && b.cpu().reg(0) == 2);
	}
	a.write8(0x40d002, 0xe3);
	first.run(8);
	second.run(8);
	assert(a.cpu().reg(0) == 3 && b.cpu().reg(0) == 2);
	assert(one.overlays != two.overlays && one.overlays->compilations > two.overlays->compilations);
	rec::hints invalid;
	invalid.ram = {{0x410000, 0x410010}};
	assert(!rec::build_native(b.maincpu_rom(), invalid));
	invalid.ram = {{0x40d000, 0x40d010}, {0x40d008, 0x40d018}};
	assert(!rec::build_native(b.maincpu_rom(), invalid));
	second.run(8);
	assert(b.cpu().reg(0) == 2 && !rec::fallback_count());
	puts("Native RAM guards: independent contexts with different bytes at the same PC passed");
}

void equivalence(game::id title, const char *regions, const char *map, bool collect, const char *recording = nullptr)
{
	const bool wingwar = title == game::wingwar::ID;
	model1 reference(load_regions(regions, title));
	model1 native(load_regions(regions, title));
	input_replay::recording replay;
	if (recording) {
		const auto error = input_replay::load(recording, title, replay);
		if (!error.empty()) { fprintf(stderr, "%s\n", error.c_str()); std::abort(); }
	}
	input_replay::start(reference, replay);
	input_replay::start(native, replay);
	reference.reset();
	native.reset();
	auto play = wingwar && !recording ? game::make_auto_play(title, "attract", 1, true) : nullptr;
	const int frames = recording ? int(replay.frames.size()) : wingwar ? play->min_frames() : 3600;
	rec::context runtime;
	runtime.attach(native.cpu(), title.name);
	rec::hints h;
	assert(h.read(map));
	if (wingwar) assert(h.ram.size() == 1 && h.ram[0].first == 0x40d000 && h.ram[0].last == 0x40e000);
	else assert(h.ram.empty());
	rec::native_stats st;
	assert(rec::build_native(native.maincpu_rom(), h, &st));
	native.run_cpu = [&](int n) { runtime.run(n); };
	size_t lines = 0;
	std::vector<std::array<uint32_t, 69>> expected;
	reference.on_line = [&](int) {
		std::array<uint32_t, 69> state;
		for (int i = 0; i < 68; i++) state[i] = reference.cpu().reg(i);
		state[68] = reference.cpu().psw();
		expected.push_back(state);
	};
	native.on_line = [&](int line) {
		std::array<uint32_t, 69> state;
		for (int i = 0; i < 68; i++) state[i] = native.cpu().reg(i);
		state[68] = native.cpu().psw();
		if (state != expected.at(line)) {
			fprintf(stderr, "Native mismatch at frame %llu line %d\n",
				(unsigned long long)native.frame_number(), line);
			for (int i = 0; i < 69; i++)
				if (state[i] != expected[line][i])
					fprintf(stderr, "r%d: interpreter %08x native %08x\n", i, expected[line][i], state[i]);
			std::abort();
		}
		lines++;
	};
	for (int f = 1; f <= frames; f++) {
		game::script_inputs in;
		if (recording) in = replay.frames[f - 1];
		else if (play) in = play->frame(f);
		else {
			const auto &io = game::get_io_profile(title);
			for (size_t c = 0; c < in.analog.size(); c++)
				if (native.has_analog_input(c)) in.analog[c] = io.analog_neutral[c];
		}
		for (auto *m : {&reference, &native}) {
			for (size_t c = 0; c < in.analog.size(); c++)
				if (in.analog[c] >= 0) assert(m->set_analog_input(c, uint8_t(in.analog[c])));
			for (int p = 0; p < 3; p++) m->m_inputs[p] = uint8_t(~in.pressed[p]);
		}
		expected.clear();
		reference.run_frame();
		native.run_frame();
		assert(reference.nvram() == native.nvram() && reference.work_ram() == native.work_ram());
		assert(reference.tgp_cycles_executed() == native.tgp_cycles_executed());
		assert(reference.advanced_ioboard_cycles() == native.advanced_ioboard_cycles());
		assert(reference.frame_quads() == native.frame_quads());
		model1_native_test::lists(reference, native);
		if (play) {
			const auto a = std::static_pointer_cast<game::wingwar::gameplay_state>(reference.game_state());
			const auto b = std::static_pointer_cast<game::wingwar::gameplay_state>(native.game_state());
			for (int buffer = 0; buffer < 2; buffer++) assert(a->cameras[buffer].matrix == b->cameras[buffer].matrix);
			const auto error = play->check(f, native);
			if (!error.empty()) { fprintf(stderr, "%s\n", error.c_str()); std::abort(); }
		}
		if (!collect && rec::fallback_count()) {
			fprintf(stderr, "%.*s native fallback at frame %d PC %06x\n",
				int(title.name.size()), title.name.data(), f, rec::first_fallback_address());
			std::abort();
		}
	}
	if (play) assert(play->finish().empty() && st.overlays->compilations > 0 && st.overlays->cache_hits > 0);
	if (recording && input_replay::ram_hash(native) != replay.expected_ram) {
		fprintf(stderr, "Recorded input RAM hash mismatch\n"); std::abort();
	}
	printf("%.*s native equivalence: %d frames, %zu scanline states, %zu RAM compilations, %zu cache hits; "
		"RAM, TGP/I/O timing, display lists and camera hooks identical; %zu function fallbacks\n",
		int(title.name.size()), title.name.data(), frames, lines,
		st.overlays ? st.overlays->compilations : 0, st.overlays ? st.overlays->cache_hits : 0, rec::fallback_count());
}

}

int main(int argc, char **argv)
{
	if ((argc == 5 || (argc == 6 && !strcmp(argv[5], "--collect-fallbacks"))) &&
		!strcmp(argv[3], "--replay-inputs")) {
		equivalence(game::wingwar::ID, argv[1], argv[2], argc == 6, argv[4]);
		return 0;
	}
	if (argc == 4 && !strcmp(argv[1], "--shared")) {
		equivalence(game::id{"vr"}, argv[2], "assets/games/vr/vr_map.txt", false);
		equivalence(game::id{"swa"}, argv[3], "assets/games/swa/swa_map.txt", false);
		return 0;
	}
	if (argc != 3 && !(argc == 4 && !strcmp(argv[3], "--collect-fallbacks"))) {
		fprintf(stderr, "usage: %s regions-dir provisional-map [--replay-inputs file] [--collect-fallbacks]\n", argv[0]);
		return 2;
	}
	guarded_versions();
	context_isolation();
	equivalence(game::wingwar::ID, argv[1], argv[2], argc == 4);
}
