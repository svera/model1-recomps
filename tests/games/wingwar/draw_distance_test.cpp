#include "../../../src/games/wingwar/gameplay.h"
#include "../../../src/games/wingwar/profile.h"
#include "../../../src/host/regions.h"
#include <bit>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>

struct model1_video_test
{
	std::array<std::map<uint32_t, uint32_t>, 2> matrices;
	std::array<std::set<uint32_t>, 2> submitted;
	std::set<uint32_t> model_tables;
	uint64_t batches = 0, objects = 0;

	void observe(model1 &machine)
	{
		const auto original = machine.m_gameplay.matrix_objects;
		machine.m_gameplay.matrix_objects = [this, &machine, original](uint32_t &word) {
			const uint32_t pc = machine.cpu().pc(), written = word;
			const bool scenery = !(word & 1) && (pc == 0x02ff79 || pc == 0x02fe99 || pc == 0x02fdf4);
			const int buffer = scenery ? int((machine.cpu().reg(14) - 0x600000) >> 16) : 0;
			if (scenery && pc != 0x02fdf4) {
				const uint32_t record = machine.read32(machine.cpu().reg(21) - 6);
				submitted[buffer].insert(record);
				if (pc == 0x02ff79) matrices[buffer][word] = record;
			}
			const auto state = std::static_pointer_cast<game::wingwar::gameplay_state>(machine.game_state());
			if (scenery && pc == 0x02fdf4 && state->scenery[buffer].extended)
				assert(state->scenery[buffer].matrix);
			auto extra = original(word);
			if (!scenery || pc != 0x02fdf4) return extra;
			if (!extra.empty()) {
				assert(word != written && matrices[buffer].contains(word));
				const uint32_t anchor = matrices[buffer].at(word);
				const auto &list = buffer ? machine.m_display_list1_ram : machine.m_display_list0_ram;
				auto number = [&](uint32_t address) { return std::bit_cast<float>(machine.read32(address)); };
				auto matrix_float = [&](uint32_t slot, int index) {
					const uint32_t at = slot + 2 + index * 2;
					return std::bit_cast<float>(uint32_t(list[at]) | uint32_t(list[at + 1]) << 16);
				};
				auto close = [](float a, float b) { return std::abs(a - b) < 0.02f + std::abs(b) * 1e-5f; };
				// Reconstruct matrices the V60 actually submitted, not a rendered-image comparison.
				for (const auto &[slot, record] : matrices[buffer]) {
					for (int i = 0; i < 9; i++) assert(close(matrix_float(slot, i), matrix_float(word, i)));
					for (int axis = 0; axis < 3; axis++) {
						float expected = matrix_float(word, 9 + axis);
						for (int k = 0; k < 3; k++)
							expected += matrix_float(word, k * 3 + axis) *
								(number(record + k * 4) - number(anchor + k * 4));
						assert(close(matrix_float(slot, 9 + axis), expected));
					}
				}
				const uint32_t models = machine.read32(machine.cpu().reg(25) + 0x7354);
				model_tables.insert(models);
				std::set<uint32_t> seen;
				for (const auto &object : extra) {
					assert(object.transform && !submitted[buffer].contains(object.owner));
					assert(seen.insert(object.owner).second && object.key == object.owner);
					const uint32_t type = machine.read32(object.owner + 0x10);
					const uint32_t model = machine.read32(models + type * 16 + 4);
					const uint32_t entry = 0x271484 + uint32_t(machine.read16(model + 2)) * 16;
					assert(object.texture == machine.read32(entry));
					assert(object.polygon == machine.read32(entry + 4));
					assert(object.size == machine.read32(entry + 8));
					assert(object.polygon < machine.polygon_rom().size() / 4 && object.size > 0);
					for (int axis = 0; axis < 3; axis++)
						assert((*object.transform)[9 + axis] == number(object.owner + axis * 4) - number(anchor + axis * 4));
				}
				const uint32_t rows = machine.read32(machine.cpu().reg(25) + 0x734c) - 128 * 4;
				const uint32_t counts = machine.read32(machine.cpu().reg(25) + 0x7350) - 128;
				size_t missing = 0;
				for (uint32_t row = 0; row < 256; row++) {
					const uint32_t first = machine.read32(rows + row * 4);
					for (uint32_t i = 0; i < machine.read8(counts + row); i++) {
						const uint32_t record = first + i * 0x1c;
						if (submitted[buffer].contains(record)) continue;
						assert(seen.contains(record));
						missing++;
					}
				}
				assert(missing == extra.size());
				batches++;
				objects += extra.size();
			}
			matrices[buffer].clear();
			submitted[buffer].clear();
			return extra;
		};
	}

	static bool has_extras(model1 &machine)
	{
		for (const auto &buffer : machine.m_list_extra) if (!buffer.empty()) return true;
		return false;
	}

	static void same_game(model1 &a, model1 &b)
	{
		assert(a.work_ram() == b.work_ram() && a.nvram() == b.nvram());
		assert(a.cpu().pc() == b.cpu().pc());
		for (int r = 0; r < 32; r++) assert(a.cpu().reg(r) == b.cpu().reg(r));
		assert(a.tgp_cycles_executed() == b.tgp_cycles_executed());
		assert(a.m_display_list0_ram == b.m_display_list0_ram && a.m_display_list1_ram == b.m_display_list1_ram);
	}
};

int main(int argc, char **argv)
{
	if (argc != 2) {
		fprintf(stderr, "usage: %s regions-dir (requires wingwar.zip)\n", argv[0]);
		return 2;
	}
	for (bool lod : { false, true }) {
		model1 arcade(load_regions(argv[1], game::wingwar::ID));
		model1 full(load_regions(argv[1], game::wingwar::ID));
		game::wingwar::set_extended_draw_distance(arcade, false);
		game::wingwar::set_lod(arcade, lod);
		game::wingwar::set_lod(full, lod);
		arcade.reset();
		full.reset();
		model1_video_test check;
		check.observe(full);
		for (int frame = 0; frame < 3200; frame++) {
			if (frame == 1600) game::wingwar::set_extended_draw_distance(full, false);
			if (frame == 1700) {
				assert(!model1_video_test::has_extras(full));
				game::wingwar::set_extended_draw_distance(full, true);
				arcade.set_smooth(true);
				full.set_smooth(true);
			}
			arcade.run_frame();
			full.run_frame();
			model1_video_test::same_game(arcade, full);
			assert(!model1_video_test::has_extras(arcade));
		}
		printf("Wing War renderer distance (LOD %s): %llu batches, %llu objects, %zu model tables; poses, submissions and game state verified\n",
			lod ? "On" : "Off", (unsigned long long)check.batches, (unsigned long long)check.objects, check.model_tables.size());
		assert(check.batches > 500 && check.objects > 10000 && check.model_tables.size() >= 2);
		full.reset();
		assert(!model1_video_test::has_extras(full));
	}
}
