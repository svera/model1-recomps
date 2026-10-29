// Star Wars Arcade's controls against the ROM's own input test: each seat's buttons (routed by the
// host's control profile, both on the player 1 byte) light the right cells on page 1, and the host's
// analog mapping (io_profile ranges and reversal) moves page 2's markers the right way, inside the
// factory calibration (no INVALID VALUE) at full travel.
#include "../../../src/games/swa/controls.h"
#include "../../../src/host/regions.h"
#include <cassert>
#include <cstdio>
#include <set>
#include <utility>

namespace {

using namespace game::swa;
constexpr uint8_t TEST = 0x04, SERVICE = 0x08, COIN1 = 0x01;
constexpr int W = 496;

struct rig
{
	model1 m;
	explicit rig(const char *dir) : m(load_regions(dir, ID))
	{
		assert(m.real_ioboard());
		m.enable_sound();   // the boot waits on the sound board
		m.reset();
		m.run_cpu = [this](int cycles) { m.cpu().run(cycles); };
		center();
	}
	void run(int frames, const uint8_t (&in)[3] = { 0, 0, 0 })
	{
		for (int f = 0; f < frames; f++) {
			for (int i = 0; i < 3; i++) m.m_inputs[i] = uint8_t(~in[i]);
			m.run_frame();
			m.m_audio_out.clear();
		}
	}
	void system(uint8_t bits, int settle)
	{
		const uint8_t in[3] = { bits, 0, 0 };
		run(10, in);
		run(settle);
	}
	void analog(int channel, uint8_t host) { assert(m.set_analog_input(channel, IO_PROFILE.analog_port(channel, host))); }
	void center()
	{
		for (int c : { 0, 1, 2, 4, 5 }) assert(m.set_analog_input(c, IO_PROFILE.analog_neutral[c]));
	}
	// past the boot, into TEST MENU > INPUT TEST (three Service presses from INDIVIDUAL)
	void input_test()
	{
		run(1300);
		system(TEST, 60);
		for (int i = 0; i < 3; i++) system(SERVICE, 40);
		system(TEST, 60);
	}
	uint32_t px(int x, int y) const { return m.frame().data()[size_t(y) * m.frame_w() + x] & 0xffffff; }
};

bool is_rgb(uint32_t p, int r, int g, int b)
{
	const int pr = (p >> 16) & 0xff, pg = (p >> 8) & 0xff, pb = p & 0xff;
	return std::abs(pr - r) < 40 && std::abs(pg - g) < 40 && std::abs(pb - b) < 40;
}

// page 1: the lit (yellow) cells, as (column, row): column 0 Gunner, 1 Pilot; rows Laser, Torpedo,
// VR button, Start; (2, 0) and (2, 1): coin chutes 1 and 2
std::set<std::pair<int, int>> lit(const rig &r)
{
	assert(r.m.frame_w() == W);
	std::set<std::pair<int, int>> cells;
	auto count = [&](int x0, int x1, int y0, int y1) {
		int n = 0;
		for (int y = y0; y < y1; y++)
			for (int x = x0; x < x1; x++) n += is_rgb(r.px(x, y), 255, 206, 0);
		return n;
	};
	for (int row = 0; row < 4; row++)
		for (int column = 0; column < 2; column++)
			if (count(column ? 350 : 240, column ? 430 : 320, 100 + row * 24, 120 + row * 24) > 20) cells.insert({ column, row });
	if (count(280, 330, 275, 295) > 20) cells.insert({ 2, 0 });
	if (count(400, 460, 275, 295) > 20) cells.insert({ 2, 1 });
	return cells;
}

std::set<std::pair<int, int>> press(rig &r, int player, int action)
{
	uint8_t in[3] = { 0, 0, 0 };
	CONTROLS.press(in, player, action);
	assert(in[2] == 0);   // both seats are on the player 1 byte
	r.run(20, in);
	const auto cells = lit(r);
	r.run(20);
	assert(lit(r).empty());
	return cells;
}

void buttons(const char *dir)
{
	rig r(dir);
	r.input_test();
	assert(lit(r).empty());
	using cells = std::set<std::pair<int, int>>;
	assert(press(r, 0, C_LASER) == (cells{ { 1, 0 } }));
	assert(press(r, 0, C_TORPEDO) == (cells{ { 1, 1 } }));
	assert(press(r, 0, C_VIEW) == (cells{ { 1, 2 } }));
	assert(press(r, 1, C_LASER) == (cells{ { 0, 0 } }));
	assert(press(r, 1, C_TORPEDO) == (cells{ { 0, 1 } }));
	assert(press(r, 1, C_VIEW) == cells{});   // the Gunner has none
	for (int p = 0; p < 2; p++) {
		const uint8_t in[3] = { CONTROLS.start_bits[p], 0, 0 };
		r.run(20, in);
		assert(lit(r) == (cells{ { 1, 3 } }));   // one Start, the Pilot's
		r.run(20);
	}
	const uint8_t coin[3] = { COIN1, 0, 0 };
	r.run(20, coin);
	assert(lit(r) == (cells{ { 2, 0 } }));
	puts("SWA controls: Pilot and Gunner buttons, Start and coin light their input-test cells");
}

// page 2's markers: the stick X (red; the value text is red only when invalid), stick Y (green) and throttle (yellow)
struct markers { int gunner_x, gunner_y, pilot_x, pilot_y, throttle; bool invalid; };

int centroid(const rig &r, int x0, int x1, int y0, int y1, int cr, int cg, int cb, bool vertical)
{
	long sum = 0, n = 0;
	for (int y = y0; y < y1; y++)
		for (int x = x0; x < x1; x++)
			if (is_rgb(r.px(x, y), cr, cg, cb)) { sum += vertical ? y : x; n++; }
	if (n <= 3) fprintf(stderr, "no marker in %d-%d x %d-%d\n", x0, x1, y0, y1);
	assert(n > 3);
	return int(sum / n);
}

markers read_markers(const rig &r)
{
	markers k{};
	k.gunner_x = centroid(r, 80, 236, 150, 174, 255, 0, 0, false);
	k.pilot_x = centroid(r, 285, 432, 150, 174, 255, 0, 0, false);
	k.gunner_y = centroid(r, 140, 176, 90, 245, 0, 255, 0, true);
	k.pilot_y = centroid(r, 340, 376, 90, 245, 0, 255, 0, true);
	k.throttle = centroid(r, 265, 292, 90, 245, 255, 255, 0, true);
	int red = 0;
	for (int y = 245; y < 265; y++)
		for (int x = 200; x < 320; x++) red += is_rgb(r.px(x, y), 255, 0, 0);
	k.invalid = red > 20;
	return k;
}

void axes(const char *dir)
{
	rig r(dir);
	r.input_test();
	r.system(TEST, 60);   // page 2
	// full host travel: left, forward and the throttle's up (faster) end
	for (int c : { 0, 1, 2, 4, 5 }) r.analog(c, 0);
	r.run(30);
	const markers low = read_markers(r);
	for (int c : { 0, 1, 2, 4, 5 }) r.analog(c, 255);
	r.run(30);
	const markers high = read_markers(r);
	printf("SWA axes: X gunner %d..%d pilot %d..%d; Y gunner %d..%d pilot %d..%d; throttle %d..%d\n",
		low.gunner_x, high.gunner_x, low.pilot_x, high.pilot_x, low.gunner_y, high.gunner_y,
		low.pilot_y, high.pilot_y, low.throttle, high.throttle);
	assert(low.gunner_x < 110 && high.gunner_x > 190);   // left / right
	assert(low.pilot_x < 310 && high.pilot_x > 395);
	assert(low.gunner_y < 120 && high.gunner_y > 215);   // forward: DOWN (dive, the top) / UP
	assert(low.pilot_y < 120 && high.pilot_y > 215);
	assert(low.throttle > 215 && high.throttle < 120);   // up: FAST (the bottom) / SLOW
	assert(!low.invalid && !high.invalid);               // full travel is the calibrated +-100
	r.center();
	r.run(30);
	const markers mid = read_markers(r);
	assert(std::abs(mid.pilot_x - 357) < 12 && std::abs(mid.throttle - 170) < 12 && !mid.invalid);
	assert(r.m.set_analog_input(0, 0));   // beyond the calibration: the game flags it
	r.run(30);
	assert(read_markers(r).invalid);
	puts("SWA controls: stick X/Y, throttle directions and full-travel calibration match the input test");
}

}

int main(int argc, char **argv)
{
	if (argc != 2) {
		fprintf(stderr, "usage: %s regions-dir (requires SWA and Model 1 I/O ROMs)\n", argv[0]);
		return 2;
	}
	buttons(argv[1]);
	axes(argv[1]);
}
