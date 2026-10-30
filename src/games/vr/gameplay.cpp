#include "gameplay.h"
#include "../settings.h"
#include "../../machine/model1.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <utility>
#include <glm/glm.hpp>

namespace game::vr {

namespace {
constexpr uint32_t TRACK_SELECT_CAMERA = 0x00502150;
constexpr uint32_t SCENERY_COUNT = 0x1cc0;
constexpr uint32_t SCENERY_READY_PC = 0xff7dc5;
// Car 0's rank index (record 0x400d00 + 0x123). The boot code clears battery RAM and the first
// attract race (the medium course) leaves it alone; the next attract race sets it to 15 and every
// later medium-course race starts from that. SEGA AGES Virtua Racing shows that race from the start.
constexpr uint32_t CAR0_RANK = 0xe23;
constexpr uint8_t CAR0_ATTRACT_RANK = 15;

void prepare_scenery(model1 &machine, gameplay_state &state)
{
	const auto &ram = machine.work_ram();
	const int count = ram[SCENERY_COUNT];
	if (count > 25) {
		fprintf(stderr, "VR draw distance: invalid scenery count %d\n", count);
		std::abort();
	}
	state.scenery_selected.fill(false);
	for (int i = 0; i < count; i++) {
		const int cell = ram[SCENERY_COUNT + 1 + i];
		if (state.scenery_selected[cell]) {
			fprintf(stderr, "VR draw distance: duplicate scenery cell %d\n", cell);
			std::abort();
		}
		state.scenery_selected[cell] = true;
	}
	state.scenery_pending = true;
}

std::vector<render_object> scenery_objects(model1 &machine, gameplay_state &state)
{
	if (!state.extended_draw_distance || !state.scenery_pending || machine.cpu().pc() != 0xff8500) return {};
	v60_bus &bus = machine;
	if (bus.read32(machine.cpu().reg(31)) != 0xff846f) return {};
	state.scenery_pending = false;
	std::vector<render_object> objects;
	const uint32_t table = bus.read32(0x501948);
	auto read_rom = [&](uint32_t address, uint32_t bytes) {
		if (address > 0x1000000 - bytes ||
			!((address < 0x300000 && address + bytes <= 0x300000) || address >= 0xf80000)) {
			fprintf(stderr, "VR draw distance: invalid scenery ROM address %08x\n", address);
			std::abort();
		}
		return bus.read32(address);
	};
	read_rom(table, 256 * 4);
	auto visit_cell = [&](int cell, auto visit) {
		uint32_t record = read_rom(table + uint32_t(cell) * 4, 4);
		if (!record || record == 0xffffffff) return;
		for (int guard = 0; ; guard++, record += 12) {
			if (guard == 4096) {
				fprintf(stderr, "VR draw distance: unterminated scenery cell %d\n", cell);
				std::abort();
			}
			const uint32_t polygon = read_rom(record, 4);
			if (!polygon) break;
			read_rom(record, 12);
			if (polygon >= machine.polygon_rom().size() / 4) {
				fprintf(stderr, "VR draw distance: invalid scenery polygon %08x in cell %d\n", polygon, cell);
				std::abort();
			}
			visit(record, polygon);
		}
	};
	// FF84A2 tests the cumulative budget before each cell, not each object.
	// Selected cells after the budget is exhausted still need host-only geometry.
	std::array<bool, 256> submitted{};
	uint32_t consumed = bus.read32(0x5011a0);
	const uint32_t budget = bus.read32(0x5011b0);
	const bool unlimited = !state.lod;
	const auto &ram = machine.work_ram();
	for (int i = 0; i < ram[SCENERY_COUNT]; i++) {
		const int cell = ram[SCENERY_COUNT + 1 + i];
		if (!unlimited && int32_t(consumed) > int32_t(budget)) continue;
		submitted[cell] = true;
		visit_cell(cell, [&](uint32_t record, uint32_t) { consumed += bus.read32(record + 8); });
	}
	std::array<std::pair<int, uint8_t>, 256> cells;
	const int center = int(machine.cpu().reg(22));
	if (center < 0 || center > 255) {
		fprintf(stderr, "VR draw distance: invalid anchor cell %d\n", center);
		std::abort();
	}
	for (int cell = 0; cell < 256; cell++) {
		const int dx = (cell & 15) - (center & 15), dy = (cell >> 4) - (center >> 4);
		cells[cell] = { dx * dx + dy * dy, uint8_t(cell) };
	}
	std::sort(cells.begin(), cells.end());
	for (const auto &[distance, cell] : cells) {
		if (submitted[cell]) continue;
		visit_cell(cell, [&](uint32_t record, uint32_t polygon) {
			objects.push_back({ bus.read32(record + 4), polygon, 0 });
		});
	}
	// The breakable boards' visibility gate also freezes their physics. Draw missing boards
	// from their saved poses instead of running that handler outside the arcade mask.
	const uint32_t end = machine.read32(0x501368);
	if (end && (end < 0x407900 || end > 0x410000 || (end - 0x407900) % 0x80)) {
		fprintf(stderr, "VR draw distance: invalid scenery records end %06x\n", end);
		std::abort();
	}
	for (uint32_t record = 0x407900; record < end; record += 0x80) {
		if (machine.read32(record + 0xa) != 0xffcc7d) continue;
		const uint32_t cell = machine.read16(record + 0x34);
		if (cell >= 256) {
			fprintf(stderr, "VR draw distance: invalid breakable board cell %u\n", cell);
			std::abort();
		}
		if (machine.read32(0x501ac0 + cell / 32 * 4) & (1u << (cell % 32))) continue;
		auto number = [&](uint32_t offset) { return std::bit_cast<float>(machine.read32(record + offset)); };
		glm::mat3 rotation(1);
		for (const auto &[offset, first, second] : std::array<std::array<int, 3>, 3>{ {
			{ 0x2c, 2, 0 }, { 0x2a, 1, 2 }, { 0x2e, 0, 1 }
		} }) {
			const float angle = int16_t(machine.read16(record + offset)) * (2.0f * 3.14159265358979323846f / 65536.0f);
			const float s = std::sin(angle), c = std::cos(angle);
			const glm::vec3 a = rotation[first], b = rotation[second];
			rotation[first] = c * a - s * b;
			rotation[second] = s * a + c * b;
		}
		const glm::vec3 position = glm::vec3(number(0x1e), number(0x22) + number(0x5e), number(0x26)) +
			rotation * glm::vec3(0, number(0x67), 0);
		std::array<float, 12> transform;
		for (int c = 0; c < 3; c++)
			for (int r = 0; r < 3; r++) transform[c * 3 + r] = rotation[c][r];
		for (int r = 0; r < 3; r++) transform[9 + r] = position[r];
		const uint32_t list = machine.read32(record + 0x4c);
		objects.push_back({ read_rom(list + 4, 4), read_rom(list, 12), 0, record, transform });
	}
	return objects;
}
}

gameplay_callbacks create_gameplay(model1 &machine)
{
	gameplay_callbacks callbacks;
	auto state = std::make_shared<gameplay_state>();
	callbacks.state = state;
	callbacks.reset = [state] {
		state->scenery_pending = false;
		state->attract_rank_pending = true;
	};
	callbacks.frame_end = [&machine, state] {
		if (state->attract_rank_pending) {
			state->attract_rank_pending = false;
			if (machine.nvram()[CAR0_RANK] == 0) machine.nvram()[CAR0_RANK] = CAR0_ATTRACT_RANK;
		}
		const uint8_t phase = machine.work_ram()[0x1138];
		if (!state->standalone_car_color || (phase != 0x12 && phase != 0x14) ||
			machine.read8(0x40dc8c) != 0) return;
		const uint32_t color = machine.read8(0x40dca2);
		if (!color) return;   // Preserve the original standalone red model.
		const uint32_t player = machine.read32(0x501320);
		if (color >= 8 || player < 0x400d00 || player > 0x403280 || (player - 0x400d00) % 0x280) {
			fprintf(stderr, "VR: invalid standalone livery color %u or player record %06x\n", color, player);
			std::abort();
		}
		// FE8A91 uses fixed standalone models. Copy only the five rendering pointers from the
		// arcade's colored-model table (FE8DE9); never change car IDs, track or handling fields.
		const uint32_t table = 0xfcd195 + color * 32;
		for (const auto &[field, entry] : std::array<std::pair<uint32_t, uint32_t>, 5>{ {
			{ 0x18, 4 }, { 0xac, 0 }, { 0xa2, 8 }, { 0xa8, 12 }, { 0xb0, 16 }
		} })
			machine.write32(player + field, machine.read32(table + entry));
	};
	callbacks.work_ram_watch = 0x500000 + SCENERY_COUNT;
	callbacks.work_ram_written = [&machine, state](uint16_t mask) {
		if ((mask & 0xff) && machine.cpu().pc() == SCENERY_READY_PC)
			prepare_scenery(machine, *state);
	};
	callbacks.matrix_objects = [&machine, state](uint32_t &) { return scenery_objects(machine, *state); };
	callbacks.copro_result = [&machine, state](uint32_t pc, uint32_t value) {
		// Full-distance props must retain the arcade's collision visibility.
		if (state->extended_draw_distance &&
			(pc == 0xffb19d || pc == 0xffb1bd || pc == 0xffb797 || pc == 0xffaaed ||
			 pc == 0xffb6a7 || pc == 0xffdc94)) {
			const uint32_t cell = machine.read16(machine.cpu().reg(25) + 0x34);
			if (cell >= 256) {
				fprintf(stderr, "VR draw distance: invalid animated scenery cell %u\n", cell);
				std::abort();
			}
			if (!(machine.read32(0x501ac0 + cell / 32 * 4) & (1u << (cell % 32))))
				return std::bit_cast<uint32_t>(10000.0f);
		}
		return std::bit_cast<uint32_t>(lod_distance(state->lod, pc, std::bit_cast<float>(value)));
	};
	callbacks.read32_watch = 0x5011b0;
	callbacks.read32_watch_end = 0x501adc;
	callbacks.read32_value = [&machine, state](uint32_t pc, uint32_t address, uint32_t value) {
		// More detailed cars must not exhaust the arcade's submission budget.
		if (address == 0x5011b0 && (pc == 0xff848a || pc == 0xff84a2)) {
			if (!state->lod) return 0x7fffffffu;
			if (state->extended_draw_distance) {
				const uint32_t caller = machine.read32(machine.cpu().reg(31));
				if (caller == 0xffd809 || caller == 0xffd85a || caller == 0xffd9e8 ||
					caller == 0xffda21 || caller == 0xffda67 || caller == 0xffdb49 ||
					caller == 0xffdb9a || caller == 0xffdbd5 || caller == 0xffb254 ||
					caller == 0xffb81c || caller == 0xffb859 || caller == 0xffc235 ||
					caller == 0xffc251 || caller == 0xffab97 || caller == 0xffb72c ||
					caller == 0xffddd4 || caller == 0xffddeb || caller == 0xffde6e ||
					caller == 0xffdedc || caller == 0xffdf42 || caller == 0xffd19c) return 0x7fffffffu;
			}
		}
		// Render-only scenery and flags use the narrow mask; collidable props use the wider one.
		if (state->extended_draw_distance && address >= 0x501aa0 && address <= 0x501abc &&
			(pc == 0xffd77a || pc == 0xffd928 || pc == 0xffdaba || pc == 0xffc1b2)) return 0xffffffffu;
		if (state->extended_draw_distance && address >= 0x501ac0 && address <= 0x501adc &&
			(pc == 0xffb0eb || pc == 0xffb769 || pc == 0xffaac4 || pc == 0xffb679 ||
			 pc == 0xffdc4d || pc == 0xffd0e2)) return 0xffffffffu;
		return value;
	};
	callbacks.object = [&machine, state](uint32_t owner, uint32_t, uint32_t polygon, uint32_t, const float *) {
		object_effects effect;
		const uint8_t phase = machine.work_ram()[0x1138];
		if (!state->standalone_car_color || (phase != 0x12 && phase != 0x14) ||
			machine.read8(0x40dc8c) || owner != machine.read32(0x501320)) return effect;
		const uint32_t color = machine.read8(0x40dca2);
		if (!color) return effect;
		if (color >= 8) {
			fprintf(stderr, "VR: invalid standalone livery color %u\n", color);
			std::abort();
		}
		// Collision and recovery use standalone red models rather than the livery pointers.
		// Keep their geometry, but use the selected B0 body's paint material (texture slot 1).
		bool standalone = polygon == machine.read32(machine.read32(0xfcd395 + 4));
		for (int i = 0; !standalone && i < 16; i++)
			standalone = polygon == machine.read32(machine.read32(0xfd377a + i * 4));
		for (int i = 0; !standalone && i < 3; i++)
			standalone = polygon == machine.read32(machine.read32(0xfd4a39 + i * 4));
		if (!standalone) return effect;
		const uint32_t body = machine.read32(0xfcd195 + color * 32 + 16);
		const uint32_t texture = machine.read32(body + 4);
		const uint16_t paint = machine.texture_word(texture - 0x40000 + 1) & 0x3ff;
		effect.material = [paint](uint16_t material) {
			const uint16_t pen = material & 0x3ff;
			if (pen != 0x02 && pen != 0xee && pen != 0xf6) return material;
			return uint16_t((material & 0xfc00) | paint);
		};
		return effect;
	};
	callbacks.record_visibility = [&machine](uint32_t owner) {
		std::vector<game::record_visibility> records;
		if (owner < 0x400d00 || owner > 0x401e80 || (owner - 0x400d00) % 0x280)
			return records;
		const auto &ram = machine.work_ram();
		if (ram[0x1138] != 0x03 && ram[0x1138] != 0x14) return records;
		for (uint32_t car = 0x400d00; car <= 0x401e80; car += 0x280) {
			if (!(machine.read32(car) & 0x80000000)) continue;
			const uint32_t cell = machine.read16(car + 0x34);
			if (cell >= 256) {
				fprintf(stderr, "VR car visibility: invalid course cell %u for record %06x\n", cell, car);
				std::abort();
			}
			game::record_visibility result{};
			result.owner = car;
			for (int i = 0; i < 3; i++) {
				const uint32_t bits = machine.read32(car + 0x1e + i * 4);
				memcpy(&result.position[i], &bits, sizeof bits);
				if (!std::isfinite(result.position[i])) {
					fprintf(stderr, "VR car visibility: non-finite position for record %06x\n", car);
					std::abort();
				}
			}
			result.culled = !(machine.read32(0x501ac0 + (cell / 32) * 4) & (1u << (cell % 32)));
			records.push_back(result);
		}
		return records;
	};
	callbacks.wide_scene = [](uint32_t camera, bool) {
		return camera != 0 && camera != TRACK_SELECT_CAMERA;
	};
	return callbacks;
}

void set_extended_draw_distance(model1 &machine, bool extended)
{
	auto state = std::static_pointer_cast<gameplay_state>(machine.game_state());
	state->extended_draw_distance = extended;
	state->scenery_pending = false;
}

void set_lod(model1 &machine, bool enabled)
{
	std::static_pointer_cast<gameplay_state>(machine.game_state())->lod = enabled;
}

float lod_distance(bool enabled, uint32_t caller, float distance)
{
	// Only the two render-only car detail tests, never collision/proximity queries.
	if (!enabled && (caller == 0xfefbf5 || caller == 0xff0d3b)) return -1.0f;
	return distance;
}

namespace {
struct setting_field { int byte, count; };
constexpr std::array<setting_field, 4> setting_fields{ {
	{ 0x22, 8 }, { 0x23, 2 }, { 0x21, 4 }, { 0x20, 4 }
} };

const setting_field *field_for(machine_setting setting)
{
	const auto index = size_t(setting);
	if (index < setting_fields.size()) return &setting_fields[index];
	fprintf(stderr, "VR: invalid machine setting %zu\n", index);
	return nullptr;
}
}

int machine_setting_value(model1 &machine, machine_setting setting)
{
	const auto *field = field_for(setting);
	if (!field) return -1;
	int value = -1;
	edit_eeprom_settings(machine, 5, [&](uint16_t *e) {
		const int shift = field->byte % 2 ? 0 : 8;
		const int stored = (e[field->byte / 2] >> shift) & 0xff;
		if (stored < field->count) value = stored;
		else fprintf(stderr, "VR: invalid machine setting at EEPROM byte %02x: %d\n", field->byte, stored);
		return false;
	});
	return value;
}

bool set_machine_setting(model1 &machine, machine_setting setting, int value)
{
	const auto *field = field_for(setting);
	if (!field) return false;
	if (value < 0 || value >= field->count) {
		fprintf(stderr, "VR: invalid value %d for machine setting %zu\n", value, size_t(setting));
		return false;
	}
	return edit_eeprom_settings(machine, 5, [&](uint16_t *e) {
		const int shift = field->byte % 2 ? 0 : 8;
		auto &word = e[field->byte / 2];
		const uint16_t updated = uint16_t((word & ~(0xff << shift)) | (value << shift));
		if (word == updated) return false;
		word = updated;
		return true;
	});
}

bool set_link_player(model1 &machine, int player)
{
	if (player < 1 || player > 2) {
		fprintf(stderr, "VR: link player must be 1 (master) or 2 (slave)\n");
		return false;
	}
	return edit_eeprom_settings(machine, 5, [&](uint16_t *e) {
		const uint16_t value = uint16_t((e[6] & 0x00ff) | (player << 8));
		if (e[6] == value) return false;
		e[6] = value;
		return true;
	});
}

bool set_standalone(model1 &machine)
{
	return edit_eeprom_settings(machine, 5, [&](uint16_t *e) {
		if (!(e[6] & 0xff00)) return false;
		e[6] &= 0x00ff;
		return true;
	});
}

bool in_attract_mode(model1 &machine)
{
	const auto phase = machine.work_ram()[0x1138];
	return machine.past_boot() && !machine.in_test_menu_mode() && (phase == 0x03 || phase == 0x07);
}

void camera_cycle::update(model1 &machine, bool pressed)
{
	const uint8_t phase = machine.work_ram()[0x1138];
	if (phase != 0x12 && phase != 0x14) {
		m_pressed = pressed;
		m_frames = 0;
		return;
	}
	if (pressed && !m_pressed) {
		// FF1482 stores the actual camera index. Cycle from it, including after a race reset.
		constexpr std::array<int, 4> next{ 3, 0, 1, 2 };   // Red -> Blue -> Yellow -> Green.
		m_view = next[machine.read8(0x501d02) & 3];
		m_frames = 6;
	}
	m_pressed = pressed;
	if (!m_frames) return;
	// FF1441: indices 0/1/2/3 are Blue/Red/Green/Yellow.
	constexpr std::array<uint8_t, 4> system{ 0x40, 0x20, 0, 0x80 };
	machine.m_inputs[0] &= uint8_t(~system[m_view]);
	if (m_view == 2) machine.m_inputs[1] &= uint8_t(~0x01);
	m_frames--;
}

bool enable_standalone_car_color(model1 &machine)
{
	if (machine.program_rom().size() < 0xfcd295) {
		fprintf(stderr, "VR: cannot enable standalone car color: missing colored-model table\n");
		return false;
	}
	std::static_pointer_cast<gameplay_state>(machine.game_state())->standalone_car_color = true;
	return true;
}

int monitor_type(model1 &machine)
{
	int value = -1;
	edit_eeprom_settings(machine, 5, [&](uint16_t *e) {
		const int stored = e[5] & 0xff;
		if (stored <= 1) value = stored;
		else fprintf(stderr, "VR: invalid EEPROM monitor type %d; expected 4:3 or 16:9\n", stored);
		return false;
	});
	return value;
}

bool set_monitor_type(model1 &machine, int value)
{
	if (value < 0 || value > 1) {
		fprintf(stderr, "VR: invalid monitor type %d; expected 0 (4:3) or 1 (16:9)\n", value);
		return false;
	}
	return edit_eeprom_settings(machine, 5, [&](uint16_t *e) {
		const uint16_t updated = uint16_t((e[5] & 0xff00) | value);
		if (e[5] == updated) return false;
		e[5] = updated;
		return true;
	});
}

int coin_setting(model1 &machine)
{
	int value = -1;
	edit_eeprom_settings(machine, 5, [&](uint16_t *e) {
		const int stored = e[8] >> 8;
		if (stored <= FREE_PLAY) value = stored;
		else fprintf(stderr, "VR: invalid EEPROM coin setting %d\n", stored);
		return false;
	});
	return value;
}

bool set_coin_setting(model1 &machine, int value)
{
	if (value < 0 || value > FREE_PLAY) {
		fprintf(stderr, "VR: invalid coin setting %d; expected 0 through %d\n", value, FREE_PLAY);
		return false;
	}
	// FC68EA derives the credit ratios and free-play flag from the game's coinage table.
	const auto &rom = machine.program_rom();
	const size_t at = 0xfc6924 + size_t(value) * 4;
	if (at + 4 > rom.size()) {
		fprintf(stderr, "VR: coinage table is missing from the program ROM\n");
		return false;
	}
	return edit_eeprom_settings(machine, 5, [&](uint16_t *e) {
		const uint16_t coin = uint16_t(value << 8 | rom[at]);
		const uint16_t ratios = uint16_t(rom[at + 2] << 8 | rom[at + 3]);
		const uint16_t mode = uint16_t(rom[at + 1] << 8 | (e[10] & 0xff));
		if (e[8] == coin && e[9] == ratios && e[10] == mode) return false;
		e[8] = coin;
		e[9] = ratios;
		e[10] = mode;
		return true;
	});
}

int language_value(model1 &machine)
{
	int value = -1;
	edit_eeprom_settings(machine, 5, [&](uint16_t *e) {
		const int country = e[13] & 0xff;
		if (country <= 2) value = country != 0;
		else fprintf(stderr, "VR: invalid EEPROM country %d; expected Japan, U.S. or Export\n", country);
		return false;
	});
	return value;
}

bool set_language(model1 &machine, int language)
{
	if (language < 0 || language > 1) {
		fprintf(stderr, "VR: invalid language %d; expected 0 (Japanese) or 1 (English)\n", language);
		return false;
	}
	return edit_eeprom_settings(machine, 5, [&](uint16_t *e) {
		const int country = e[13] & 0xff;
		if (country <= 2 && int(country != 0) == language) return false;
		e[13] = uint16_t((e[13] & 0xff00) | (language ? 2 : 0));
		return true;
	});
}

}
