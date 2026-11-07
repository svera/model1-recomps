#include "gameplay.h"
#include "../../machine/model1.h"
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace game::wingwar {

namespace {
constexpr uint32_t NEAREST_DETAIL_DISTANCE = std::bit_cast<uint32_t>(256.0f);

uint32_t rom_word(model1 &machine, uint32_t address, uint32_t bytes = 4)
{
	if (address < 0x200000 || address > 0x300000 - bytes) {
		fprintf(stderr, "Wing War draw distance: invalid scenery ROM address %08x\n", address);
		std::abort();
	}
	return machine.read32(address);
}

std::vector<render_object> scenery_objects(model1 &machine, gameplay_state &state, uint32_t &word)
{
	const uint32_t pc = machine.cpu().pc();
	if ((word & 1) || (pc != 0x02ff79 && pc != 0x02fe99 && pc != 0x02fdf4)) return {};
	const uint32_t cursor = machine.cpu().reg(14);
	if (cursor < 0x600000 || cursor >= 0x620000) {
		fprintf(stderr, "Wing War draw distance: invalid scenery display-list cursor %08x\n", cursor);
		std::abort();
	}
	auto &list = state.scenery[(cursor - 0x600000) >> 16];
	if (pc != 0x02fdf4) {
		if (!list.started) {
			list.started = true;
			list.extended = state.extended_draw_distance;
		}
		if (!list.extended) return {};
		const uint32_t node = machine.cpu().reg(21);
		if (node < 0x40c106 || node > 0x40f000) {
			fprintf(stderr, "Wing War draw distance: invalid scenery node %08x\n", node);
			std::abort();
		}
		const uint32_t record = machine.read32(node - 6);
		rom_word(machine, record, 0x1c);
		list.submitted.insert(record);
		if (pc == 0x02ff79 && !list.matrix) {
			list.matrix = word;
			list.anchor = record;
		}
		return {};
	}
	if (!list.matrix) {
		list = {};
		return {};
	}
	word = *list.matrix;
	std::vector<render_object> objects;
	if (list.extended) {
		const uint32_t variables = machine.cpu().reg(25);
		const uint32_t rows = machine.read32(variables + 0x734c) - 128 * 4;
		const uint32_t counts = machine.read32(variables + 0x7350) - 128;
		const uint32_t models = machine.read32(variables + 0x7354);
		rom_word(machine, rows, 256 * 4);
		rom_word(machine, counts, 256);
		auto number = [&](uint32_t address) { return std::bit_cast<float>(rom_word(machine, address)); };
		const std::array<float, 3> anchor{ number(list.anchor), number(list.anchor + 4), number(list.anchor + 8) };
		for (uint32_t row = 0; row < 256; row++) {
			const uint32_t count = machine.read8(counts + row);
			const uint32_t first = rom_word(machine, rows + row * 4);
			if (!count) continue;
			rom_word(machine, first, count * 0x1c);
			for (uint32_t i = 0; i < count; i++) {
				const uint32_t record = first + i * 0x1c;
				if (list.submitted.contains(record)) continue;
				const uint32_t type = rom_word(machine, record + 0x10);
				if (type >= 256) {
					fprintf(stderr, "Wing War draw distance: invalid scenery type %08x\n", type);
					std::abort();
				}
				// Tier one is also the model selected by LOD Off; tier zero runs compound handlers.
				const uint32_t model = rom_word(machine, models + type * 16 + 4);
				rom_word(machine, model, 4);
				const uint32_t entry = 0x271484 + uint32_t(machine.read16(model + 2)) * 16;
				rom_word(machine, entry, 12);
				std::array<float, 12> transform{ 1, 0, 0, 0, 1, 0, 0, 0, 1 };
				for (int axis = 0; axis < 3; axis++) {
					transform[9 + axis] = number(record + axis * 4) - anchor[axis];
					if (!std::isfinite(transform[9 + axis])) {
						fprintf(stderr, "Wing War draw distance: non-finite scenery position %08x\n", record);
						std::abort();
					}
				}
				objects.push_back({ rom_word(machine, entry), rom_word(machine, entry + 4),
					rom_word(machine, entry + 8), record, transform });
				objects.back().key = record;
			}
		}
	}
	list = {};
	return objects;
}
}

uint32_t lod_result(bool arcade_lod, uint32_t pc, uint32_t value)
{
	// The V60 converts this distance to 256, then shifts by eight to select model tier 1.
	return !arcade_lod && pc == LOD_RESULT_READ ? NEAREST_DETAIL_DISTANCE : value;
}

gameplay_callbacks create_gameplay(model1 &machine)
{
	gameplay_callbacks callbacks;
	auto state = std::make_shared<gameplay_state>();
	callbacks.state = state;
	callbacks.reset = [state] { state->scenery = {}; };
	callbacks.matrix_objects = [&machine, state](uint32_t &word) { return scenery_objects(machine, *state, word); };
	callbacks.copro_result = [state](uint32_t pc, uint32_t value) {
		return lod_result(state->lod, pc, value);
	};
	return callbacks;
}

void set_lod(model1 &machine, bool enabled)
{
	std::static_pointer_cast<gameplay_state>(machine.game_state())->lod = enabled;
}

void set_extended_draw_distance(model1 &machine, bool enabled)
{
	std::static_pointer_cast<gameplay_state>(machine.game_state())->extended_draw_distance = enabled;
}

}
