// Star Wars Arcade's scripted play (src/games/swa/script.cpp) on the interpreter, all five scenarios
// with checks on: credits, mission, flight and hits; progression completes the opening objective,
// while the other runs reach game over and attract. Rebel Attack ranks the score, Training does not.
// The default run also checks the Digital Sound Board's in-game music (a looping stream other than
// the attract theme), and that a silent run (the headless tools': model1cov, model1lock) plays the
// same game as one with sound. All five scenarios also run on the native recompile of the shipped map
// (assets/games/swa/swa_map.txt), with no interpreter fallback.
#include "../../../src/games/script.h"
#include "../../../src/games/swa/profile.h"
#include "../../../src/host/regions.h"
#include "../../../src/machine/dsbz80.h"
#include "../../../src/machine/m1audio.h"
#include "../../../src/rt/native.h"
#include <cassert>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace game::swa;

namespace {

constexpr uint32_t THEME_START = 0x334000, THEME_END = 0x3f4000;   // the attract's title theme
constexpr int SILENT_FRAMES = 8000;

uint64_t ram_hash(model1 &m)
{
	uint64_t h = 1469598103934665603ull;
	for (auto *ram : { &m.nvram(), &m.work_ram() })
		for (uint8_t b : *ram) h = (h ^ b) * 1099511628211ull;
	return h;
}

void scripted_axes()
{
	const game::script_inputs unset;
	for (int value : unset.analog) assert(value == -1);
	auto pilot = game::make_auto_play(ID, "", 12345, false);
	auto gunner = game::make_auto_play(ID, "gunner", 12345, false);
	auto repeat = game::make_auto_play(ID, "gunner", 12345, false);
	assert(pilot && gunner && repeat);
	for (int f = 1; f <= 22000; f++) {
		const auto p = pilot->frame(f), g = gunner->frame(f), r = repeat->frame(f);
		assert(g.analog == r.analog && g.pressed == r.pressed);
		for (int c : { 3, 6, 7 }) assert(g.analog[c] == -1);
		for (int c = 3; c < 8; c++) assert(p.analog[c] == -1);
		if (f >= 1600)
			for (int c = 0; c < 3; c++) assert(p.analog[c] == g.analog[c]);
		int x = 127, y = 127;
		if (f >= 3700) {
			const int phase = ((f - 3700) / 120) % 4;
			if (phase < 2) x = phase == 0 ? 27 : 227;
			else y = phase == 2 ? 27 : 227;
		}
		assert(g.analog[4] == x && g.analog[5] == y);
	}
	puts("SWA scripted axes: deterministic Gunner sweeps, unchanged Pilot controls and unused channels");
}

struct reticle
{
	int x0 = 496, x1 = -1, y0 = 384, y1 = -1, pixels = 0;
	bool contains(int x, int y) const { return x >= x0 - 3 && x <= x1 + 3 && y >= y0 - 3 && y <= y1 + 3; }
};

void gunner_aim(const std::string &dir)
{
	reticle centre;
	std::vector<uint32_t> reference;
	for (int direction = 0; direction < 5; direction++) {
		model1 m(load_regions(dir, ID));
		m.reset();
		m.set_drawing(false);
		auto play = game::make_auto_play(ID, "gunner", 12345, false);
		assert(play);
		for (int f = 1; f <= 4000; f++) {
			auto in = play->frame(f, m);
			if (f >= 3700) {
				in.analog[0] = in.analog[1] = 127;
				in.analog[4] = direction == 1 ? 27 : direction == 2 ? 227 : 127;
				in.analog[5] = direction == 3 ? 27 : direction == 4 ? 227 : 127;
				in.pressed[1] = 0;
			}
			for (size_t c = 0; c < in.analog.size(); c++)
				if (in.analog[c] >= 0) assert(m.set_analog_input(c, uint8_t(in.analog[c])));
			for (size_t p = 0; p < in.pressed.size(); p++) m.m_inputs[p] = uint8_t(~in.pressed[p]);
			if (f == 4000) m.set_drawing(true);
			m.run_frame();
		}
		assert((m.nvram()[0xf334] | m.nvram()[0xf335] << 8) < 180 * 30);
		const auto &frame = m.frame();
		reticle aim;
		for (int y = 0; y < m.frame_h(); y++)
			for (int x = 0; x < m.frame_w(); x++)
				if ((frame.pix(y, x) & 0xffffff) == 0x63f700) { // Gunner reticle, distinct from the green Pilot label
					aim.x0 = std::min(aim.x0, x); aim.x1 = std::max(aim.x1, x);
					aim.y0 = std::min(aim.y0, y); aim.y1 = std::max(aim.y1, y);
					aim.pixels++;
				}
		assert(aim.pixels >= 40 && aim.x1 - aim.x0 <= 26 && aim.y1 - aim.y0 <= 26);
		if (direction == 0) {
			centre = aim;
			reference.assign(frame.data(), frame.data() + size_t(m.frame_w()) * m.frame_h());
			assert(std::abs(aim.x0 + aim.x1 - 496) < 6);
			assert(std::abs(aim.y0 + aim.y1 - 382) < 6);
		} else {
			const int dx = (aim.x0 + aim.x1 - centre.x0 - centre.x1) / 2;
			const int dy = (aim.y0 + aim.y1 - centre.y0 - centre.y1) / 2;
			if (direction <= 2) {
				assert(direction == 1 ? dx > 100 : dx < -100);
				assert(std::abs(dy) <= 2);
			} else {
				assert(direction == 3 ? dy > 70 : dy < -70);
				assert(std::abs(dx) <= 2);
			}
			for (int y = 0; y < m.frame_h(); y++)
				for (int x = 0; x < m.frame_w(); x++)
					if (!centre.contains(x, y) && !aim.contains(x, y))
						assert(frame.pix(y, x) == reference[size_t(y) * m.frame_w() + x]);
			printf("SWA Gunner direction %d: reticle moved (%d, %d), Pilot and scene unchanged\n", direction, dx, dy);
		}
	}
}

void continue_requires_inputs(const std::string &dir)
{
	for (bool omit_coin : { true, false }) {
		model1 m(load_regions(dir, ID));
		m.reset();
		m.set_drawing(false);
		auto play = game::make_auto_play(ID, "continue", 12345, true);
		assert(play && play->min_frames() == 36000);
		bool offered = false, rejected = false;
		for (int f = 1; f <= 17500; f++) {
			auto in = play->frame(f, m);
			if (offered) in.pressed[0] &= uint8_t(~(omit_coin ? COIN : START));
			for (size_t c = 0; c < in.analog.size(); c++)
				if (in.analog[c] >= 0) assert(m.set_analog_input(c, uint8_t(in.analog[c])));
			for (size_t p = 0; p < in.pressed.size(); p++) m.m_inputs[p] = uint8_t(~in.pressed[p]);
			m.run_frame();
			const auto &ram = m.nvram();
			if (ram[0xf008] == 13 && ram[0xf009] == 12) offered = true;
			if (offered) {
				const unsigned time = ram[0xf334] | ram[0xf335] << 8;
				assert(time == 0xffff || time / 30 != 180);
			}
			const auto error = play->check(f, m);
			if (!error.empty()) {
				assert(offered);
				rejected = true;
				break;
			}
		}
		assert(offered && (rejected || !play->finish().empty()));
		printf("SWA continue: withholding %s prevents a resumed flight and fails the scenario\n",
			omit_coin ? "the coin" : "Start");
	}
}

void progression_requires_firing(const std::string &dir)
{
	model1 m(load_regions(dir, ID));
	m.enable_sound();
	m.reset();
	auto play = game::make_auto_play(ID, "progression", 12345, true);
	auto repeat = game::make_auto_play(ID, "progression", 98765, true);
	assert(play && repeat && play->min_frames() == 10000 && !play->finish().empty());
	bool flying = false;
	for (int f = 1; f <= 10000; f++) {
		auto in = play->frame(f, m);
		const auto again = repeat->frame(f, m);
		assert(in.analog == again.analog && in.pressed == again.pressed);
		for (int c : { 3, 6, 7 }) assert(in.analog[c] == -1);
		if (f >= 1600) {
			assert(in.analog[0] == 127 && in.analog[1] == 127 && in.analog[2] == 28);
			assert(in.analog[4] >= 27 && in.analog[4] <= 227 && in.analog[5] >= 27 && in.analog[5] <= 227);
			assert(in.pressed[1] == (f % 90 < 6 ?
				PILOT_LASER | GUNNER_LASER | PILOT_TORPEDO | GUNNER_TORPEDO : PILOT_LASER | GUNNER_LASER));
			in.pressed[1] = 0;
		}
		for (size_t c = 0; c < in.analog.size(); c++)
			if (in.analog[c] >= 0) assert(m.set_analog_input(c, uint8_t(in.analog[c])));
		for (size_t p = 0; p < in.pressed.size(); p++) m.m_inputs[p] = uint8_t(~in.pressed[p]);
		m.run_frame();
		m.m_audio_out.clear();
		const auto &ram = m.nvram();
		if (ram[0xf008] == 13) {
			if (ram[0xf009] == 3) flying = true;
			assert(ram[0xf009] != 4 && ram[0xf009] != 5);
		}
		assert(play->check(f, m) == repeat->check(f, m));
		// The missing objective must remain an error, even if the run has not failed yet.
		assert(!play->finish().empty());
	}
	assert(flying);
	puts("SWA progression: deterministic controls across seeds; withholding firing prevents completion");
}

struct music
{
	bool in_game = false, looped = false;
	uint32_t last = 0;
	uint64_t last_triggers = 0;
	int last_state = 0;

	void watch(const dsbz80 &d, bool flying)
	{
		const uint32_t pos = d.position();
		if (flying && d.state() == 2 && (pos < THEME_START || pos >= THEME_END)) in_game = true;
		// the stream jumped back on its own: the loop, not a new command
		if (flying && d.state() == 2 && last_state == 2 && d.triggers() == last_triggers && pos + 0x1000 < last)
			looped = true;
		last = pos;
		last_triggers = d.triggers();
		last_state = d.state();
	}
};

uint64_t run(const std::string &dir, const std::string &scenario, bool sound, int frames, bool checks, music *dsb = nullptr,
	bool native = false)
{
	model1 m(load_regions(dir, ID));
	if (sound) m.enable_sound();
	if (native) {
		rec::hints map;
		assert(map.read("assets/games/swa/swa_map.txt"));
		rec::attach(m.cpu(), "swa");
		assert(rec::build_native(m.program_rom(), map));
		m.run_cpu = [](int n) { rec::run(n); };
	}
	m.reset();
	if (!native) m.run_cpu = [&](int c) { m.cpu().run(c); };
	auto play = game::make_auto_play(ID, scenario, 12345, checks);
	auto repeat = scenario == "progression" ? game::make_auto_play(ID, scenario, 98765, false) : nullptr;
	assert(play);
	if (checks) assert(frames >= play->min_frames());
	bool flying = false, over = false;
	for (int f = 1; f <= frames; f++) {
		const game::script_inputs in = play->frame(f, m);
		if (repeat) {
			const auto again = repeat->frame(f, m);
			assert(in.analog == again.analog && in.pressed == again.pressed);
		}
		for (size_t c = 0; c < in.analog.size(); c++)
			if (in.analog[c] >= 0) assert(m.set_analog_input(c, uint8_t(in.analog[c])));
		for (int p = 0; p < 3; p++) m.m_inputs[p] = uint8_t(~in.pressed[p]);
		m.run_frame();
		m.m_audio_out.clear();
		const std::string problem = play->check(f, m);
		if (!problem.empty()) {
			fprintf(stderr, "%s\n", problem.c_str());
			assert(false);
		}
		if (dsb) {
			const uint16_t time = uint16_t(m.nvram()[0xf334] | m.nvram()[0xf335] << 8);
			if (!flying && time != 0xffff && time != 0) flying = true;
			if (flying && m.nvram()[0xf008] == 14) over = true;
			dsb->watch(*m.audio()->dsb(), flying && !over);
		}
	}
	if (checks) {
		const std::string problem = play->finish();
		if (!problem.empty()) {
			fprintf(stderr, "%s\n", problem.c_str());
			assert(false);
		}
	}
	return ram_hash(m);
}

void progression(const std::string &dir)
{
	progression_requires_firing(dir);
	const auto interpreter = run(dir, "progression", true, 10000, true);
	const auto silent = run(dir, "progression", false, 10000, true);
	assert(interpreter == silent);
	const auto native = run(dir, "progression", true, 10000, true, nullptr, true);
	assert(interpreter == native && rec::fallback_count() == 0);
	puts("SWA progression: interpreter, silent and native gameplay with sound match");
}

}

int main(int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: %s <swa regions dir>\n", argv[0]);
		return 1;
	}
	assert(!game::make_auto_play(ID, "no-such-scenario", 1, false));
	auto quiet = game::make_auto_play(ID, "", 1, false);
	assert(quiet && quiet->finish().empty());
	if (argc > 2 && (std::string(argv[2]) == "--objective" || std::string(argv[2]) == "--progression")) {
		progression(argv[1]);
		return 0;
	}
	scripted_axes();
	gunner_aim(argv[1]);
	if (argc > 2 && std::string(argv[2]) == "--aim") return 0;
	if (argc > 2 && std::string(argv[2]) == "--continue") {
		continue_requires_inputs(argv[1]);
		const auto interpreter = run(argv[1], "continue", true, 36000, true);
		const auto native = run(argv[1], "continue", true, 36000, true, nullptr, true);
		assert(interpreter == native && rec::fallback_count() == 0);
		puts("SWA continue: interpreter and native gameplay with sound match");
		return 0;
	}

	music dsb;
	run(argv[1], "", true, 22000, true, &dsb);
	assert(dsb.in_game && dsb.looped);
	puts("SWA in-game DSB music: a looping stream outside the attract theme");

	run(argv[1], "gunner", true, 22000, true);
	run(argv[1], "training", true, 22000, true);

	const uint64_t with_sound = run(argv[1], "", true, SILENT_FRAMES, false);
	const uint64_t silent = run(argv[1], "", false, SILENT_FRAMES, false);
	assert(with_sound == silent);
	printf("SWA silent run matches the run with sound through frame %d\n", SILENT_FRAMES);

	music native_dsb;
	run(argv[1], "", true, 22000, true, &native_dsb, true);
	assert(native_dsb.in_game && native_dsb.looped);
	assert(rec::fallback_count() == 0);
	puts("SWA native: the scripted game and its music with no interpreter fallback");
	run(argv[1], "training", true, 22000, true, nullptr, true);
	assert(rec::fallback_count() == 0);
	puts("SWA native Training: the scripted game with no interpreter fallback");
	run(argv[1], "gunner", true, 22000, true, nullptr, true);
	assert(rec::fallback_count() == 0);
	puts("SWA native Gunner: aiming and firing with no interpreter fallback");
	continue_requires_inputs(argv[1]);
	const auto continued = run(argv[1], "continue", true, 36000, true);
	assert(run(argv[1], "continue", true, 36000, true, nullptr, true) == continued);
	assert(rec::fallback_count() == 0);
	puts("SWA continue: interpreter and native gameplay with sound match");
	progression(argv[1]);
	puts("SWA gameplay tests passed");
}
