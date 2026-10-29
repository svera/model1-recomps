#include "../../../src/games/vr/gameplay.h"
#include "../../../src/machine/model1.h"
#include "../../../src/host/regions.h"
#include "../../../src/host/png.h"
#include "../../../src/games/script.h"
#include "../../../src/rt/native.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static void scripted_inputs()
{
	const auto title = game::id{ "vr" };
	assert(!game::make_auto_play(title, "unknown", 12345, true));
	auto medium = game::make_auto_play(title, "intermediate", 12345, false);
	assert(medium && medium->frame(3068).analog[0] == 0xb0);
	assert(medium->frame(3130).analog[0] == 0xb0);
	assert(medium->frame(3339).analog[0] == 0xb0);
	auto views = game::make_auto_play(title, "views", 12345, false);
	assert(views);
	for (int view = 0; view < 4; view++) {
		const int f = 4000 + view * 900;
		const auto pressed = views->frame(f).pressed;
		if (view < 3) assert(pressed[0] == uint8_t(0x20 << view));
		else assert(pressed[1] & 0x01);
		const auto released = views->frame(f + 6).pressed;
		assert(!(released[0] & 0xe0) && !(released[1] & 0x01));
	}
	auto service = game::make_auto_play(title, "service", 12345, true);
	assert(service && service->frame(3000).pressed[0] == 0x04);
	assert(service->frame(3006).pressed[0] == 0);
	assert(service->frame(6000).pressed[1] == 0x01);
	assert(service->frame(6006).pressed[1] == 0);
	model1::regions regions;
	regions.title = title;
	model1 machine(std::move(regions));
	assert(service->check(6006, machine).empty());
	assert(!service->finish().empty());
	machine.nvram()[0xdf00] = 0xff;
	assert(service->check(6007, machine).empty());
	assert(!service->finish().empty());
	machine.nvram()[0xdf00] = 0;
	assert(service->check(6008, machine).empty());
	assert(service->finish().empty());
	auto qualifying = game::make_auto_play(title, "qualifying", 12345, true);
	assert(qualifying && qualifying->min_frames() == 26000);
	assert(!qualifying->finish().empty());
	assert(qualifying->frame(3000).pressed[0] == 0x04);
	assert(qualifying->frame(3420).pressed[0] == 0x40);
	assert(qualifying->frame(3580).pressed[1] == 0x01);
	assert(qualifying->frame(4560).pressed[1] == 0x01);
	assert(qualifying->frame(6000).pressed[0] == 0x01);
	assert(qualifying->frame(6060).pressed[0] == 0);
	assert(qualifying->frame(6120).pressed[0] == 0x10);
	assert((qualifying->frame(6130).analog == std::array<int, 8>{ 0x80, 0xff, 0x30, -1, -1, -1, -1, -1 }));
	auto unchecked = game::make_auto_play(title, "qualifying", 12345, false);
	assert(unchecked && unchecked->min_frames() == 0 && unchecked->finish().empty());
	assert(unchecked->frame(6130).analog == qualifying->frame(6130).analog);
	auto compatible = game::make_auto_play(title, "views", 12345, false);
	assert(compatible->frame(4000, machine).pressed == views->frame(4000).pressed);
}

static void language_settings(bool board)
{
	model1::regions regions;
	regions.title = game::id{ "vr" };
	if (board) regions.ioboard.resize(0x10000, 0xff);
	model1 machine(std::move(regions));
	assert(bool(machine.eeprom_words()) == board);
	using game::vr::language_value;
	using game::vr::set_language;
	auto read = [&] {
		std::array<uint16_t, 64> words;
		for (int i = 0; i < 64; i++)
			words[i] = board ? machine.eeprom_words()[i] :
				uint16_t(machine.eeprom()[2 * i] << 8 | machine.eeprom()[2 * i + 1]);
		return words;
	};
	auto write = [&](const std::array<uint16_t, 64> &words) {
		for (int i = 0; i < 64; i++)
			if (board) machine.eeprom_words()[i] = words[i];
			else {
				machine.eeprom()[2 * i] = uint8_t(words[i] >> 8);
				machine.eeprom()[2 * i + 1] = uint8_t(words[i]);
			}
	};
	std::array<uint16_t, 64> words{};
	write(words);
	assert(language_value(machine) == -1);
	assert(!set_language(machine, 1) && read() == words);
	for (int i = 0; i < 64; i++) words[i] = uint16_t(0x4321 + 173 * i);
	words[0] = 0x5345; words[1] = 0x4741;
	words[13] = 0xab00;
	write(words);
	assert(language_value(machine) == 0);
	assert(set_language(machine, 1));
	auto english = read();
	assert(language_value(machine) == 1 && english[13] == 0xab02);
	for (int i = 0; i < 64; i++)
		if (i != 4 && i != 13) assert(english[i] == words[i]);
	uint16_t crc = 0;
	for (int bit = 5 * 16; bit < 64 * 16; bit++) {
		const bool input = (english[bit / 16] >> (15 - bit % 16)) & 1;
		const bool top = crc & 0x8000;
		crc = uint16_t(crc << 1);
		if (top != input) crc ^= 0x1021;
	}
	assert(english[4] == uint16_t(crc >> 8 | crc << 8));
	assert(!set_language(machine, 1) && read() == english);
	assert(!set_language(machine, -1) && !set_language(machine, 2) && read() == english);
	assert(set_language(machine, 0) && language_value(machine) == 0);
	assert(read()[13] == 0xab00);
	words[13] = 0xab01;   // Keep an existing U.S. region when English is already selected.
	write(words);
	assert(language_value(machine) == 1);
	assert(!set_language(machine, 1) && read() == words);
}

static void machine_settings(bool board)
{
	model1::regions regions;
	regions.title = game::id{ "vr" };
	if (board) regions.ioboard.resize(0x10000, 0xff);
	model1 machine(std::move(regions));
	auto read = [&] {
		std::array<uint16_t, 64> words;
		for (int i = 0; i < 64; i++)
			words[i] = board ? machine.eeprom_words()[i] :
				uint16_t(machine.eeprom()[2 * i] << 8 | machine.eeprom()[2 * i + 1]);
		return words;
	};
	auto write = [&](const std::array<uint16_t, 64> &words) {
		for (int i = 0; i < 64; i++)
			if (board) machine.eeprom_words()[i] = words[i];
			else {
				machine.eeprom()[2 * i] = uint8_t(words[i] >> 8);
				machine.eeprom()[2 * i + 1] = uint8_t(words[i]);
			}
	};
	using game::vr::machine_setting;
	using game::vr::machine_setting_value;
	using game::vr::set_machine_setting;
	std::array<uint16_t, 64> words{};
	write(words);
	assert(machine_setting_value(machine, machine_setting::car_color) == -1);
	assert(!set_machine_setting(machine, machine_setting::car_color, 1) && read() == words);
	for (int i = 0; i < 64; i++) words[i] = uint16_t(0x1243 + i * 173);
	words[0] = 0x5345; words[1] = 0x4741;
	words[16] = 0x0100; words[17] = 0;
	write(words);
	constexpr std::array<int, 4> offsets{ 0x22, 0x23, 0x21, 0x20 };
	constexpr std::array<int, 4> counts{ 8, 2, 4, 4 };
	for (size_t i = 0; i < counts.size(); i++) {
		const auto setting = machine_setting(i);
		for (int value = 0; value < counts[i]; value++) {
			const auto before = read();
			const int previous = machine_setting_value(machine, setting);
			assert(set_machine_setting(machine, setting, value) == (previous != value));
			const auto after = read();
			assert(machine_setting_value(machine, setting) == value);
			const int shift = offsets[i] % 2 ? 0 : 8;
			for (int word = 0; word < 64; word++)
				if (word != 4 && word != offsets[i] / 2) assert(after[word] == before[word]);
			assert(after[offsets[i] / 2] ==
				uint16_t((before[offsets[i] / 2] & ~(0xff << shift)) | (value << shift)));
			if (previous != value) {
				uint16_t crc = 0;
				for (int bit = 5 * 16; bit < 64 * 16; bit++) {
					const bool input = (after[bit / 16] >> (15 - bit % 16)) & 1;
					const bool top = crc & 0x8000;
					crc = uint16_t(crc << 1);
					if (top != input) crc ^= 0x1021;
				}
				assert(after[4] == uint16_t(crc >> 8 | crc << 8));
			}
			assert(!set_machine_setting(machine, setting, value) && read() == after);
		}
		const auto before = read();
		assert(!set_machine_setting(machine, setting, -1) && read() == before);
		assert(!set_machine_setting(machine, setting, counts[i]) && read() == before);
	}
	// MONITOR TYPE: EEPROM byte 0x0b, word 5's low byte (the first word the CRC covers)
	for (int value : { 1, 0, 1 }) {
		const auto before = read();
		const int previous = game::vr::monitor_type(machine);
		assert(game::vr::set_monitor_type(machine, value) == (previous != value));
		const auto after = read();
		assert(game::vr::monitor_type(machine) == value);
		assert(after[5] == uint16_t((before[5] & 0xff00) | value));
		for (int word = 0; word < 64; word++)
			if (word != 4 && word != 5) assert(after[word] == before[word]);
		uint16_t crc = 0;
		for (int bit = 5 * 16; bit < 64 * 16; bit++) {
			const bool input = (after[bit / 16] >> (15 - bit % 16)) & 1;
			const bool top = crc & 0x8000;
			crc = uint16_t(crc << 1);
			if (top != input) crc ^= 0x1021;
		}
		assert(after[4] == uint16_t(crc >> 8 | crc << 8));
	}
	const auto before = read();
	assert(!game::vr::set_monitor_type(machine, 2) && !game::vr::set_monitor_type(machine, -1) && read() == before);
}

static void scenery_lists()
{
	uint32_t word = 0;   // the display-list word matrix_objects is told was written
	model1::regions regions;
	regions.title = game::id{ "vr" };
	regions.maincpu.resize(0x1000000, 0xff);
	regions.polygons.resize(0x2000);
	auto write_rom = [&](uint32_t at, uint32_t value) {
		for (int b = 0; b < 4; b++) regions.maincpu[at + b] = uint8_t(value >> (8 * b));
	};
	for (int cell = 0; cell < 256; cell++) {
		const uint32_t record = 0x9000 + uint32_t(cell) * 24;
		write_rom(0x8000 + uint32_t(cell) * 4, record);
		write_rom(record, 1 + uint32_t(cell));
		write_rom(record + 4, 0x40000 + uint32_t(cell));
		write_rom(record + 8, 10);
		write_rom(record + 12, 0);
	}
	write_rom(0x7000, 500);
	write_rom(0x7004, 0x40123);
	write_rom(0x7008, 1);
	write_rom(0x700c, 0);
	const auto original_rom = regions.maincpu;
	model1 machine(std::move(regions));
	auto &ram = machine.work_ram();
	v60_bus &bus = machine;
	auto callbacks = game::vr::create_gameplay(machine);
	auto state = std::static_pointer_cast<game::vr::gameplay_state>(callbacks.state);
	state->lod = true;
	assert(machine.program_rom() == original_rom);
	for (bool lod : { false, true })
	for (int budget : { 0, 10, 5000 })
	for (int center = 0; center < 256; center++) {
		state->lod = lod;
		std::fill(ram.begin(), ram.end(), 0xcd);
		bus.write32(0x501368, 0x407900);
		std::fill_n(ram.begin() + 0x1aa0, 32, 0);
		const int cx = center & 15, cy = center >> 4;
		int n = 0;
		for (int dy = -2; dy <= 2; dy++)
			for (int dx = -2; dx <= 2; dx++) {
				const int x = cx + dx, y = cy + dy;
				if (x < 0 || x > 15 || y < 0 || y > 15) continue;
				const int cell = y * 16 + x;
				ram[0x1cc1 + n++] = uint8_t(cell);
				ram[0x1aa0 + cell / 8] |= uint8_t(1u << (cell % 8));
			}
		ram[0x1cc0] = uint8_t(n);
		bus.write32(0x501948, 0x8000);
		bus.write32(0x5011a0, 37);
		bus.write32(0x5011b0, uint32_t(37 + budget));
		bus.write32(0x500100, 0xff846f);
		machine.cpu().set_reg(31, 0x500100);
		machine.cpu().set_reg(22, uint32_t(center));
		machine.cpu().pc_ref() = 0xff7dc5;
		const auto before = ram;
		callbacks.work_ram_written(0xff);
		assert(callbacks.matrix_objects(word).empty());
		machine.cpu().pc_ref() = 0xff8500;
		const auto objects = callbacks.matrix_objects(word);
		const int submitted = lod ? std::min(n, budget / 10 + 1) : n;
		assert(objects.size() == size_t(256 - submitted));
		std::array<bool, 256> seen{};
		for (int i = 0; i < submitted; i++) seen[ram[0x1cc1 + i]] = true;
		int last_distance = -1;
		for (const auto &object : objects) {
			const int cell = int(object.polygon) - 1;
			assert(cell >= 0 && cell < 256 && !seen[cell]);
			seen[cell] = true;
			assert(object.texture == 0x40000 + uint32_t(cell) && object.size == 0);
			const int dx = (cell & 15) - cx, dy = (cell >> 4) - cy;
			const int distance = dx * dx + dy * dy;
			assert(distance >= last_distance);
			last_distance = distance;
		}
		assert(std::all_of(seen.begin(), seen.end(), [](bool value) { return value; }));
		assert(callbacks.matrix_objects(word).empty());
		assert(ram == before && machine.program_rom() == original_rom);
	}
	for (bool full : { false, true })
	for (bool visible : { false, true }) {
		game::vr::set_extended_draw_distance(machine, full);
		state->extended_draw_distance = full;
		machine.write32(0x501368, 0x407980);
		machine.write32(0x40790a, 0xffcc7d);
		machine.write16(0x407934, 0);
		machine.write32(0x501ac0, visible ? 1 : 0);
		machine.write32(0x40794c, 0x7000);
		for (const auto &[offset, value] : std::array<std::pair<uint32_t, float>, 5>{ {
			{ 0x1e, 10 }, { 0x22, 20 }, { 0x26, 30 }, { 0x5e, 2 }, { 0x67, -.5f }
		} }) machine.write32(0x407900 + offset, std::bit_cast<uint32_t>(value));
		for (uint32_t offset : { 0x2au, 0x2cu, 0x2eu }) machine.write16(0x407900 + offset, 0);
		const auto before = ram;
		state->scenery_pending = true;
		const auto objects = callbacks.matrix_objects(word);
		int boards = 0;
		for (const auto &object : objects)
			if (object.owner) {
				boards++;
				assert(object.owner == 0x407900 && object.polygon == 500 && object.texture == 0x40123);
				assert(object.transform);
				const auto &pose = *object.transform;
				assert(pose[0] == 1 && pose[4] == 1 && pose[8] == 1);
				assert(pose[9] == 10 && pose[10] == 21.5f && pose[11] == 30);
			}
		assert(boards == int(full && !visible));
		assert(ram == before);
	}
	state->extended_draw_distance = true;
	machine.write32(0x501368, 0x407900);
	machine.cpu().pc_ref() = 0xff7dc5;
	callbacks.work_ram_written(0xff);
	bus.write32(0x500100, 0xffddd4);
	machine.cpu().pc_ref() = 0xff8500;
	assert(callbacks.matrix_objects(word).empty() && state->scenery_pending);
	bus.write32(0x500100, 0xff846f);
	assert(!callbacks.matrix_objects(word).empty() && !state->scenery_pending);
	state->extended_draw_distance = false;
	callbacks.work_ram_written(0xff);
	assert(callbacks.matrix_objects(word).empty());
	bus.write32(0x5011b0, 5000);
	assert(bus.read32(0x5011b0) == 5000);
	state->extended_draw_distance = true;
	callbacks.reset();
	assert(!state->scenery_pending);
	machine.cpu().pc_ref() = 0;
	bus.write8(0x501cc0, 0);
	auto machine_state = std::static_pointer_cast<game::vr::gameplay_state>(machine.game_state());
	assert(!machine_state->scenery_pending);
	machine.cpu().pc_ref() = 0xff7dc5;
	bus.write8(0x501cc1, 0);
	assert(!machine_state->scenery_pending);
	bus.write8(0x501cc0, 0);
	assert(machine_state->scenery_pending);
	machine.reset();
	assert(!machine_state->scenery_pending);
}

static void lod_settings()
{
	model1::regions regions;
	regions.title = game::id{ "vr" };
	model1 machine(std::move(regions));
	machine.cpu().set_reg(31, 0x500100);
	const auto state = std::static_pointer_cast<game::vr::gameplay_state>(machine.game_state());
	assert(!state->lod);
	machine.write32(0x5011b0, 5500);
	for (uint32_t pc : { 0xff848au, 0xff84a2u, 0xff8571u, 0u }) {
		machine.cpu().pc_ref() = pc;
		assert(machine.read32(0x5011b0) == (pc == 0xff848a || pc == 0xff84a2 ? 0x7fffffffu : 5500u));
		assert(machine.work_ram()[0x11b0] == uint8_t(5500));
	}
	for (uint32_t caller : { 0xfefbf5u, 0xff0d3bu, 0xff7364u, 0xffaaedu, 0xffac09u, 0xffaf07u }) {
		for (float distance : { -100.0f, -0.0f, 0.0f, 0.1f, 40.0f, 10000.0f }) {
			assert(game::vr::lod_distance(true, caller, distance) == distance);
			const float full = game::vr::lod_distance(false, caller, distance);
			if (caller == 0xfefbf5 || caller == 0xff0d3b) assert(full < 0);
			else assert(full == distance);
		}
	}
	game::vr::set_lod(machine, true);
	assert(state->lod);
	machine.cpu().pc_ref() = 0xff84a2;
	assert(machine.read32(0x5011b0) == 5500);
	machine.reset();
	assert(state->lod);
	game::vr::set_lod(machine, false);
	assert(!state->lod);
}

static void animated_scenery_settings()
{
	model1::regions regions;
	regions.title = game::id{ "vr" };
	model1 machine(std::move(regions));
	machine.cpu().set_reg(31, 0x500100);
	game::vr::set_lod(machine, true);
	for (bool full : { false, true }) {
		game::vr::set_extended_draw_distance(machine, full);
		for (uint32_t pc : { 0xffd77au, 0xffd928u, 0xffdabau, 0xffc1b2u,
			0xffb0ebu, 0xffb769u, 0xffaac4u, 0xffb679u, 0xffd644u, 0xffdc4du, 0xffd0e2u, 0xffcc8cu, 0u }) {
			machine.cpu().pc_ref() = pc;
			const bool wide_mask = pc == 0xffb0eb || pc == 0xffb769 || pc == 0xffaac4 ||
				pc == 0xffb679 || pc == 0xffdc4d || pc == 0xffd0e2;
			const bool narrow_mask = pc == 0xffd77a || pc == 0xffd928 || pc == 0xffdaba || pc == 0xffc1b2;
			for (uint32_t address = 0x501a9c; address <= 0x501ae0; address += 4) {
				machine.write32(address, 0x12345678);
				const bool mask = (narrow_mask && address >= 0x501aa0 && address <= 0x501abc) ||
					(wide_mask && address >= 0x501ac0 && address <= 0x501adc);
				assert(machine.read32(address) == (full && mask ? 0xffffffffu : 0x12345678u));
				assert(machine.work_ram()[address - 0x500000] == 0x78);
			}
			machine.write8(0x501a35, 0);
			assert(machine.read8(0x501a35) == 0);
		}
		machine.write32(0x5011b0, 1234);
		for (uint32_t caller : { 0xffd809u, 0xffd85au, 0xffd9e8u, 0xffda21u, 0xffda67u,
			0xffdb49u, 0xffdb9au, 0xffdbd5u, 0xffb254u, 0xffb81cu, 0xffb859u, 0xffc235u, 0xffc251u,
			0xffab97u, 0xffb72cu, 0xffddd4u, 0xffddebu, 0xffde6eu, 0xffdedcu, 0xffdf42u,
			0xffd19cu, 0xffcec9u, 0xff846fu, 0xffd72bu, 0u }) {
			machine.write32(0x500100, caller);
			for (uint32_t pc : { 0xff84a2u, 0xff848au, 0xff8571u }) {
				machine.cpu().pc_ref() = pc;
				const bool animated = (caller >= 0xffd809 && caller <= 0xffdbd5) ||
					caller == 0xffb254 || caller == 0xffb81c || caller == 0xffb859 || caller == 0xffc235 ||
					caller == 0xffc251 || caller == 0xffab97 || caller == 0xffb72c ||
					caller == 0xffddd4 || caller == 0xffddeb || caller == 0xffde6e ||
					caller == 0xffdedc || caller == 0xffdf42 || caller == 0xffd19c;
				assert(machine.read32(0x5011b0) ==
					(full && animated && pc != 0xff8571 ? 0x7fffffffu : 1234u));
			}
		}
	}
}

static void rom_lod(const char *dir, bool native)
{
	model1 arcade(load_regions(dir, game::id{ "vr" })), full(load_regions(dir, game::id{ "vr" }));
	arcade.set_smooth(false);
	full.set_smooth(false);
	game::vr::set_lod(arcade, true);
	if (native) {
		rec::hints map;
		assert(map.read("assets/games/vr/vr_map.txt"));
		rec::attach(full.cpu(), "vr");
		assert(rec::build_native(full.program_rom(), map));
		full.run_cpu = [](int n) { rec::run(n); };
	}
	arcade.reset();
	full.reset();
	const auto rom = full.program_rom();
	auto play = game::make_auto_play(game::id{ "vr" }, "alternate", 12345, false);
	assert(play);
	int more = 0, far = 0, near = 0;
	arcade.on_instruction = [&](uint32_t pc) {
		if (pc == 0xfefbf8 || pc == 0xff0d3e) {
			if (arcade.cpu().reg(11) & 0x80000000) near++;
			else far++;
		}
	};
	if (!native) full.on_instruction = [&](uint32_t pc) {
		if (pc == 0xfefbf8 || pc == 0xff0d3e) assert(full.cpu().reg(11) & 0x80000000);
	};
	for (int f = 1; f <= 4800; f++) {
		const auto inputs = play->frame(f);
		for (size_t c = 0; c < inputs.analog.size(); c++) {
			if (inputs.analog[c] >= 0) {
				assert(arcade.set_analog_input(c, uint8_t(inputs.analog[c])));
				assert(full.set_analog_input(c, uint8_t(inputs.analog[c])));
			}
		}
		for (size_t c = 0; c < inputs.pressed.size(); c++)
			arcade.m_inputs[c] = full.m_inputs[c] = uint8_t(~inputs.pressed[c]);
		arcade.run_frame();
		full.run_frame();
		assert(arcade.work_ram()[0x1138] == full.work_ram()[0x1138]);
		for (uint32_t car = 0x400d00; car <= 0x401e80; car += 0x280)
			for (uint32_t field : { 0x1eu, 0x22u, 0x26u, 0x30u, 0x3cu, 0x44u, 0x191u, 0x1abu })
				assert(arcade.read32(car + field) == full.read32(car + field));
		if (full.frame_quads() > arcade.frame_quads()) more++;
	}
	printf("VR LOD (%s): %d fuller-detail frames, %d near and %d distant arcade selections, unchanged car simulation\n",
		native ? "native" : "interpreter", more, near, far);
	assert(more > 100 && far > 100 && near > 100);
	assert(full.program_rom() == rom);
	game::vr::set_lod(full, true);
	full.on_instruction = {};
	for (int f = 0; f < 120; f++) {
		arcade.run_frame();
		full.run_frame();
		if (f > 10) assert(arcade.frame_quads() == full.frame_quads());
	}
	if (native) assert(rec::fallback_count() == 0);
}

static void rom_language(const char *dir, bool native)
{
	model1 machine(load_regions(dir, game::id{ "vr" }));
	if (native) {
		rec::hints map;
		assert(map.read("assets/games/vr/vr_map.txt"));
		rec::attach(machine.cpu(), "vr");
		assert(rec::build_native(machine.program_rom(), map));
		machine.run_cpu = [](int n) { rec::run(n); };
	}
	machine.reset();
	for (int f = 0; f < 900; f++) machine.run_frame();
	assert(game::vr::language_value(machine) >= 0);
	if (native) assert(rec::fallback_count() == 0);
	const char *shots = getenv("VR_LANGUAGE_SHOTS");
	for (int language : { 1, 0 }) {
		game::vr::set_language(machine, language);
		assert(game::vr::language_value(machine) == language);
		const uint16_t settings = machine.eeprom_words()[13], crc = machine.eeprom_words()[4];
		machine.reset();
		if (native) rec::attach(machine.cpu(), "vr");
		for (int f = 1; f <= (shots ? 3500 : 900); f++) {
			machine.run_frame();
			if (shots && (f == 60 || f == 3150 || f == 3300 || f == 3500)) {
				const auto &image = machine.frame();
				const std::string path = std::string(shots) + "/" +
					(language ? "english" : "japanese") + "-" + std::to_string(f) + ".png";
				if (!write_png(path.c_str(), image.data(), image.width(), image.height())) {
					fprintf(stderr, "VR language: cannot write comparison picture to %s\n", path.c_str());
					std::abort();
				}
			}
		}
		assert(machine.nvram()[0xdc9b] == (language ? 2 : 0));
		assert(machine.eeprom_words()[13] == settings && machine.eeprom_words()[4] == crc);
		printf("VR language (%s): %s survives reset with native country %d and unchanged EEPROM checksum\n",
			native ? "native" : "interpreter", language ? "English" : "Japanese", machine.nvram()[0xdc9b]);
		if (native) {
			if (rec::fallback_count())
				fprintf(stderr, "VR language fallback: %06x (%zu calls)\n", rec::first_fallback_address(), rec::fallback_count());
			assert(rec::fallback_count() == 0);
		}
	}
}

static void rom_draw_distance(const char *dir, bool native)
{
	const game::id vr{ "vr" };
	model1 arcade(load_regions(dir, vr)), extended(load_regions(dir, vr));
	game::vr::set_extended_draw_distance(arcade, false);
	if (native) {
		rec::hints map;
		assert(map.read("assets/games/vr/vr_map.txt"));
		rec::attach(extended.cpu(), "vr");
		assert(rec::build_native(extended.program_rom(), map));
		extended.run_cpu = [](int n) { rec::run(n); };
	}
	arcade.reset();
	extended.reset();
	auto play = game::make_auto_play(vr, "", 12345, false);
	assert(play);
	int extra_quads_frames = 0, most_extra_at = 0;
	size_t max_extra_quads = 0;
	for (int f = 1; f <= 6000; f++) {
		const auto inputs = play->frame(f);
		for (size_t c = 0; c < inputs.analog.size(); c++) {
			if (inputs.analog[c] >= 0) {
				assert(arcade.set_analog_input(c, uint8_t(inputs.analog[c])));
				assert(extended.set_analog_input(c, uint8_t(inputs.analog[c])));
			}
		}
		for (size_t c = 0; c < inputs.pressed.size(); c++)
			arcade.m_inputs[c] = extended.m_inputs[c] = uint8_t(~inputs.pressed[c]);
		arcade.run_frame();
		extended.run_frame();
		assert(arcade.work_ram() == extended.work_ram());
		assert(arcade.nvram() == extended.nvram());
		for (int r = 0; r <= 32; r++) assert(arcade.cpu().reg(r) == extended.cpu().reg(r));
		if (!memcmp(&extended.work_ram()[0x1b00], &arcade.work_ram()[0x1b00], 48) &&
			extended.frame_quads() > arcade.frame_quads()) {
			extra_quads_frames++;
			const size_t extra = size_t(extended.frame_quads() - arcade.frame_quads());
			if (extra > max_extra_quads) {
				max_extra_quads = extra;
				most_extra_at = f;
				if (const char *shots = getenv("VR_DISTANCE_SHOTS")) {
					const std::string dir(shots);
					const auto &a = arcade.frame(), &b = extended.frame();
					if (!write_png((dir + "/arcade.png").c_str(), a.data(), a.width(), a.height()) ||
						!write_png((dir + "/extended.png").c_str(), b.data(), b.width(), b.height())) {
						fprintf(stderr, "VR draw distance: cannot write comparison pictures to %s\n", shots);
						std::abort();
					}
				}
			}
		}
	}
	assert(extra_quads_frames > 100 && max_extra_quads > 100);
	if (native) {
		if (rec::fallback_count())
			fprintf(stderr, "VR extended draw distance fallback: %06x (%zu calls)\n", rec::first_fallback_address(), rec::fallback_count());
		assert(rec::fallback_count() == 0);
	}
	printf("VR extended (%s): unchanged CPU registers and game RAM, %d matching-camera frames with more polygons, up to %zu extra quads at frame %d\n",
		native ? "native" : "interpreter", extra_quads_frames, max_extra_quads, most_extra_at);
}

static void rom_machine_settings(const char *dir, bool native)
{
	model1 machine(load_regions(dir, game::id{ "vr" }));
	if (native) {
		rec::hints map;
		assert(map.read("assets/games/vr/vr_map.txt"));
		rec::attach(machine.cpu(), "vr");
		assert(rec::build_native(machine.program_rom(), map));
		machine.run_cpu = [](int n) { rec::run(n); };
	}
	machine.reset();
	for (int f = 0; f < 900; f++) machine.run_frame();
	const auto rom = machine.program_rom();
	constexpr std::array<int, 4> offsets{ 0x22, 0x23, 0x21, 0x20 };
	constexpr std::array<std::array<int, 4>, 4> configurations{ {
		{ 7, 1, 3, 3 }, { 2, 0, 2, 2 }, { 1, 1, 1, 0 }, { 0, 0, 0, 1 }
	} };
	for (const auto &values : configurations) {
		for (size_t i = 0; i < values.size(); i++) {
			const auto setting = game::vr::machine_setting(i);
			const bool changed = game::vr::machine_setting_value(machine, setting) != values[i];
			assert(game::vr::set_machine_setting(machine, setting, values[i]) == changed);
		}
		std::array<uint16_t, 64> saved;
		std::copy_n(machine.eeprom_words(), saved.size(), saved.begin());
		machine.reset();
		if (native) rec::attach(machine.cpu(), "vr");
		for (int f = 0; f < 900; f++) machine.run_frame();
		assert(std::equal(saved.begin(), saved.end(), machine.eeprom_words()));
		for (size_t i = 0; i < values.size(); i++) {
			assert(game::vr::machine_setting_value(machine, game::vr::machine_setting(i)) == values[i]);
			assert(machine.nvram()[0xdc80 + offsets[i]] == values[i]);
		}
		assert(machine.program_rom() == rom);
		if (native) {
			if (rec::fallback_count())
				fprintf(stderr, "VR settings fallback: %06x (%zu calls)\n", rec::first_fallback_address(), rec::fallback_count());
			assert(rec::fallback_count() == 0);
		}
	}
	printf("VR machine settings (%s): arcade reads all four choices after reboot, with unchanged EEPROM and ROM\n",
		native ? "native" : "interpreter");
}

static void rom_monitor(const char *dir, bool native)
{
	model1 machine(load_regions(dir, game::id{ "vr" }));
	machine.set_aspect(4.0f / 3.0f);
	if (native) {
		rec::hints map;
		assert(map.read("assets/games/vr/vr_map.txt"));
		rec::attach(machine.cpu(), "vr");
		assert(rec::build_native(machine.program_rom(), map));
		machine.run_cpu = [](int n) { rec::run(n); };
	}
	machine.reset();
	for (int f = 0; f < 900; f++) machine.run_frame();
	assert(game::vr::set_coin_setting(machine, game::vr::FREE_PLAY));
	const auto rom = machine.program_rom();
	std::array<std::vector<uint32_t>, 2> frames;
	for (int wide : { 0, 1 }) {
		game::vr::set_monitor_type(machine, wide);
		assert(game::vr::monitor_type(machine) == wide);
		machine.reset();
		if (native) rec::attach(machine.cpu(), "vr");
		int racing = 0;
		for (int f = 1; f <= 6000 && racing < 120; f++) {
			machine.m_inputs[0] = uint8_t(~((f >= 3000 && f < 3006) || (f >= 3120 && f < 3126) ? 0x10 : 0));
			machine.m_inputs[1] = 0xff;
			assert(machine.set_analog_input(0, 0x80));
			assert(machine.set_analog_input(1, 0xff));
			machine.run_frame();
			if (machine.work_ram()[0x1138] == 0x14) racing++;
		}
		assert(racing == 120);
		// the arcade's own copy, as its test menu shows it
		assert(machine.nvram()[0xdc8b] == wide);
		assert(machine.frame_w() == 496 && machine.frame_h() == 384);
		frames[wide].assign(machine.frame().data(), machine.frame().data() + size_t(machine.frame_w()) * machine.frame_h());
		if (const char *shots = getenv("VR_MONITOR_SHOTS")) {
			const std::string path = std::string(shots) + "/monitor-" + std::to_string(wide) + ".png";
			assert(write_png(path.c_str(), machine.frame().data(), machine.frame_w(), machine.frame_h()));
		}
	}
	// 16:9 WIDE draws a different (squeezed, wider) 3D picture in the same 496x384
	size_t differ = 0;
	for (size_t i = 0; i < frames[0].size(); i++) differ += frames[0][i] != frames[1][i];
	assert(differ > frames[0].size() / 10);
	assert(machine.program_rom() == rom);
	if (native) {
		if (rec::fallback_count())
			fprintf(stderr, "VR monitor fallback: %06x\n", rec::first_fallback_address());
		assert(rec::fallback_count() == 0);
	}
	printf("VR monitor type (%s): arcade reads 4:3 and 16:9 WIDE after reboot and draws them differently (%zu pixels)\n",
		native ? "native" : "interpreter", differ);
}

static void rom_cameras(const char *dir, bool native)
{
	model1 machine(load_regions(dir, game::id{ "vr" }));
	if (native) {
		rec::hints map;
		assert(map.read("assets/games/vr/vr_map.txt"));
		rec::attach(machine.cpu(), "vr");
		assert(rec::build_native(machine.program_rom(), map));
		machine.run_cpu = [](int n) { rec::run(n); };
	}
	machine.reset();
	for (int f = 0; f < 900; f++) machine.run_frame();
	assert(game::vr::set_coin_setting(machine, game::vr::FREE_PLAY));
	machine.reset();
	for (int f = 1; f <= 6000 && machine.work_ram()[0x1138] != 0x14; f++) {
		machine.m_inputs[0] = uint8_t(~((f >= 3000 && f < 3006) || (f >= 3120 && f < 3126) ? 0x10 : 0));
		assert(machine.set_analog_input(0, 0x80));
		machine.run_frame();
	}
	assert(machine.work_ram()[0x1138] == 0x14);
	game::vr::camera_cycle cameras;
	auto frame = [&](bool pressed) {
		std::fill(std::begin(machine.m_inputs), std::end(machine.m_inputs), 0xff);
		cameras.update(machine, pressed);
		machine.run_frame();
	};
	for (int f = 0; f < 30; f++) frame(false);
	const int initial = machine.read8(0x501d02);
	constexpr std::array<int, 4> next{ 3, 0, 1, 2 };
	int expected = initial;
	for (int press = 0; press < 8; press++) {
		expected = next[expected];
		for (int f = 0; f < 30; f++) frame(true);
		assert(machine.read8(0x501d02) == expected);
		for (int f = 0; f < 10; f++) frame(false);
		assert(machine.read8(0x501d02) == expected);
	}
	assert(machine.read8(0x501d02) == initial);
	machine.reset();
	for (int f = 0; f < 900; f++) frame(true);
	assert(machine.m_inputs[0] == 0xff && machine.m_inputs[1] == 0xff);
	if (native) {
		if (rec::fallback_count())
			fprintf(stderr, "VR camera fallback: %06x\n", rec::first_fallback_address());
		assert(rec::fallback_count() == 0);
	}
	printf("VR cameras (%s): all four views cycle twice, holding does not repeat, reset clears pulses\n",
		native ? "native" : "interpreter");
}

static void rom_car_color(const char *dir, bool native)
{
	model1 machine(load_regions(dir, game::id{ "vr" }));
	const auto original_rom = machine.program_rom();
	assert(game::vr::enable_standalone_car_color(machine));
	assert(game::vr::enable_standalone_car_color(machine));
	if (native) {
		rec::hints map;
		assert(map.read("assets/games/vr/vr_map.txt"));
		rec::attach(machine.cpu(), "vr");
		assert(rec::build_native(machine.program_rom(), map));
		machine.run_cpu = [](int n) { rec::run(n); };
	}
	machine.reset();
	for (int f = 0; f < 900; f++) machine.run_frame();
	assert(game::vr::set_coin_setting(machine, game::vr::FREE_PLAY));
	for (int color : { 0, 2, 1, 3, 4, 5, 6, 7 }) {
		game::vr::set_machine_setting(machine, game::vr::machine_setting::car_color, color);
		if (native) {
			assert(rec::fallback_count() == 0);
			rec::attach(machine.cpu(), "vr");
		}
		machine.reset();
		int racing = 0;
		for (int f = 1; f <= 6000 && racing < 120; f++) {
			machine.m_inputs[0] = uint8_t(~((f >= 3000 && f < 3006) || (f >= 3120 && f < 3126) ? 0x10 : 0));
			assert(machine.set_analog_input(0, 0x80));
			assert(machine.set_analog_input(1, 0xff));
			machine.run_frame();
			if (machine.work_ram()[0x1138] != 0x14) continue;
			racing++;
			if (racing < 4) continue;
			const uint32_t player = machine.read32(0x501320);
			assert(machine.read8(player + 4) == 0);
			assert(machine.read32(player + 0x3c) == machine.read32(0xfcd395 + 0x1c));
			const uint32_t table = color ? 0xfcd195 + uint32_t(color) * 32 : 0xfcd395;
			for (const auto &[field, entry] : std::array<std::pair<uint32_t, uint32_t>, 5>{ {
				{ 0x18, 4 }, { 0xac, 0 }, { 0xa2, 8 }, { 0xa8, 12 }, { 0xb0, 16 }
			} })
				if (machine.read32(player + field) != machine.read32(table + entry)) {
					fprintf(stderr, "VR color=%d tick=%d field=%x actual=%x expected=%x mode=%d\n",
						color, racing, field, machine.read32(player + field), machine.read32(table + entry), machine.read8(0x40d826));
					assert(false);
				}
				assert(machine.program_rom() == original_rom);
			if (racing == 120) {
				if (const char *shots = getenv("VR_COLOR_SHOTS")) {
					const std::string path = std::string(shots) + "/color-" + std::to_string(color) + ".png";
					assert(write_png(path.c_str(), machine.frame().data(), machine.frame_w(), machine.frame_h()));
				}
			}
		}
		if (racing != 120) fprintf(stderr, "VR color %d: phase=%02x racing=%d\n", color, machine.work_ram()[0x1138], racing);
		assert(racing == 120);
	}
	if (native) {
		if (rec::fallback_count())
			fprintf(stderr, "VR color fallback: %06x\n", rec::first_fallback_address());
		assert(rec::fallback_count() == 0);
	}
	printf("VR car color (%s): all eight race liveries honored, with unchanged ROM, car identity and starting position\n",
		native ? "native" : "interpreter");
}

static void rom_free_play(const char *dir, bool native)
{
	model1 machine(load_regions(dir, game::id{ "vr" }));
	if (native) {
		rec::hints map;
		assert(map.read("assets/games/vr/vr_map.txt"));
		rec::attach(machine.cpu(), "vr");
		assert(rec::build_native(machine.program_rom(), map));
		machine.run_cpu = [](int n) { rec::run(n); };
	}
	auto boot = [&] {
		if (native && rec::fallback_count()) {
			fprintf(stderr, "VR free-play fallback before reboot: %06x (%zu calls)\n",
				rec::first_fallback_address(), rec::fallback_count());
			assert(rec::fallback_count() == 0);
		}
		machine.reset();
		if (native) rec::attach(machine.cpu(), "vr");
		for (int f = 0; f < 900; f++) machine.run_frame();
	};
	boot();
	const auto rom = machine.program_rom();
	assert(game::vr::coin_setting(machine) == 0);
	const uint16_t start_credit = machine.eeprom_words()[10] & 0xff;
	for (int value = 0; value <= game::vr::FREE_PLAY; value++) {
		game::vr::set_coin_setting(machine, value);
		assert(game::vr::coin_setting(machine) == value);
		assert((machine.eeprom_words()[10] & 0xff) == start_credit);
		assert(!game::vr::set_coin_setting(machine, value));
	}
	// Reproduce the saved-credit launch, then enable native free play without clearing battery RAM.
	assert(game::vr::set_coin_setting(machine, 0));
	for (int f = 0; f < 2100; f++) machine.run_frame();
	machine.m_inputs[0] = uint8_t(~0x01);
	for (int f = 0; f < 6; f++) machine.run_frame();
	machine.m_inputs[0] = 0xff;
	for (int f = 0; f < 30; f++) machine.run_frame();
	machine.m_inputs[0] = uint8_t(~0x01);
	for (int f = 0; f < 6; f++) machine.run_frame();
	machine.m_inputs[0] = 0xff;
	for (int f = 0; f < 30; f++) machine.run_frame();
	assert(machine.read16(0x40e818) > 0);
	boot();
	assert(machine.work_ram()[0x1138] == 0x10);
	const auto battery = machine.nvram();
	assert(game::vr::set_coin_setting(machine, game::vr::FREE_PLAY));
	assert(machine.nvram() == battery);
	boot();
	assert(machine.nvram()[0xdd20] == 1);
	assert(machine.work_ram()[0x1138] != 0x10);
	for (int f = 0; f < 600; f++) {
		machine.run_frame();
		assert(machine.work_ram()[0x1138] != 0x10);
	}
	machine.m_inputs[0] = uint8_t(~0x10);
	for (int f = 0; f < 6; f++) machine.run_frame();
	machine.m_inputs[0] = 0xff;
	for (int f = 0; f < 30; f++) machine.run_frame();
	assert(machine.work_ram()[0x1138] == 0x10);
	assert(game::vr::set_coin_setting(machine, 0));
	boot();
	assert(machine.nvram()[0xdd20] == 0 && game::vr::coin_setting(machine) == 0);
	assert(machine.program_rom() == rom);
	if (native) {
		if (rec::fallback_count())
			fprintf(stderr, "VR free-play fallback: %06x (%zu calls)\n", rec::first_fallback_address(), rec::fallback_count());
		assert(rec::fallback_count() == 0);
	}
	printf("VR free play (%s): saved credits do not auto-start, Start selects a course, arcade coinage restored\n",
		native ? "native" : "interpreter");
}

static void rom_qualifying(const char *dir, bool native)
{
	model1 machine(load_regions(dir, game::id{ "vr" }));
	const auto rom = machine.program_rom();
	if (native) {
		rec::hints map;
		assert(map.read("assets/games/vr/vr_map.txt"));
		rec::attach(machine.cpu(), "vr");
		assert(rec::build_native(machine.program_rom(), map));
		machine.run_cpu = [](int n) { rec::run(n); };
	}
	machine.reset();
	auto play = game::make_auto_play(game::id{ "vr" }, "qualifying", 12345, true);
	uint32_t record = 0, time = 0, ranking_slot = 0;
	for (int f = 1; f <= play->min_frames(); f++) {
		const auto inputs = play->frame(f, machine);
		for (size_t c = 0; c < inputs.analog.size(); c++)
			if (inputs.analog[c] >= 0) assert(machine.set_analog_input(c, uint8_t(inputs.analog[c])));
		for (size_t c = 0; c < inputs.pressed.size(); c++)
			machine.m_inputs[c] = uint8_t(~inputs.pressed[c]);
		machine.run_frame();
		const auto bad = play->check(f, machine);
		if (!bad.empty()) fprintf(stderr, "%s\n", bad.c_str());
		assert(bad.empty());
		if (machine.work_ram()[0x1138] == 0x1e && machine.read32(0x501910) == 3) {
			ranking_slot = machine.read32(0x501918);
			record = machine.read32(ranking_slot);
			time = machine.read32(record + 4);
		}
	}
	const auto bad = play->finish();
	if (!bad.empty()) fprintf(stderr, "%s\n", bad.c_str());
	assert(bad.empty());
	assert(record >= 0x400000 && record <= 0x40ffe8 && time > 0);
	assert(machine.read32(record + 0x14) == 0x004b4453);
	assert(machine.program_rom() == rom);
	if (native) {
		if (rec::fallback_count())
			fprintf(stderr, "VR qualifying fallback: %06x (%zu calls)\n", rec::first_fallback_address(), rec::fallback_count());
		assert(rec::fallback_count() == 0);
	}
	const auto battery = machine.nvram();
	const auto difficulty = machine.eeprom_words()[16];
	machine.reset();
	assert(machine.nvram() == battery);
	for (int c = 0; c < 3; c++) {
		machine.m_inputs[c] = 0xff;
		assert(machine.set_analog_input(c, c ? 0x30 : 0x80));
	}
	if (native) rec::attach(machine.cpu(), "vr");
	for (int f = 0; f < 1000; f++) machine.run_frame();
	assert(machine.read32(ranking_slot) == record && machine.eeprom_words()[16] == difficulty);
	assert(machine.read32(record + 4) == time && machine.read32(record + 0x14) == 0x004b4453);
	if (native) {
		if (rec::fallback_count())
			fprintf(stderr, "VR qualifying fallback: %06x (%zu calls)\n", rec::first_fallback_address(), rec::fallback_count());
		assert(rec::fallback_count() == 0);
	}
	printf("VR qualifying (%s): control-only finish, SDK and time %u saved in battery RAM and retained after reboot\n",
		native ? "native" : "interpreter", time);
}

int main(int argc, char **argv)
{
	model1::regions regions;
	regions.title = game::id{ "vr" };
	model1 machine(std::move(regions));
	const auto callbacks = game::vr::create_gameplay(machine);
	assert(callbacks.wide_scene);
	assert(!callbacks.wide_scene(0, false));
	assert(!callbacks.wide_scene(0, true));
	assert(!callbacks.wide_scene(0x00502150, false));
	assert(callbacks.wide_scene(1, false));
	assert(callbacks.wide_scene(0x5020f0, false));
	language_settings(false);
	language_settings(true);
	machine_settings(false);
	machine_settings(true);
	scripted_inputs();
	scenery_lists();
	lod_settings();
	animated_scenery_settings();
	if (argc > 2 && (std::string(argv[2]) == "--lod" || std::string(argv[2]) == "--lod-native"))
		rom_lod(argv[1], std::string(argv[2]) == "--lod-native");
	else if (argc > 2 && (std::string(argv[2]) == "--language" || std::string(argv[2]) == "--language-native"))
		rom_language(argv[1], std::string(argv[2]) == "--language-native");
	else if (argc > 2 && (std::string(argv[2]) == "--settings" || std::string(argv[2]) == "--settings-native"))
		rom_machine_settings(argv[1], std::string(argv[2]) == "--settings-native");
	else if (argc > 2 && (std::string(argv[2]) == "--free-play" || std::string(argv[2]) == "--free-play-native"))
		rom_free_play(argv[1], std::string(argv[2]) == "--free-play-native");
	else if (argc > 2 && (std::string(argv[2]) == "--car-color" || std::string(argv[2]) == "--car-color-native"))
		rom_car_color(argv[1], std::string(argv[2]) == "--car-color-native");
	else if (argc > 2 && (std::string(argv[2]) == "--cameras" || std::string(argv[2]) == "--cameras-native"))
		rom_cameras(argv[1], std::string(argv[2]) == "--cameras-native");
	else if (argc > 2 && (std::string(argv[2]) == "--monitor" || std::string(argv[2]) == "--monitor-native"))
		rom_monitor(argv[1], std::string(argv[2]) == "--monitor-native");
	else if (argc > 2 && (std::string(argv[2]) == "--qualifying" || std::string(argv[2]) == "--qualifying-native"))
		rom_qualifying(argv[1], std::string(argv[2]) == "--qualifying-native");
	else if (argc > 1) rom_draw_distance(argv[1], argc > 2 && std::string(argv[2]) == "--native");
	puts("VR gameplay tests passed");
}
