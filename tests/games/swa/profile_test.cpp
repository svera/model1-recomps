#include "../../../src/games/swa/controls.h"
#include "../../../src/games/swa/gameplay.h"
#include "../../../src/machine/model1.h"
#include <algorithm>
#include <bit>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <set>

int main()
{
	using namespace game::swa;
	assert(game::find("swa") == ID);
	const auto &p = game::get(ID);
	assert(p.display_name == "Star Wars Arcade");
	assert(p.romset == "swa");
	assert(p.recompiler_map == "games/swa/swa_map.txt");
	assert(!p.camera_ram && !p.mode_ram);
	assert(p.cpu_cycles_per_instruction == 8);
	const auto &io = game::get_io_profile(ID);
	assert(io.digital_controls[4] == "Start");
	assert(io.digital_controls[5].empty());   // Start 2: wired in MAME, never read
	assert(io.digital_controls[8] == "Pilot Laser" && io.digital_controls[9] == "Pilot Torpedo");
	assert(io.digital_controls[10] == "Gunner Laser" && io.digital_controls[11] == "Gunner Torpedo");
	assert(io.digital_controls[12] == "Pilot VR Button");
	for (int channel : { 3, 6, 7 }) {
		assert(io.analog_controls[channel].empty());
		assert(io.analog_neutral[channel] == 0xff);
	}
	// calibrated ranges: full host travel is the factory calibration's +-100, neutral its 0
	for (int channel : { 0, 1, 4, 5 }) {
		assert(!io.analog_controls[channel].empty());
		assert(io.analog_kinds[channel] == game::analog_kind::centered);
		assert(io.analog_neutral[channel] == 127);
		const bool x = channel == 0 || channel == 4;
		assert(io.analog_port(channel, 0) == (x ? 227 : 27));     // left / forward
		assert(io.analog_port(channel, 255) == (x ? 27 : 227));   // right / back
		assert(io.analog_port(channel, io.analog_host(channel, 127)) == 127);
	}
	assert(io.analog_port(2, 0) == 28 && io.analog_port(2, 255) == 228);   // fast / slow
	assert(io.analog_neutral[2] == 128 && io.analog_port(2, io.analog_host(2, 128)) == 128);
	for (int v = 0; v < 256; v++)
		for (int channel : { 0, 1, 2, 4, 5 }) {
			const uint8_t port = io.analog_port(channel, uint8_t(v));
			assert(port >= 27 && port <= 228);
			assert(std::abs(int(io.analog_host(channel, port)) - v) <= 1);
			if (v) assert(io.analog_ranges[channel].reverse ? port <= io.analog_port(channel, uint8_t(v - 1))
															 : port >= io.analog_port(channel, uint8_t(v - 1)));
		}
	assert(io.analog_host(0, 0) == 255 && io.analog_host(1, 255) == 255);   // clamped outside the range
	for (int channel : { 0, 1, 2 }) assert(io.analog_player[channel] == 0);
	for (int channel : { 4, 5 }) assert(io.analog_player[channel] == 1);
	assert(io.host_axis_defaults[0] == game::host_axis::left_x && io.host_axis_defaults[1] == game::host_axis::left_y);
	assert(io.host_axis_defaults[2] == game::host_axis::right_y);
	assert(io.host_axis_defaults[4] == game::host_axis::left_x && io.host_axis_defaults[5] == game::host_axis::left_y);
	// other games keep the identity mapping
	const auto &vr = game::get_io_profile(*game::find("vr"));
	for (int v = 0; v < 256; v++) assert(vr.analog_port(0, uint8_t(v)) == v && vr.analog_host(0, uint8_t(v)) == v);
	assert(vr.analog_player[1] == 0);

	// buttons: both seats on the player 1 byte, the Gunner's on its own bits
	auto pressed = [](int player, int action) {
		uint8_t in[3] = { 0, 0, 0 };
		CONTROLS.press(in, player, action);
		assert(in[0] == 0 && in[2] == 0);
		return in[1];
	};
	assert(pressed(0, C_LASER) == PILOT_LASER && pressed(0, C_TORPEDO) == PILOT_TORPEDO);
	assert(pressed(0, C_VIEW) == PILOT_VIEW);
	assert(pressed(1, C_LASER) == GUNNER_LASER && pressed(1, C_TORPEDO) == GUNNER_TORPEDO);
	assert(pressed(1, C_VIEW) == 0);
	for (int a : { int(C_UP), int(C_DOWN), int(C_LEFT), int(C_RIGHT), int(C_THROTTLE_UP), int(C_THROTTLE_DOWN) })
		assert(pressed(0, a) == 0 && pressed(1, a) == 0);   // analog only
	assert(CONTROLS.start_bits[0] == START && CONTROLS.start_bits[1] == START);
	assert(CONTROLS.visible(C_VIEW, 0) && !CONTROLS.visible(C_VIEW, 1));
	assert(CONTROLS.visible(C_THROTTLE_UP, 0) && !CONTROLS.visible(C_THROTTLE_UP, 1));
	assert(CONTROLS.visible(C_LASER, 1) && CONTROLS.visible(C_TORPEDO, 1) && CONTROLS.visible(C_START, 1));
	for (int a : CONTROLS.pad_actions) assert(CONTROLS.visible(a));
	// every analog channel has keys; the sticks the d-pad too
	for (int channel : { 0, 1, 2, 4, 5 }) {
		int n = 0;
		for (const auto &k : CONTROLS.analog_keys)
			if (k.channel == channel) { n++; assert(k.low >= 0 && k.high >= 0 && k.dpad == (channel != 2)); }
		assert(n == 1);
	}
	// no key does two things for one player, nor is shared between them
	std::set<SDL_Scancode> used;
	for (int p = 0; p < 2; p++)
		for (int a = 0; a < C_COUNT; a++)
			if (CONTROLS.visible(a, p) && CONTROLS.keys[p][a]) assert(used.insert(CONTROLS.keys[p][a]).second);
	assert(game::get_video(ID).smooth_frames && game::get_video(ID).separate_parts);
	assert(game::get_video(ID).default_fps == 30);   // blending is the player's choice
	assert(!game::get_video(ID).pin_hud_edges);

	model1::regions regions;
	regions.title = ID;
	regions.maincpu.resize(0x2000000, 0xff);
	regions.copro_tables.resize(0x40000);
	regions.polygons.resize(0xc00000);
	model1 m(std::move(regions));
	auto gameplay = create_gameplay(m);
	// the opening (the cruiser chase, then the crawl): wide
	m.nvram()[0xf008] = 2;
	m.nvram()[0xf009] = 2;
	m.nvram()[0xf783] = 0;
	assert(gameplay.wide_scene && gameplay.wide_scene(0, false));
	m.nvram()[0xf008] = 3;
	m.nvram()[0xf009] = 3;
	assert(gameplay.wide_scene(0, false));
	// the music credits (the same demo, with its credits flag): 4:3
	m.nvram()[0xf783] = 1;
	assert(!gameplay.wide_scene(0, true));
	m.nvram()[0xf008] = 4;
	m.nvram()[0xf009] = 1;
	assert(gameplay.wide_scene(0, false));
	m.nvram()[0xf783] = 0;
	auto state = std::static_pointer_cast<gameplay_state>(gameplay.state);
	m.cpu().set_reg(25, 0x408000);
	const auto prepare_frame = [&] { gameplay.frame_begin(); };
	m.nvram()[0xf008] = 2;
	m.nvram()[0xf009] = 2;
	prepare_frame();
	assert(!state->z_wrap_prefetched);
	m.nvram()[0xf008] = 3;
	m.nvram()[0xf009] = 2;
	prepare_frame();
	assert(!state->z_wrap_prefetched);
	state->full_distance = true;
	prepare_frame();
	assert(!state->z_wrap_prefetched);
	m.nvram()[0xf770] = 1;
	prepare_frame();
	assert(state->z_wrap_prefetched);
	m.nvram()[0xf009] = 3;
	prepare_frame();
	assert(state->z_wrap_prefetched);
	m.nvram()[0xf770] = 2;
	prepare_frame();
	assert(!state->z_wrap_prefetched);
	m.nvram()[0xf009] = 2;
	prepare_frame();
	assert(!state->z_wrap_prefetched);
	m.nvram()[0xf770] = 1;
	prepare_frame();
	assert(state->z_wrap_prefetched);
	state->full_distance = false;
	prepare_frame();
	assert(!state->z_wrap_prefetched);
	state->full_distance = true;
	m.nvram()[0xf009] = 2;
	prepare_frame();
	assert(state->z_wrap_prefetched);
	m.nvram()[0xf008] = 4;
	prepare_frame();
	assert(!state->z_wrap_prefetched);
	gameplay.reset();
	assert(!state->z_wrap_prefetched);
	m.cpu().set_reg(20, 0x400100);
	for (bool lod : { false, true })
		for (bool full : { false, true }) {
			state->lod = lod;
			state->full_distance = full;
			for (auto [patch, globe] : { std::pair{0x23b68eu, 0x23b682u}, {0x23b692u, 0x23b686u},
				{0x23b696u, 0x23b68au}, {0x451a8u, 0x44f9fu}, {0x45224u, 0x450dau}, {0x452a0u, 0x451a7u} }) {
				const bool model = patch >= 0x230000;
				const uint32_t pc = model ? 0xc18af : 0xc18aa, address = model ? 0x400102 : 0x400106;
				assert(gameplay.read32_value(pc, address, patch) == (lod ? patch : globe));
				assert(gameplay.read32_value(pc, 0x40, patch) == patch);
				assert(gameplay.read32_value(pc + 1, address, patch) == patch);
			}
		}
	m.work_ram()[0] = 0x12;
	m.work_ram()[1] = 0x34;
	m.work_ram()[2] = 0x56;
	m.work_ram()[3] = 0x78;
	assert(!m.tgp_has_hle());
	assert(m.camera() == 0);
	assert(m.game_mode() == 0);
	assert(!m.past_boot());
	assert(!m.in_test_menu_mode());
	m.forget_camera();
	assert(m.work_ram()[0] == 0x12 && m.work_ram()[1] == 0x34);
	assert(m.work_ram()[2] == 0x56 && m.work_ram()[3] == 0x78);
	// Draw distance Full: a far layer's list facing +z (a 90-degree wedge to 9 cells) gains the cells
	// past it, nearest first, in the same direction only
	std::vector<std::pair<int, int>> list;
	for (int z = 0; z <= 9; z++)
		for (int x = -z; x <= z; x++)
			if (std::hypot(float(x), float(z)) <= 9) list.emplace_back(x, z);
	const auto far = game::swa::far_cells(list, 18);
	assert(!far.empty());
	auto has = [&](int x, int z) { return std::find(far.begin(), far.end(), std::pair{ x, z }) != far.end(); };
	assert(has(0, 10) && has(0, 18) && has(10, 10) && !has(0, 9) && !has(0, -12) && !has(18, 0) && !has(-14, 4));
	for (size_t i = 0; i < far.size(); i++) {
		const auto [x, z] = far[i];
		assert(std::hypot(float(x), float(z)) > 9 && std::hypot(float(x), float(z)) <= 18 && z > 0);
		if (i) assert(far[i - 1].first * far[i - 1].first + far[i - 1].second * far[i - 1].second <= x * x + z * z);
	}
	assert(game::swa::far_cells({}, 18).empty());
	{
		// widescreen: TGP 0x31's full-width right and left extents widen by the extra pixels; its
		// distances, top and bottom, a narrow view's (+-20) and 4:3 stay as sent
		auto word = [](float v) { return std::bit_cast<uint32_t>(v); };
		assert(game::swa::view_extent(2, word(248), 83) == word(331));
		assert(game::swa::view_extent(3, word(-248), 186) == word(-434));
		assert(game::swa::view_extent(2, word(248), 0) == word(248));
		assert(game::swa::view_extent(0, word(50), 83) == word(50));
		assert(game::swa::view_extent(4, word(192), 83) == word(192));
		assert(game::swa::view_extent(2, word(20), 83) == word(20));
		assert(game::swa::view_extent(3, word(-20), 83) == word(-20));
	}
	{
		// widescreen cockpit: a diagonal band at the left edge carries on outwards along its slope
		constexpr uint32_t KEY = 0xff000000, WHITE = 0xffffffff;
		constexpr int EXT = 83, W = 496 + 2 * EXT, H = 384;
		std::vector<uint32_t> px(size_t(W) * H, KEY);
		auto band = [](float x) { return 150.0f + 0.5f * x; };   // rows down going inwards
		for (int x = EXT; x < EXT + 200; x++)
			for (int y = int(band(float(x - EXT))); y < int(band(float(x - EXT))) + 4; y++) px[size_t(y) * W + x] = WHITE;
		assert(game::swa::continue_sides(px.data(), W, H, EXT));
		for (int d : { 1, 40, EXT }) {
			const int x = EXT - d, mid = int(band(float(-d))) + 2;
			assert(px[size_t(mid) * W + x] == WHITE);
			assert(px[size_t(mid + 10) * W + x] != WHITE && px[size_t(mid - 10) * W + x] != WHITE);
		}
		for (int y = 0; y < H; y++)
			for (int x = EXT + 496; x < W; x++) assert(px[size_t(y) * W + x] == KEY);
		std::vector<uint32_t> empty(size_t(W) * H, KEY);
		assert(!game::swa::continue_sides(empty.data(), W, H, EXT));
		assert(!game::swa::continue_sides(px.data(), 496, H, 0));
	}
	puts("SWA: profile, controls, I/O layout, analog calibration, unset camera/mode RAM, draw distance and widescreen cockpit checks passed");
}
