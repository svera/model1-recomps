#include "../../../src/games/wingwar/gameplay.h"
#include "../../../src/games/wingwar/profile.h"
#include "../../../src/host/regions.h"
#include <algorithm>
#include <bit>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <map>
#include <glm/gtc/matrix_inverse.hpp>

struct model1_video_test
{
	static glm::mat4 matrix(const float *values)
	{
		glm::mat4 result(1);
		for (int i = 0; i < 9; i++) result[i / 3][i % 3] = values[i];
		for (int i = 0; i < 3; i++) result[3][i] = values[9 + i];
		return result;
	}

	static void identities(model1 &m)
	{
		model1::logic_frame a, b;
		a.has_camera = b.has_camera = true;
		a.frame = 10;
		b.frame = 12;
		a.camera = b.camera = { 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0 };
		b.camera[9] = -2;
		auto part = [](uint32_t owner, float x) {
			model1::slot s;
			s.owner = owner;
			s.polys = { 0x1234 };
			s.mat[0] = s.mat[4] = s.mat[8] = 1;
			s.mat[9] = x;
			s.mat[11] = 100;
			return s;
		};
		a.slots = { part(0x401d00, 10), part(0x401d80, 30), part(1, 50) };
		b.slots = { part(0x401d80, 32), part(0x401d00, 12), part(1, 48), part(1, 98) };
		m.blend_slots(a, b, .5);
		assert(!m.m_blend_cut);
		assert(m.m_blend_ok[0] && m.m_blend_ok[1] && m.m_blend_ok[3]);
		assert(m.m_blend_from[0][9] == 30 && m.m_blend[0][9] == 31);
		assert(m.m_blend_from[1][9] == 10 && m.m_blend[1][9] == 11);
		assert(m.m_blend_from[3][9] == 100 && m.m_blend[3][9] == 99);
		// An attract fly-by moves the camera up to 12 units a game frame: still one shot.
		b.camera[9] = -12;
		m.blend_slots(a, b, .5);
		assert(!m.m_blend_cut);
		// A camera jump must not morph between unrelated attract shots.
		b.camera[9] = -100;
		m.blend_slots(a, b, .5);
		assert(m.m_blend_cut);
		assert(std::none_of(m.m_blend_ok.begin(), m.m_blend_ok.end(), [](bool value) { return value; }));
	}

	static void attract(model1 &m, model1 &arcade)
	{
		std::array<std::map<uint32_t, uint32_t>, 2> records;
		const auto original = m.m_gameplay.matrix_objects;
		size_t camera_checks = 0, world_checks = 0, moving_checks = 0, copy_checks = 0, fallback = 0, cuts = 0;
		m.m_gameplay.matrix_objects = [&](uint32_t &word) {
			const uint32_t pc = m.cpu().pc();
			if (!(word & 1) && pc == 0x02ff79) {
				const uint32_t at = m.cpu().reg(14);
				records[(at - 0x600000) >> 16][word] = m.read32(m.cpu().reg(21) - 6);
			}
			auto extra = original(word);
			if (pc == 0x02fdf4 && !(word & 1)) {
				const int buffer = int((m.cpu().reg(14) - 0x600000) >> 16);
				const auto state = std::static_pointer_cast<game::wingwar::gameplay_state>(m.game_state());
				const auto &camera = state->cameras[buffer].matrix;
				if (camera && !records[buffer].empty()) {
					for (const auto &[slot, record] : records[buffer]) {
						const uint32_t address = 0x600000 + uint32_t(buffer) * 0x10000 + slot * 2;
						assert(m.read32(address) == 0xb);
						std::array<float, 12> pose;
						for (int i = 0; i < 12; i++) pose[i] = std::bit_cast<float>(m.read32(address + 4 + i * 4));
						glm::vec4 position(0, 0, 0, 1);
						for (int i = 0; i < 3; i++) position[i] = std::bit_cast<float>(m.read32(record + i * 4));
						const glm::vec4 expected = matrix(camera->data()) * position;
						for (int i = 0; i < 3; i++) assert(std::abs(expected[i] - pose[9 + i]) < 0.02f);
						camera_checks++;
					}
				}
				records[buffer].clear();
			}
			return extra;
		};
		for (int frame = 0; frame < 3200; frame++) {
			m.run_frame();
			arcade.run_frame();
			assert(m.work_ram() == arcade.work_ram() && m.nvram() == arcade.nvram());
			assert(m.cpu().pc() == arcade.cpu().pc());
			for (int r = 0; r < 32; r++) assert(m.cpu().reg(r) == arcade.cpu().reg(r));
			assert(m.tgp_cycles_executed() == arcade.tgp_cycles_executed());
			assert(m.m_display_list0_ram == arcade.m_display_list0_ram);
			assert(m.m_display_list1_ram == arcade.m_display_list1_ram);
			const auto &a = m.m_logic[0], &b = m.m_logic[1];
			if (b.frame != frame || a.list.empty()) continue;
			if (!a.has_camera || !b.has_camera) {
				fallback++;
				continue;
			}
			m.blend_slots(a, b, .5);
			if (m.m_blend_cut) { cuts++; continue; }
			for (size_t i = 0; i < b.slots.size(); i++) {
				const auto &part = b.slots[i];
				if (!m.m_blend_ok[i] || part.polys.empty()) continue;
				for (float value : m.m_blend[i]) assert(std::isfinite(value));
				if (part.owner == 1) world_checks++;
				for (const auto &object : part.extra) {
					assert(part.owner == 1 && object.transform);
					glm::vec4 source(0, 0, 0, 1), local(0, 0, 0, 1);
					for (int axis = 0; axis < 3; axis++) {
						source[axis] = std::bit_cast<float>(m.read32(object.owner + axis * 4));
						local[axis] = (*object.transform)[9 + axis];
					}
					const glm::vec4 expected = matrix(a.camera.data()) * source;
					const glm::vec4 from = matrix(m.m_blend_from[i].data()) * local;
					for (int axis = 0; axis < 3; axis++) assert(std::abs(from[axis] - expected[axis]) < 0.02f);
					copy_checks++;
				}
				if (part.owner <= 1) continue;
				bool same_record = false;
				for (const auto &older : a.slots)
					if (older.owner == part.owner &&
						std::equal(m.m_blend_from[i].begin(), m.m_blend_from[i].end(), older.mat))
						same_record = true;
				// An unpaired part can inherit its record's motion when that record changes parts.
				const auto old_count = std::count_if(a.slots.begin(), a.slots.end(),
					[&](const auto &s) { return s.owner == part.owner && !s.polys.empty(); });
				const auto new_count = std::count_if(b.slots.begin(), b.slots.end(),
					[&](const auto &s) { return s.owner == part.owner && !s.polys.empty(); });
				const bool current = std::equal(m.m_blend_from[i].begin(), m.m_blend_from[i].end(), part.mat);
				if (old_count == new_count) assert(same_record || current);
				moving_checks++;
			}
		}
		printf("Wing War interpolation: %zu camera poses, %zu world parts, %zu record parts, %zu copies; %zu cuts, %zu fallback pairs; game state unchanged\n",
			camera_checks, world_checks, moving_checks, copy_checks, cuts, fallback);
		assert(camera_checks > 10000 && world_checks > 10000 && moving_checks > 100);
		assert(cuts > 0 && fallback > 0);
		if (std::static_pointer_cast<game::wingwar::gameplay_state>(m.game_state())->extended_draw_distance)
			assert(copy_checks > 10000);
		m.m_gameplay.matrix_objects = original;
		m.reset();
		for (int buffer = 0; buffer < 2; buffer++) {
			assert(!m.m_list_has_camera[buffer]);
			assert(!std::static_pointer_cast<game::wingwar::gameplay_state>(m.game_state())->cameras[buffer].matrix);
		}
	}
};

int main(int argc, char **argv)
{
	if (argc != 2) {
		fprintf(stderr, "usage: %s regions-dir (requires wingwar.zip)\n", argv[0]);
		return 2;
	}
	for (bool full : { false, true }) {
		model1 m(load_regions(argv[1], game::wingwar::ID));
		model1 arcade(load_regions(argv[1], game::wingwar::ID));
		game::wingwar::set_extended_draw_distance(m, full);
		game::wingwar::set_extended_draw_distance(arcade, full);
		m.reset();
		arcade.reset();
		m.set_smooth(true);
		model1_video_test::identities(m);
		model1_video_test::attract(m, arcade);
	}
}
