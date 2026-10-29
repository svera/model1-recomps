#include "../../../src/games/swa/controls.h"
#include "../../../src/games/swa/gameplay.h"
#include "../../../src/machine/model1.h"
#include <algorithm>
#include <bit>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>

int main()
{
	using namespace game::swa;
	assert(game::find("swa") == ID);
	const auto &p = game::get(ID);
	assert(p.display_name == "Star Wars Arcade");
	assert(p.romset == "swa");
	assert(p.recompiler_map == "games/swa/swa_map.txt");
	assert(!p.camera_ram && p.mode_ram == 0x40f003 && p.test_menu_mode == 1);
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
	// the test mode flag, in battery-backed RAM; with no camera RAM, the boot counts as over
	m.nvram()[0xf003] = 1;
	assert(m.game_mode() == 1 && m.in_test_menu_mode() && m.past_boot());
	m.nvram()[0xf003] = 0;
	assert(m.game_mode() == 0 && !m.in_test_menu_mode() && m.past_boot());
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
		// merged lists reaching unevenly: from the shortest one's reach (7) out, the cells none draws
		// (here a gap down the middle past 7) join the ring too, the drawn ones don't
		std::set<std::pair<int, int>> drawn(list.begin(), list.end());
		for (auto c : list)
			if (std::abs(c.first) <= 1 && std::hypot(float(c.first), float(c.second)) > 7) drawn.erase(c);
		const auto filled = game::swa::far_cells(list, 18, &drawn, 7);
		auto in = [&](int x, int z) { return std::find(filled.begin(), filled.end(), std::pair{ x, z }) != filled.end(); };
		assert(in(0, 8) && in(1, 8) && in(-1, 9) && in(0, 9) && !in(2, 8) && !in(5, 7) && !in(0, 7) && in(0, 10));
		assert(filled.size() == far.size() + 6);
	}
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
		// widescreen surface sides: the neighbouring headings' cells the heading lacks, within each
		// layer's reach, a coarse cell the heading half draws split into its missing fine cells,
		// fine cells a coarse one already stands for dropped, in (layer, band) order
		using game::swa::cell_bands;
		std::array<cell_bands, 4> own{ cell_bands{ { { 0, 1 } } }, cell_bands{}, cell_bands{ { { 0, 3 } } }, cell_bands{} };
		std::array<cell_bands, 4> other{
			cell_bands{ { { 0, 1 }, { 5, 0 } }, { { 9, 9 } } },   // own, a fine cell, one past reach (6)
			cell_bands{ { { 6, 0 } } },                           // inside the new coarse (3, 0)
			cell_bands{ {}, { { 0, 3 }, { 0, 0 }, { 3, 0 } } },   // own, half drawn by own fine (0, 1), whole
			cell_bands{} };
		const auto sides = game::swa::side_cells(own, { other }, { 6, 8, 4, 8 });
		auto has = [&](bool coarse, int x, int z) {
			return std::count_if(sides.begin(), sides.end(), [&](const auto &c) { return c.coarse == coarse && c.x == x && c.z == z; });
		};
		assert(has(false, 5, 0) == 1 && has(true, 2, 0) == 0);
		assert(has(true, 3, 0) == 1 && has(false, 6, 0) == 0 && has(false, 9, 9) == 0 && has(true, 0, 3) == 0);
		assert(has(true, 0, 0) == 0 && has(false, 0, 0) == 1 && has(false, 1, 0) == 1 && has(false, 1, 1) == 1 && has(false, 0, 1) == 0);
		for (size_t i = 1; i < sides.size(); i++)
			assert((std::pair{ sides[i - 1].layer, sides[i - 1].band } <= std::pair{ sides[i].layer, sides[i].band }));
		assert(game::swa::side_cells(own, {}, { 4, 8, 4, 8 }).empty());
		assert(game::swa::side_headings(0) == 0 && game::swa::side_headings(83) == 1 && game::swa::side_headings(186) == 2);
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
	{
		// the defaults' settings (as 736DC writes them): the game's CRC is 6D3A
		static const uint8_t defaults[0x44] = { 'S', '3', '2', 'A', 0x13, 0x01, 0, 0, 0x3a, 0x6d, 1, 0, 0, 0, 0, 1, 1, 1, 0, 1, 2, 0, 1, 0, 1, 0, 1,
			0, 3, 3, 3, 3, 0x20, 0xe0, 0x20, 0xe0, 0x20, 0xe0, 0x20, 0xe0, 0x20, 0xe0, 0x20, 0xe0, 0x20, 0xe0, 0x20, 0xe0 };
		assert(settings_crc(defaults + 10, 0x3a) == 0x6d3a);
		uint8_t *block = &m.nvram()[0xfc80];
		memset(block, 0xff, 0x80);
		assert(game_assignment_value(m, game_assignment::player_difficulty) == -1);   // not loaded yet
		assert(!set_game_assignment(m, game_assignment::player_difficulty, 0));
		memcpy(block, defaults, sizeof defaults);
		memcpy(block + 0x44, defaults + 8, 0x3c);
		for (int i = 0; i < 3; i++) assert(game_assignment_value(m, game_assignment(i)) == 2);   // 3/5
		assert(game_assignment_value(m, game_assignment::advertise_sound) == 0);
		assert(!set_game_assignment(m, game_assignment::enemy_difficulty, 5));
		assert(!set_game_assignment(m, game_assignment::advertise_sound, 2));
		assert(set_game_assignment(m, game_assignment::enemy_difficulty, 4));
		assert(set_game_assignment(m, game_assignment::advertise_sound, 1));
		assert(block[0x1e] == 5 && block[0x0e] == 1 && block[0x1d] == 3);
		assert(game_assignment_value(m, game_assignment::enemy_difficulty) == 4);
		assert(game_assignment_value(m, game_assignment::advertise_sound) == 1);
		const uint16_t crc = settings_crc(block + 10, 0x3a);
		assert(crc != 0x6d3a && block[8] == uint8_t(crc) && block[9] == crc >> 8);
		assert(!memcmp(block + 8, block + 0x44, 0x3c));   // the backup copy
		// the EEPROM: the same bytes, high first in each word
		const uint8_t *e = m.eeprom();
		if (const uint16_t *w = m.eeprom_words())
			for (int i = 0; i < 0x40; i++) assert(w[i] == (block[2 * i] << 8 | block[2 * i + 1]));
		else assert(!memcmp(e, block, 0x80));
		// COIN ASSIGNMENT's coin/credit setting: #1 by default, then free play (MANUAL SETTING's byte kept)
		const int64_t coins = coin_setting(m);
		assert(coins == 0x0101010001 && !is_free_play(coins));
		const int64_t free = with_coin_entry(coins, COIN_SETTING_FREE_PLAY);
		assert(free == 0x0101010101 && is_free_play(free));
		assert(set_coin_setting(m, free) && !set_coin_setting(m, free));   // (already so: no boot again)
		assert(coin_setting(m) == free && block[0x12] == 1 && block[0x13] == 1);
		assert(game_assignment_value(m, game_assignment::enemy_difficulty) == 4);   // the CRC still holds
		assert(!memcmp(block + 8, block + 0x44, 0x3c));
		if (const uint16_t *w = m.eeprom_words()) assert(w[9] == (block[0x12] << 8 | block[0x13]));
		assert(with_coin_entry(free, COIN_SETTING_1) == coins);
		assert(!set_coin_setting(m, -1) && !set_coin_setting(m, int64_t(1) << 40));
	}
	puts("SWA: profile, controls, I/O layout, analog calibration, unset camera RAM, mode byte, draw distance, widescreen cockpit, game and coin assignment checks passed");
}
