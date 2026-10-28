#include "../src/machine/model1.h"
#include "../src/games/vr/gameplay.h"
#include "../src/games/swa/gameplay.h"
#include "../src/games/script.h"
#include "../src/host/regions.h"
#include "../src/rt/native.h"
#include <array>
#include <cassert>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <cstdio>
#include <map>
#include <set>

struct model1_video_test
{
	static void swa_wrap_smooth(const char *dir, bool native, bool lod)
	{
		model1 m(load_regions(dir, game::id{ "swa" }));
		m.reset();
		m.set_drawing(false);
		m.set_smooth(true);
		m.set_aspect(16.0f / 9);
		m.set_edge_aa(false);
		game::swa::set_lod(m, lod);
		game::swa::set_full_draw_distance(m, true);
		if (native) {
			rec::hints hints;
			assert(hints.read("assets/games/swa/swa_map.txt"));
			rec::attach(m.cpu(), "swa");
			assert(rec::build_native(m.program_rom(), hints));
			m.run_cpu = [](int n) { rec::run(n); };
		}
		while (m.frame_number() < 6000) m.run_frame();
		m.set_drawing(true);
		size_t render_passes = 0, hud_frames = 0;
		const auto render_begin = m.m_gameplay.render_begin;
		m.m_gameplay.render_begin = [&] {
			render_passes++;
			if (render_begin) render_begin();
		};
		for (int frame : { 6072, 6142, 6250, 6972, 7400, 8000, 9000 }) {
			while (m.frame_number() < uint64_t(frame)) m.run_frame();
			for (int scale : { 1, 3 }) {
				m.set_render_scale(scale);
				for (double fraction : { .25, .5, .75 }) {
					const double now = frame - fraction;
					model1::gpu_parts parts;
					render_passes = 0;
					const int kind = m.render_parts(now, parts, false);
					assert(kind == (m.m_above_hud_seen ? 2 : 1));
					assert(render_passes == (m.m_above_hud_seen ? 2u : 1u));
					hud_frames += m.m_above_hud_seen;
					std::vector<uint32_t> pixels;
					if (kind == 1) model1::compose_cpu(parts, m.frame_w(), m.frame_h(), pixels);
					else pixels.assign(m.frame().data(), m.frame().data() + size_t(m.frame_w()) * m.frame_h());
					m.m_redrawing = true;
					assert(m.render_smooth(now, false));
					m.m_redrawing = false;
					assert(std::equal(pixels.begin(), pixels.end(), m.frame().data()));
				}
			}
		}
		assert(hud_frames > 0);
		if (native) assert(rec::fallback_count() == 0);
		printf("SWA %s LOD %s: smooth parts match whole frames at 1x/3x; above-HUD scenes render once\n",
			native ? "native" : "interpreter", lod ? "on" : "off");
	}
	static void copy_bounds_and_sort()
	{
		model1::regions regions;
		regions.title = game::id{ "swa" };
		model1 m(std::move(regions));
		auto &view = *m.m_view;
		view.init_translation_matrix();
		view.a_left = view.a_top = -0.5f;
		view.a_right = view.a_bottom = 0.5f;
		const game::model_bounds bounds{ { 0, 0, 0 }, { 1, 2, 3 }, 4 };
		view.translation[11] = 10;
		assert(!view.outside(bounds));
		view.translation[9] = 20;
		assert(view.outside(bounds));
		view.translation[9] = 5;
		assert(!view.outside(bounds));   // intersects the right edge
		view.translation[9] = 0;
		view.translation[10] = -20;
		assert(view.outside(bounds));
		view.translation[10] = 0;
		view.translation[11] = -10;
		assert(view.outside(bounds));
		view.translation[0] = 20;
		view.translation[11] = 10;
		view.translation[9] = 20;
		assert(!view.outside(bounds));   // scaled bounds still intersect

		for (int count : { 0, 1, 255, 256, 4096, 32768 }) {
			std::vector<model1::quad_t *> expected;
			uint32_t random = 12345;
			for (int i = 0; i < count; i++) {
				auto &q = m.m_quaddb[i];
				random = random * 1664525u + 1013904223u;
				q.z = i % 2 ? std::bit_cast<float>(random & 0xff7fffffu) : float((i * 7919) % 127 - 63);
				if (i % 23 == 0) q.z = 0.0f;
				if (i % 29 == 0) q.z = -0.0f;
				q.foreground = i % 17 == 0;
				expected.push_back(&q);
			}
			m.m_quadpt = m.m_quaddb.get() + expected.size();
			std::qsort(expected.data(), expected.size(), sizeof(expected[0]), [](const void *a, const void *b) {
				return (*static_cast<model1::quad_t *const *>(a))->compare(*static_cast<model1::quad_t *const *>(b));
			});
			m.sort_quads();
			assert(std::equal(expected.begin(), expected.end(), m.m_quadind.get()));
		}
	}

	static void swa_wrap_transition(const char *dir, bool native, bool lod)
	{
		model1 m(load_regions(dir, game::id{ "swa" })), arcade(load_regions(dir, game::id{ "swa" }));
		for (model1 *machine : { &m, &arcade }) {
			machine->reset();
			machine->set_drawing(false);
			game::swa::set_lod(*machine, lod);
		}
		game::swa::set_full_draw_distance(arcade, false);
		m.set_aspect(16.0f / 9);
		arcade.set_aspect(16.0f / 9);
		if (native) {
			rec::hints hints;
			assert(hints.read("assets/games/swa/swa_map.txt"));
			rec::attach(m.cpu(), "swa");
			assert(rec::build_native(m.program_rom(), hints));
			m.run_cpu = [](int n) { rec::run(n); };
		}
		game::swa::set_full_draw_distance(m, true);
		const auto state = std::static_pointer_cast<game::swa::gameplay_state>(m.game_state());
		const auto no_periodic_extras = [](const model1 &machine) {
			for (const auto &list : machine.m_list_extra)
				for (const auto &[matrix, extras] : list)
					for (const auto &extra : extras) assert(!extra.transform);
		};
		size_t arcade_asteroids = 0, arcade_ships = 0;
		const auto arcade_object = arcade.m_gameplay.object;
		arcade.m_gameplay.object = [&](uint32_t owner, uint32_t texture, uint32_t polygon, uint32_t size, const float *data) {
			if (polygon == (arcade.read32(0x23ba7a) & 0x7fffff) || polygon == (arcade.read32(0x23ba76) & 0x7fffff)) arcade_asteroids++;
			if (polygon == (arcade.read32(0x9b584) & 0x7fffff) || polygon == (arcade.read32(0x9a63c) & 0x7fffff) ||
				polygon == (arcade.read32(0x9a63c + 8) & 0x7fffff)) arcade_ships++;
			return arcade_object(owner, texture, polygon, size, data);
		};
		struct ship_sample { glm::vec3 view, relative; float fade; bool asteroid, cluster = false; };
		std::vector<ship_sample> ships;
		std::set<uint32_t> globe_polygons, approach_models;
		const auto object = m.m_gameplay.object;
		m.m_gameplay.object = [&](uint32_t owner, uint32_t texture, uint32_t polygon, uint32_t size, const float *data) {
			if (m.read8(0x40f008) == 3 && m.read8(0x40f770) == 2) {
				for (auto [model, colour] : { std::pair{0x23b682u, 0x44f9fu}, {0x23b686u, 0x450dau},
					{0x23b68au, 0x451a7u}, {0x23b68eu, 0x451a8u}, {0x23b692u, 0x45224u}, {0x23b696u, 0x452a0u} })
					if (polygon == (m.read32(model) & 0x7fffff)) {
						assert(texture == colour);
						assert(lod || model < 0x23b68e);
						globe_polygons.insert(model);
					}
			}
			// Track ship bodies, asteroid clusters and DB88C's small-rock clusters at their final renderer matrices.
			const bool cluster = polygon == (m.read32(0x23beba) & 0x7fffff);
			const bool asteroid = cluster || polygon == (m.read32(0x23ba7a) & 0x7fffff) || polygon == (m.read32(0x23ba76) & 0x7fffff);
			if (asteroid || polygon == (m.read32(0x9b584) & 0x7fffff) ||
				polygon == (m.read32(0x9a63c) & 0x7fffff) ||
				polygon == (m.read32(0x9a63c + 2 * 4) & 0x7fffff)) {
				const auto *v = m.m_view->translation;
				glm::mat3 rotation(glm::vec3(v[0], v[1], v[2]) / 2.48f,
					glm::vec3(v[3], v[4], v[5]) / 2.48f, glm::vec3(v[6], v[7], v[8]) / 2.48f);
				if (asteroid)
					for (int c = 0; c < 3; c++)
						for (int r = 0; r < 3; r++)
							rotation[c][r] = std::bit_cast<float>(m.read32(0x40f570 + 4 * (3 * c + r)));
				const glm::vec3 view(v[9], v[10], v[11]);
				ships.push_back({ view, glm::inverse(rotation) * view, m.m_object_fade, asteroid, cluster });
			}
			return object(owner, texture, polygon, size, data);
		};
		std::map<int, decltype(ships)> frames;
		size_t death_star_frames = 0;
		const auto read32_value = m.m_gameplay.read32_value;
		m.m_gameplay.read32_value = [&](uint32_t pc, uint32_t address, uint32_t value) {
			const auto result = read32_value(pc, address, value);
			if (pc == 0xc18af && address == m.cpu().reg(20) + 2 && m.read8(0x40f008) == 3 && m.read8(0x40f770) == 2 &&
				(value == 0x23b68e || value == 0x23b692 || value == 0x23b696)) {
				approach_models.insert(value);
				assert(result == (lod ? value : value - 12));
			}
			return result;
		};
		for (int f = 1; f <= 29000; f++) {
			m.run_frame();
			arcade.run_frame();
			assert(m.nvram() == arcade.nvram() && m.work_ram() == arcade.work_ram());
			if (m.read8(0x40f008) == 3 && m.read8(0x40f770) == 2 && f % 100 == 0) {
				assert(!state->z_wrap_prefetched && state->asteroid_objects.empty());
				ships.clear();
				m.redraw();
				arcade_asteroids = 0;
				arcade.redraw();
				assert(arcade_asteroids == 0);
				assert(std::none_of(ships.begin(), ships.end(), [](const auto &s) { return s.asteroid; }));
				death_star_frames++;
			}
			if ((f >= 6072 && f <= 6180) || (f >= 6968 && f <= 6972)) {
				ships.clear();
				m.redraw();
				frames[f] = ships;
				const std::vector<uint32_t> pixels(m.frame().data(), m.frame().data() + m.frame_w() * m.frame_h());
				auto saved0 = m.m_list_extra[0], saved1 = m.m_list_extra[1];
				for (auto &list : m.m_list_extra)
					for (auto &[matrix, extras] : list)
						for (auto &extra : extras) extra.bounds.reset();
				m.redraw();
				assert(std::equal(pixels.begin(), pixels.end(), m.frame().data()));
				m.m_list_extra[0] = std::move(saved0);
				m.m_list_extra[1] = std::move(saved1);
				ships = frames[f];
				arcade_asteroids = arcade_ships = 0;
				arcade.redraw();
				no_periodic_extras(arcade);
				assert(!std::static_pointer_cast<game::swa::gameplay_state>(arcade.game_state())->z_wrap_prefetched);
			}
			if (f == 6072) {
				assert(!state->destroyer_manager && state->destroyer_models.empty() && state->asteroid_objects.empty());
				assert(std::count_if(ships.begin(), ships.end(), [](const auto &s) { return s.asteroid && !s.cluster; }) >= 5);
				assert(std::count_if(ships.begin(), ships.end(), [](const auto &s) { return s.cluster; }) >= 5);
				assert(arcade_asteroids == 0 && arcade_ships == 0);
			}
			if (f == 6102) assert(state->asteroid_objects.size() == 38 + 5);
			if (f == 6142) assert(arcade_asteroids > 0 && arcade_ships > 0);
			if (f == 6142) {
				size_t duplicates = 0;
				for (size_t a = 0; a < ships.size(); a++)
					for (size_t b = 0; b < a; b++)
						if (ships[a].asteroid && ships[b].asteroid && glm::length(ships[a].view - ships[b].view) < 1) duplicates++;
				assert(duplicates == 0);
			}
			if (f == 6138) assert(state->destroyer_models.empty() && state->destroyer_manager);
			if (f == 6142 || f == 6972) {
				m.set_render_scale(3);
				m.redraw();
				const std::vector<uint32_t> pixels(m.frame().data(), m.frame().data() + m.frame_w() * m.frame_h());
				auto saved0 = m.m_list_extra[0], saved1 = m.m_list_extra[1];
				for (auto &list : m.m_list_extra)
					for (auto &[matrix, extras] : list)
						for (auto &extra : extras) extra.bounds.reset();
				m.redraw();
				assert(std::equal(pixels.begin(), pixels.end(), m.frame().data()));
				m.m_list_extra[0] = std::move(saved0);
				m.m_list_extra[1] = std::move(saved1);
				m.set_render_scale(1);
			}
		}
		for (const auto &[after, samples] : frames) {
			const int before = after - 1;
			if (!frames.contains(before)) {
				assert(samples.size() >= 5);
				continue;
			}
			assert(frames[before].size() >= 5 && frames[after].size() >= 5);
			for (const auto &ship : samples) {
				if (ship.fade <= 0.17f || ship.view.z <= 0 ||
					std::fabs(ship.view.x) >= ship.view.z || std::fabs(ship.view.y) >= ship.view.z) continue;
				float nearest = INFINITY;
				for (const auto &previous : frames[before])
					if (ship.asteroid == previous.asteroid && ship.cluster == previous.cluster) nearest = std::min(nearest, glm::length(ship.relative - previous.relative));
				if (nearest > 4096) {
					fprintf(stderr, "SWA %s appeared at frame %d: (%g, %g, %g), nearest previous object %g units away\n",
						ship.cluster ? "rock cluster" : ship.asteroid ? "asteroid" : "ship", after, ship.view.x, ship.view.y, ship.view.z, nearest);
					assert(false);
				}
			}
		}
		assert(death_star_frames >= 30);
		assert(approach_models == (std::set<uint32_t>{0x23b68e, 0x23b692, 0x23b696}));
		assert(globe_polygons == (lod
			? std::set<uint32_t>{0x23b682, 0x23b686, 0x23b68a, 0x23b68e, 0x23b692, 0x23b696}
			: std::set<uint32_t>{0x23b682, 0x23b686, 0x23b68a}));
		printf("SWA %s LOD %s: fleet, asteroids and rock clusters present from first scene picture (6072); continuous through 6180 and 6968-6972; no asteroids in Death Star demo; globe models follow LOD setting; Arcade has no periodic extras\n",
			native ? "native" : "interpreter", lod ? "on" : "off");
		game::swa::set_full_draw_distance(m, false);
		for (int f = 0; f < 10; f++) {
			m.run_frame();
			arcade.run_frame();
			assert(m.nvram() == arcade.nvram() && m.work_ram() == arcade.work_ram());
		}
		m.redraw();
		arcade.redraw();
		assert(std::equal(m.frame().data(), m.frame().data() + m.frame_w() * m.frame_h(), arcade.frame().data()));
		assert(!state->z_wrap_prefetched);
		if (native) assert(rec::fallback_count() == 0);
	}

	static void overlay_priority()
	{
		for (int scale : { 1, 2 })
			for (float aspect : { 4.0f / 3, 16.0f / 9 }) {
				model1::regions regions;
				regions.title = game::id{ "swa" };
				model1 m(std::move(regions));
				m.set_render_scale(scale);
				m.set_aspect(aspect);
				m.m_pens[1] = 0x00ff00;
				m.m_pens[2] = 0x000000;
				m.m_pens[3] = 0xffffff;
				for (int i = 0; i < 16; i++) {
					m.m_tiles.char_w(16 + i, 0x1111, 0xffff);
					m.m_tiles.char_w(32 + i, 0x2222, 0xffff);
					m.m_tiles.char_w(48 + i, 0x3333, 0xffff);
				}
				m.m_tiles.tile_w(0, 0x8001, 0xffff);
				m.m_tiles.tile_w(1, 0x8002, 0xffff);
				m.m_tiles.tile_w(2, 3, 0xffff);
				m.m_tiles.update(m.m_pens);
				auto &bitmap = m.m_bitmap;
				bitmap.fill(0x808080);
				for (int layer : { 7, 5, 3, 1 }) m.draw_tiles(bitmap, bitmap.cliprect(), layer, 0);
				m.build_overlay_mask(bitmap, bitmap.cliprect());
				bitmap.fill(0xffff00);
				m.apply_overlay_stencil(bitmap, bitmap.cliprect());
				for (int y = 0; y < bitmap.height(); y++)
					for (int x = 0; x < bitmap.width(); x++) {
						const bool feature = y < 8 * scale && x >= m.m_ext * scale && x < (m.m_ext + 8) * scale;
						assert(bitmap.pix(y, x) == (feature ? 0x00ff00u : 0xffff00u));
					}
			}
		puts("Overlay priority: only HUD features mask foreground polygons, not dark HUD or bright backgrounds");
	}

	static void swa_name_entry(const char *dir)
	{
		const game::id title{ "swa" };
		model1 m(load_regions(dir, title));
		m.reset();
		m.set_drawing(false);
		auto play = game::make_auto_play(title, "", 12345, false);
		assert(play);
		for (int f = 1; f <= 15750; f++) {
			const auto in = play->frame(f, m);
			for (size_t c = 0; c < in.analog.size(); c++)
				if (in.analog[c] >= 0) assert(m.set_analog_input(c, uint8_t(in.analog[c])));
			for (size_t c = 0; c < in.pressed.size(); c++)
				m.m_inputs[c] = uint8_t(~in.pressed[c]);
			m.run_frame();
		}
		for (int scale : { 1, 2 }) {
			m.set_render_scale(scale);
			m.screen_update(m.m_bitmap, m.m_bitmap.cliprect());
			assert(m.m_above_hud_seen);
			bitmap_rgb32 unmasked;
			unmasked.allocate(m.frame_w(), m.frame_h());
			std::copy_n(m.frame().data(), size_t(m.frame_w()) * m.frame_h(), unmasked.data());
			m.tgp_render(unmasked, unmasked.cliprect(), model1::RENDER_ABOVE_HUD);
			size_t yellow = 0;
			for (int y = 50 * scale; y < m.frame_h(); y++)
				for (int x = 0; x < m.frame_w(); x++) {
					const uint32_t pixel = m.m_bitmap.pix(y, x);
					assert(pixel == unmasked.pix(y, x));
					if ((pixel & 0xffff00) == 0xffff00 && (pixel & 0xff) == 0) yellow++;
				}
			assert(yellow > size_t(1000 * scale * scale));
			printf("SWA name entry (%dx): %zu yellow letter pixels, no masking by the photograph\n", scale, yellow);
		}
	}

	static void trackside_collisions()
	{
		model1::regions regions;
		regions.title = game::id{ "vr" };
		model1 m(std::move(regions));
		m.cpu().set_reg(25, 0x407900);
		for (bool full : { false, true }) {
			game::vr::set_extended_draw_distance(m, full);
			for (int cell = 0; cell < 256; cell++) {
				m.write16(0x407934, cell);
				for (bool visible : { false, true }) {
					const uint32_t mask = visible ? 1u << (cell % 32) : 0;
					m.write32(0x501ac0 + cell / 32 * 4, mask);
					for (uint32_t pc : { 0xffb19du, 0xffb1bdu, 0xffb797u, 0xffb6a7u, 0xffaaedu,
						0xffdc94u, 0xffdc4du, 0u })
						for (float distance : { -1000.0f, -1.0f, 0.0f, 1.0f, 1000.0f }) {
							const bool added_query = pc == 0xffb19d || pc == 0xffb1bd || pc == 0xffb797 ||
								pc == 0xffb6a7 || pc == 0xffaaed || pc == 0xffdc94;
							const uint32_t value = std::bit_cast<uint32_t>(distance);
							const uint32_t expected = std::bit_cast<uint32_t>(full && !visible && added_query ? 10000.0f : distance);
							assert(m.m_gameplay.copro_result(pc, value) == expected);
							assert(m.read32(0x501ac0 + cell / 32 * 4) == mask);
						}
				}
			}
		}
	}

	static void trackside_distance(const char *dir, bool native, bool smooth = true, bool lod = true, bool racing = false)
	{
		model1 arcade(load_regions(dir, game::id{ "vr" })), full(load_regions(dir, game::id{ "vr" }));
		for (model1 *m : { &arcade, &full }) {
			m->set_smooth(smooth);
			game::vr::set_lod(*m, lod);
			m->reset();
		}
		game::vr::set_extended_draw_distance(arcade, false);
		if (native) {
			rec::hints map;
			assert(map.read("assets/games/vr/vr_map.txt"));
			rec::attach(full.cpu(), "vr");
			assert(rec::build_native(full.program_rom(), map));
			full.run_cpu = [](int n) { rec::run(n); };
		}
		std::array<int, 8> calls{}, outside{}, writes{}, pixels{};
		std::array<std::vector<std::vector<uint32_t>>, 2> simulation;
		int sim_index = 0;
		for (model1 *m : { &arcade, &full }) {
			const int index = sim_index++;
			const auto written = m->m_gameplay.work_ram_written;
			m->m_gameplay.work_ram_written = [&, m, index, written](uint16_t mask) {
				written(mask);
				if (!(mask & 0xff) || m->cpu().pc() != 0xff7dc5) return;
				auto &snapshot = simulation[index].emplace_back();
				for (uint32_t car = 0x400d00; car < 0x403500; car += 0x280)
					for (uint32_t field : { 0x1eu, 0x22u, 0x26u, 0x30u, 0x3cu, 0x44u, 0x191u, 0x1abu })
						snapshot.push_back(m->read32(car + field));
				for (uint32_t record = 0x407900; record < m->read32(0x501368); record += 0x80)
					if (m->read32(record + 0xa) == 0xffcc7d)
						for (uint32_t offset = 0x1c; offset < 0x6b; offset++)
							snapshot.push_back(m->read8(record + offset));
			};
		}
		std::array<std::set<uint32_t>, 8> polygons;
		for (uint32_t list : { 0x2e02c0u, 0x2e02d0u, 0x2e02e0u, 0x2e0340u })
			polygons[7].insert(full.read32(list));
		polygons[6].insert(full.read32(0x2e02d0));
		polygons[3].insert(full.read32(0x2e03e0));
		polygons[4].insert(full.read32(0x2e00f0));
		for (int i = 0; i < 128; i++)
			polygons[5].insert(full.read32(full.read32(0xfd8e61 + i * 4)));
		for (uint32_t list : { 0x2e00d0u, 0x2e00e0u }) polygons[0].insert(full.read32(list));
		for (uint32_t table : { 0xfdc033u, 0xfdc0b3u, 0xfdc133u, 0xfdc1b3u, 0xfdc233u })
			for (int i = 0; i < 32; i++) polygons[1].insert(full.read32(full.read32(table + i * 4)));
		polygons[2].insert(full.read32(0x2e01c0));
		for (uint32_t table = 0xfd9fc3; table <= 0xfda4c3; table += 0x80)
			for (int i = 0; i < 32; i++) polygons[2].insert(full.read32(full.read32(table + i * 4)));
		const auto read = full.m_gameplay.read32_value;
		full.m_gameplay.read32_value = [&](uint32_t pc, uint32_t address, uint32_t value) {
			const int type = pc == 0xffb769 ? 0 : pc == 0xffb0eb ? 1 : pc == 0xffc1b2 ? 2 :
				pc == 0xffaac4 ? 3 : pc == 0xffb679 ? 4 : pc == 0xffdc4d ? 5 : pc == 0xffd0e2 ? 6 : -1;
			const uint32_t result = read(pc, address, value);
			if (type >= 0) {
				const uint32_t cell = full.read16(full.cpu().reg(25) + 0x34);
				assert(cell < 256 && result == 0xffffffff);
				calls[type]++;
				if (!(value & (1u << (cell % 32)))) outside[type]++;
			}
			return result;
		};
		const auto matrix = full.m_gameplay.matrix_objects;
		full.m_gameplay.matrix_objects = [&](uint32_t &word) {
			if (full.cpu().pc() == 0xff8500) {
				const uint32_t caller = full.read32(full.cpu().reg(31));
				if (caller == 0xffb81c || caller == 0xffb859) writes[0]++;
				if (caller == 0xffb254) writes[1]++;
				if (caller == 0xffc235 || caller == 0xffc251) writes[2]++;
				if (caller == 0xffab97) writes[3]++;
				if (caller == 0xffb72c) writes[4]++;
				if (caller == 0xffddd4 || caller == 0xffddeb) writes[5]++;
				if (caller == 0xffd19c) writes[6]++;
			}
			auto extra = matrix(word);
			for (const auto &o : extra)
				if (o.owner && full.read32(o.owner + 0xa) == 0xffcc7d) {
					calls[7]++;
					outside[7]++;
					assert(o.transform);
					assert(polygons[7].count(o.polygon));
				}
			return extra;
		};
		const auto object = full.m_gameplay.object;
		size_t compared = 0;
		int rear_banner_pixels = 0;
		int racing_sign_pixels = 0;
		int racing_breakable_pixels = 0;
		auto play = game::make_auto_play(game::id{ "vr" }, "", 12345, false);
		assert(play);
		for (int f = 1; f <= 6000; f++) {
			if (racing) {
				const auto input = play->frame(f);
				for (model1 *m : { &arcade, &full }) {
					for (size_t c = 0; c < input.pressed.size(); c++)
						m->m_inputs[c] = uint8_t(~input.pressed[c]);
					for (size_t c = 0; c < input.analog.size(); c++)
						if (input.analog[c] >= 0) assert(m->set_analog_input(c, uint8_t(input.analog[c])));
				}
			}
			arcade.run_frame();
			full.run_frame();
			const size_t common = std::min(simulation[0].size(), simulation[1].size());
			while (compared < common) {
				if (simulation[0][compared] != simulation[1][compared])
					printf("Simulation difference f=%d snapshot=%zu\n", f, compared);
				assert(simulation[0][compared] == simulation[1][compared]);
				compared++;
			}
			if (f % 4 != 1 || (smooth && full.m_logic[1].frame != f - 1)) continue;
			bool rear = false;
			if (smooth) {
				const auto &a = full.m_logic[0], &b = full.m_logic[1];
				if (a.has_camera && b.has_camera) {
					auto eye = [](const std::array<float, 12> &m) {
						const glm::mat3 rotation(glm::vec3(m[0], m[1], m[2]), glm::vec3(m[3], m[4], m[5]), glm::vec3(m[6], m[7], m[8]));
						return -glm::transpose(rotation) * glm::vec3(m[9], m[10], m[11]);
					};
					const glm::vec3 move = eye(b.camera) - eye(a.camera);
					const glm::vec3 forward(b.camera[2], b.camera[5], b.camera[8]);
					const float distance = glm::length(move);
					rear = distance > .05f && distance < 8 && glm::dot(forward, move) < -.5f * distance;
				}
				full.render_smooth(double(full.frame_number()) - .5);
			}
			const size_t n = size_t(full.frame_w()) * full.frame_h();
			const std::vector<uint32_t> picture(full.frame().data(), full.frame().data() + n);
			for (int type = 0; type < 8; type++) {
				full.m_gameplay.object = [&](uint32_t owner, uint32_t texture, uint32_t polygon, uint32_t size, const float *data) {
					auto effect = object(owner, texture, polygon, size, data);
					if (polygons[type].count(polygon) &&
						(type != 6 || full.read32(owner + 0xa) == 0xffd0d3) &&
						(type != 7 || full.read32(owner + 0xa) == 0xffcc7d)) {
						assert(owner >= 0x400000 && owner < 0x410000);
						const uint32_t cell = full.read16(owner + 0x34);
						assert(cell < 256);
						const uint32_t mask = type == 2 ? 0x501aa0 : 0x501ac0;
						effect.hidden = !(full.read32(mask + cell / 32 * 4) & (1u << (cell % 32)));
					}
					return effect;
				};
				full.redraw();
				int changed = 0;
				for (size_t i = 0; i < n; i++) if (picture[i] != full.frame().data()[i]) changed++;
				if (changed > 4) pixels[type]++;
				if (type == 1 && changed > 4 && rear) rear_banner_pixels++;
				if (type == 6 && changed > 4 && full.work_ram()[0x1138] == 0x14) racing_sign_pixels++;
				if (type == 7 && changed > 4 && full.work_ram()[0x1138] == 0x14) racing_breakable_pixels++;
			}
			full.m_gameplay.object = object;
		}
		printf("Matched simulation updates: %zu; rear-facing distant banner pixel samples: %d\n", compared, rear_banner_pixels);
		assert(compared > 2000);
		if (smooth && !racing) assert(rear_banner_pixels > 5);
		if (racing) {
			printf("Beginner race distant arrow-board pixel samples: %d\n", racing_sign_pixels);
			assert(racing_sign_pixels > 200);
			printf("Beginner race distant breakable-board pixel samples: %d; supplemental instances: %d\n",
				racing_breakable_pixels, calls[7]);
			assert(calls[7] > 100);
		}
		for (int type = 0; type < 8; type++) {
			printf("Trackside %s (%s): %d calls, %d outside, %d part writes, %d distant pixel samples; car simulation unchanged\n",
				type == 0 ? "windmill" : type == 1 ? "banner" : type == 2 ? "flag" :
					type == 3 ? "light pole" : type == 4 ? "windmill building" : type == 5 ? "palm" :
					type == 6 ? "arrow board" : "breakable board",
				native ? "native" : "interpreter",
				calls[type], outside[type], writes[type], pixels[type]);
			if (!racing || type >= 6)
				assert(calls[type] > 100 && outside[type] > 100 && pixels[type] > (type >= 6 ? 0 : 5));
			if (type != 7) assert(writes[type] == calls[type] * (type == 0 || type == 2 ? 4 : 2));
		}
	}

	static void attract_scenery(const char *dir, bool full, bool smooth, bool lod, bool native)
	{
		model1 m(load_regions(dir, game::id{ "vr" }));
		if (native) {
			rec::hints map;
			assert(map.read("assets/games/vr/vr_map.txt"));
			rec::attach(m.cpu(), "vr");
			assert(rec::build_native(m.program_rom(), map));
			m.run_cpu = [](int n) { rec::run(n); };
		}
		m.set_smooth(smooth);
		game::vr::set_extended_draw_distance(m, full);
		game::vr::set_lod(m, lod);
		m.reset();
		int checks = 0, wheels = 0, culled = 0, active = 0, parts = 0, outside = 0;
		std::map<std::vector<uint32_t>, std::set<uint32_t>> expected;
		const auto read32_value = m.m_gameplay.read32_value;
		m.m_gameplay.read32_value = [&](uint32_t pc, uint32_t address, uint32_t value) {
			const uint32_t result = read32_value(pc, address, value);
			if (pc == 0xffd928 && address >= 0x501aa0 && address <= 0x501abc) {
				wheels++;
				const uint32_t bit = 1u << (m.read16(m.cpu().reg(25) + 0x34) % 32);
				if (!(value & bit)) outside++;
				if (!(result & bit)) culled++;
				else if (m.read8(0x501a35)) active++;
			}
			return result;
		};
		const auto matrix_objects = m.m_gameplay.matrix_objects;
		m.m_gameplay.matrix_objects = [&](uint32_t &word) {
			auto extra = matrix_objects(word);
			if (m.cpu().pc() == 0xff8500) {
				const uint32_t caller = m.read32(m.cpu().reg(31));
				if (caller == 0xffd9e8 || caller == 0xffda21 || caller == 0xffda67) parts++;
			}
			if (extra.empty()) return extra;
			std::vector<uint32_t> key;
			for (const auto &o : extra) key.push_back(o.polygon);
			auto &objects = expected[key];
			const uint32_t table = m.read32(0x501948);
			for (int cell = 0; cell < 256; cell++) {
				uint32_t record = m.read32(table + cell * 4);
				if (!record || record == 0xffffffff) continue;
				for (int n = 0; m.read32(record); n++, record += 12) {
					assert(n < 4096);
					objects.insert(m.read32(record));
				}
			}
			return extra;
		};
		for (int f = 1; f <= 6000; f++) {
			m.run_frame();
			if (!smooth) continue;
			const auto &frame = m.m_logic[1];
			if (frame.frame != f - 1) continue;
			std::set<uint32_t> drawn;
			const std::set<uint32_t> *course = nullptr;
			for (const auto &slot : frame.slots) {
				if (!slot.extra.empty()) {
					assert(slot.owner == 1);
					std::vector<uint32_t> key;
					for (const auto &o : slot.extra) key.push_back(o.polygon);
					course = &expected.at(key);
				}
				for (const auto &o : slot.extra) drawn.insert(o.polygon);
				for (uint32_t poly : slot.polys) drawn.insert(poly);
				for (uint32_t caller_poly : { 0x2e0130u, 0x2e0140u, 0x2e0150u }) {
					const uint32_t polygon = m.read32(caller_poly);
					if (std::find(slot.polys.begin(), slot.polys.end(), polygon) != slot.polys.end())
						assert(slot.owner >= 0x400000 && slot.owner < 0x410000);
				}
			}
			if (!course) continue;
			checks++;
			for (uint32_t polygon : *course) assert(drawn.count(polygon));
		}
		printf("Attract scenery (%s, %s, smooth %s, LOD %s): %d complete lists; wheel %d calls, %d outside, %d culled, %d part writes\n",
			native ? "native" : "interpreter", full ? "full" : "arcade", smooth ? "on" : "off", lod ? "on" : "off",
			checks, wheels, outside, culled, parts);
		assert(wheels > 100 && outside > 100 && active > 0);
		assert(parts == active * 3 * 2); // Each matrix command is written as two 16-bit words.
		if (full) {
			assert(culled == 0);
			if (smooth) assert(checks > 100);
		} else assert(culled == outside && checks == 0);
	}

	static void crash_materials()
	{
		model1::regions regions;
		regions.title = game::id{ "vr" };
		regions.maincpu.resize(0x1000000);
		auto write = [&](uint32_t address, uint32_t value) {
			for (int i = 0; i < 4; i++) regions.maincpu[address + i] = uint8_t(value >> (i * 8));
		};
		write(0xfcd395 + 4, 0x9000);
		write(0x9000, 0x1234);
		for (int color = 1; color < 8; color++) {
			write(0xfcd195 + color * 32 + 16, 0x9100 + color * 16);
			write(0x9100 + color * 16 + 4, 0x40000 + color * 2);
		}
		model1 m(std::move(regions));
		assert(game::vr::enable_standalone_car_color(m));
		m.write32(0x501320, 0x400d00);
		m.write8(0x40dc8c, 0);
		m.work_ram()[0x1138] = 0x14;
		for (int color = 1; color < 8; color++) {
			m.write8(0x40dca2, uint8_t(color));
			const uint16_t paint = uint16_t(0x100 + color);
			m.m_tgp_ram[color * 2 + 1] = paint;
			const auto effect = m.m_gameplay.object(0x400d00, 0x40000, 0x1234, 0, nullptr);
			assert(effect.material);
			for (int material = 0; material <= 0xffff; material++) {
				const int pen = material & 0x3ff;
				const auto expected = pen == 2 || pen == 0xee || pen == 0xf6
					? uint16_t((material & 0xfc00) | paint) : uint16_t(material);
				assert(effect.material(uint16_t(material)) == expected);
			}
			assert(!m.m_gameplay.object(0x400f80, 0x40000, 0x1234, 0, nullptr).material);
			assert(!m.m_gameplay.object(0x400d00, 0x40000, 0x5678, 0, nullptr).material);
		}
		m.write8(0x40dca2, 0);
		assert(!m.m_gameplay.object(0x400d00, 0x40000, 0x1234, 0, nullptr).material);
		m.write8(0x40dca2, 3);
		m.write8(0x40dc8c, 1);
		assert(!m.m_gameplay.object(0x400d00, 0x40000, 0x1234, 0, nullptr).material);
		m.write8(0x40dc8c, 0);
		m.work_ram()[0x1138] = 3;
		assert(!m.m_gameplay.object(0x400d00, 0x40000, 0x1234, 0, nullptr).material);
	}

	static void crash_livery(const char *dir, bool smooth, bool native = false)
	{
		model1 m(load_regions(dir, game::id{ "vr" }));
		assert(game::vr::enable_standalone_car_color(m));
		if (native) {
			rec::hints map;
			assert(map.read("assets/games/vr/vr_map.txt"));
			rec::attach(m.cpu(), "vr");
			assert(rec::build_native(m.program_rom(), map));
			m.run_cpu = [](int n) { rec::run(n); };
		}
		m.set_smooth(smooth);
		m.reset();
		for (int f = 0; f < 900; f++) m.run_frame();
		assert(game::vr::set_coin_setting(m, game::vr::FREE_PLAY));
		assert(game::vr::set_machine_setting(m, game::vr::machine_setting::car_color, 3));
		m.reset();
		const auto rom = m.program_rom();
		const auto object_effect = m.m_gameplay.object;
		size_t painted = 0, neutral = 0, opponents = 0;
		std::set<uint32_t> damaged;
		for (int i = 0; i < 3; i++) damaged.insert(m.read32(m.read32(0xfd4a39 + i * 4)));
		std::set<uint32_t> seen;
		m.m_gameplay.object = [&](uint32_t owner, uint32_t tex, uint32_t poly, uint32_t size, const float *data) {
			auto effect = object_effect(owner, tex, poly, size, data);
			if (owner >= 0x400d00 && owner <= 0x401e80 && owner != m.read32(0x501320)) {
				assert(!effect.material && !effect.colour);
				opponents++;
			}
			if (!effect.material) return effect;
			assert(owner == m.read32(0x501320));
			assert(!effect.hidden && !effect.point && !effect.normal && !effect.polygon);
			if (!damaged.count(poly)) return effect;
			seen.insert(poly);
			const auto material = effect.material;
			effect.material = [&, material](uint16_t before) {
				const uint16_t after = material(before);
				assert((before & 0xfc00) == (after & 0xfc00));
				const uint16_t pen = before & 0x3ff;
				if (pen == 0x02 || pen == 0xee || pen == 0xf6) {
					const uint16_t paint = m.palette_word(0x1000 | (after & 0x3ff));
					const int r = paint & 31, g = (paint >> 5) & 31, b = (paint >> 10) & 31;
					assert(g > r && g > b);
					painted++;
				} else {
					assert(before == after);
					neutral++;
				}
				return after;
			};
			return effect;
		};
		int racing = 0;
		for (int f = 1; f <= 6000 && racing < 240; f++) {
			m.m_inputs[0] = uint8_t(~((f >= 3000 && f < 3006) || (f >= 3120 && f < 3126) ? 0x10 : 0));
			assert(m.set_analog_input(0, 0x80));
			assert(m.set_analog_input(1, 0xff));
			m.run_frame();
			if (smooth) m.render_smooth(double(m.frame_number()) - 0.5);
			if (m.work_ram()[0x1138] != 0x14) continue;
			racing++;
			// Exercise the ROM's three impact models, not replacement host geometry.
			if (racing == 60 || racing == 100 || racing == 140) {
				const uint32_t player = m.read32(0x501320);
				m.write16(player + 0x191, 1);
				const uint32_t flags = racing == 60 ? 8 : racing == 100 ? 16 : 24;
				m.write32(player, (m.read32(player) & ~24u) | flags);
			}
		}
		printf("VR green crash livery (%s, smooth %s): %zu painted and %zu unchanged detail polygons, %zu opponent objects, %zu impact models\n",
			native ? "native" : "interpreter", smooth ? "on" : "off", painted, neutral, opponents, seen.size());
		assert(racing == 240 && painted > 100 && neutral > 100 && opponents > 100);
		assert(seen == damaged && m.program_rom() == rom);
		if (native) assert(rec::fallback_count() == 0);
	}

	static void scenery_distance(const char *dir, bool smooth, bool lod)
	{
		model1 m(load_regions(dir, game::id{ "vr" }));
		m.set_smooth(smooth);
		game::vr::set_lod(m, lod);
		m.reset();
		auto play = game::make_auto_play(game::id{ "vr" }, "alternate", 12345, true);
		assert(play);
		using object_key = std::pair<uint32_t, uint32_t>;
		std::set<object_key> drawn;
		int checked = 0, incomplete = 0, budget_skips = 0;
		bool collecting = false;
		std::set<uint8_t> phases;
		const auto matrix_objects = m.m_gameplay.matrix_objects;
		m.m_gameplay.matrix_objects = [&](uint32_t &word) {
			auto extra = matrix_objects(word);
			if (collecting)
				for (const auto &object : extra)
					if (!object.owner) assert(drawn.emplace(object.texture, object.polygon).second);
			return extra;
		};
		auto trace = [&](uint32_t pc) {
			if (pc == 0xff8450) {
				drawn.clear();
				collecting = true;
			}
			if (!collecting) return;
			if (pc == 0xff84a2 && m.read32(0x5011a0) > m.read32(0x5011b0)) budget_skips++;
			if (pc == 0xff8566 && m.read32(m.cpu().reg(31)) == 0xff846f)
				assert(drawn.emplace(m.cpu().reg(4), m.cpu().reg(3)).second);
			if (pc != 0xff8473) return;
			collecting = false;
			std::set<object_key> expected;
			const uint32_t table = m.read32(0x501948);
			for (int cell = 0; cell < 256; cell++) {
				uint32_t record = m.read32(table + cell * 4);
				if (!record || record == 0xffffffff) continue;
				for (int n = 0; m.read32(record); n++, record += 12) {
					assert(n < 4096);
					expected.emplace(m.read32(record + 4), m.read32(record));
				}
			}
			checked++;
			phases.insert(m.work_ram()[0x1138]);
			if (drawn != expected) {
				if (!incomplete)
					printf("Scenery gap: drawn %zu of %zu objects, budget %u/%u\n",
						drawn.size(), expected.size(), m.read32(0x5011a0), m.read32(0x5011b0));
				incomplete++;
			}
		};
		for (int f = 1; f <= 4500; f++) {
			const auto inputs = play->frame(f);
			for (size_t c = 0; c < inputs.analog.size(); c++)
				if (inputs.analog[c] >= 0) assert(m.set_analog_input(c, uint8_t(inputs.analog[c])));
			for (size_t c = 0; c < inputs.pressed.size(); c++)
				m.m_inputs[c] = uint8_t(~inputs.pressed[c]);
			if (f == 3300) m.on_instruction = trace;
			m.run_frame();
			const auto error = play->check(f, m);
			if (!error.empty()) fprintf(stderr, "%s\n", error.c_str());
			assert(error.empty());
		}
		printf("Acropolis scenery (smooth %s, LOD %s): %d lists, %d incomplete, %d budget skips\n",
			smooth ? "on" : "off", lod ? "on" : "off", checked, incomplete, budget_skips);
		assert(checked > 100 && incomplete == 0);
		if (lod) assert(budget_skips > 0);
		else assert(budget_skips == 0);
		assert(phases.count(0x12) && phases.count(0x14));
	}

	static void backface_specular()
	{
		model1::regions regions;
		regions.title = game::id{ "vr" };
		model1 m(std::move(regions));
		m.m_view->spec_enable = true;
		m.m_view->set_lightparam(0, 0.5f, 0.2f, 1.0f, 4);
		glm::vec3 light(0, 0, 1), front(0, 0, 1), back(0, 0, -1), edge(1, 0, 0);
		assert(m.compute_specular(front, light, 1, 0) > 0);
		assert(m.compute_specular(back, light, -1, 0) == 0);
		assert(m.compute_specular(edge, light, 0, 0) == 0);
		m.m_view->spec_enable = false;
		assert(m.compute_specular(front, light, 1, 0) == 0);
	}

	static void logo_shading(const char *dir)
	{
		model1 m(load_regions(dir, game::id{ "vr" }));
		m.set_smooth(true);
		m.reset();
		bool saw_logo = false, back_only = true, hide_logo = false;
		m.m_gameplay.object = [&](uint32_t, uint32_t, uint32_t poly, uint32_t, const float *) {
			game::object_effects effect;
			effect.hidden = poly != 0x1031c7 || hide_logo;
			if (effect.hidden) return effect;
			saw_logo = true;
			effect.normal = [&](float &x, float &y, float &z) {
				const glm::vec3 normal(x, y, z);
				if (glm::dot(normal, normal) == 0) return;
				glm::vec3 current = normal;
				m.m_view->transform_vector(current);
				back_only &= glm::dot(glm::normalize(current), m.m_view->light) < -0.5f;
				if (m.m_shade_from && m.m_alpha < 1) {
					for (const float *matrix : { m.m_shade_from, m.m_shade_to }) {
						const glm::vec3 turned(
							matrix[0] * x + matrix[3] * y + matrix[6] * z,
							matrix[1] * x + matrix[4] * y + matrix[7] * z,
							matrix[2] * x + matrix[5] * y + matrix[8] * z);
						back_only &= glm::dot(glm::normalize(turned), m.m_view->light) < -0.5f;
					}
				}
			};
			return effect;
		};
		size_t checked = 0;
		for (int f = 1; f <= 2800; f++) {
			m.run_frame();
			if (f < 280 || f % 2) continue;
			for (double fraction : { 0.0, 0.5 }) {
				saw_logo = false;
				back_only = true;
				m.render_smooth(double(m.frame_number() - 1) + fraction);
				if (!saw_logo || !back_only) continue;
				const size_t count = size_t(m.frame_w()) * m.frame_h();
				const std::vector<uint32_t> logo(m.frame().data(), m.frame().data() + count);
				hide_logo = true;
				m.redraw();
				hide_logo = false;
				for (size_t i = 0; i < count; i++) {
					if (logo[i] == m.frame().data()[i]) continue;
					for (int shift : { 0, 8, 16 }) assert(((logo[i] >> shift) & 0xff) <= 0x60);
					checked++;
				}
			}
		}
		assert(checked > 1000);
		printf("VR reverse logo shading: %zu pixels retain their dark shade across arcade and interpolated pictures\n", checked);
	}

	static void race_grid_hud(const char *dir, const char *scenario, float aspect)
	{
		model1 m(load_regions(dir, game::id{ "vr" }));
		m.set_aspect(aspect);
		m.reset();
		auto play = game::make_auto_play(game::id{ "vr" }, scenario, 12345, false);
		assert(play);
		std::set<uint8_t> checked;
		size_t gauge_pixels = 0, go_pixels = 0;
		for (int f = 1; f <= 4800; f++) {
			const auto in = play->frame(f, m);
			for (size_t c = 0; c < in.analog.size(); c++)
				if (in.analog[c] >= 0) assert(m.set_analog_input(c, uint8_t(in.analog[c])));
			for (size_t c = 0; c < in.pressed.size(); c++)
				m.m_inputs[c] = uint8_t(~in.pressed[c]);
			m.run_frame();
			const uint8_t phase = m.work_ram()[0x1138];
			if (phase != 0x12 && phase != 0x14) continue;
			assert(m.hud_edge_pinning_active());
			bitmap_rgb32 original;
			original.allocate(model1::SCREEN_W + 2 * m.m_ext, model1::SCREEN_H);
			original.fill(model1::KEY);
			m.m_tiles.m_ox = m.m_ext;
			m.m_tiles.draw(original, original.cliprect(), 1, 0, 0);
			m.m_tiles.m_ox = 0;
			const auto &cached = m.cached_layer(1, 0).px;
			assert(m.m_tiles.tile_r(0x5000) == 0 && m.m_tiles.tile_r(0x5004) == 0);
			for (int y = 0; y < model1::SCREEN_H; y++)
				for (int x = 0; x < model1::SCREEN_W; x++) {
					const uint32_t value = original.pix(y, m.m_ext + x);
					if (value == model1::KEY) continue;
					const uint32_t code = m.m_tiles.tile_code(m.m_tiles.tile_r((y / 8) * 64 + x / 8));
					if (code >= 0x1180 && code <= 0x11bc) {
						assert(cached.pix(y, x) == value);
						gauge_pixels++;
					} else if (code >= 0x1300 && code <= 0x15ff) {
						assert(cached.pix(y, m.m_ext + x) == value);
						go_pixels++;
					}
				}
			m.pin_hud_tiles(original, 1);
			assert(std::equal(original.data(), original.data() + size_t(original.width()) * original.height(), cached.data()));
			size_t edge_pixels = 0;
			for (int y = 0; y < model1::SCREEN_H; y++)
				for (int x = 0; x < m.m_ext; x++)
					edge_pixels += cached.pix(y, x) != model1::KEY;
			if (edge_pixels > 100) checked.insert(phase);
		}
		assert(checked.size() == 2);
		assert(gauge_pixels > 10000);
		if (!*scenario) assert(go_pixels > 10000);
		printf("VR %s countdown/grid/racing HUD: edge-anchored at %.3f; centered prompts unchanged\n",
			*scenario ? scenario : "beginner", aspect);
	}

	static void race_grid_hud(const char *dir)
	{
		for (const char *scenario : { "", "intermediate" })
			for (float aspect : { 16.0f / 10, 16.0f / 9, 21.0f / 9 })
				race_grid_hud(dir, scenario, aspect);
	}

	static void car_visibility(const char *dir, bool extended, bool native = false)
	{
		model1 m(load_regions(dir, game::id{ "vr" })), reference(load_regions(dir, game::id{ "vr" }));
		reference.m_gameplay.record_visibility = {};
		if (native) {
			rec::hints map;
			assert(map.read("assets/games/vr/vr_map.txt"));
			rec::attach(m.cpu(), "vr");
			assert(rec::build_native(m.program_rom(), map));
			m.run_cpu = [](int n) { rec::run(n); };
		}
		for (auto *machine : { &m, &reference }) {
			game::vr::set_extended_draw_distance(*machine, extended);
			machine->set_smooth(true);
			machine->set_aspect(16.0f / 9);
			machine->reset();
		}
		int retained_frames = 0, checked_pictures = 0;
		for (int f = 1; f <= 480; f++) {
			m.run_frame();
			reference.run_frame();
			if (f < 469) continue;
			const auto &b = m.m_logic[1];
			int parts = 0;
			for (const auto &part : b.slots) {
				if (part.owner != 0x401480 || part.polys.empty()) continue;
				parts++;
				assert(part.retained == (b.frame == 472));
			}
			assert(parts == 3);
			assert(m.work_ram() == reference.work_ram() && m.nvram() == reference.nvram());
			for (int r = 0; r < 32; r++) assert(m.cpu().reg(r) == reference.cpu().reg(r));
			assert(m.cpu().pc() == reference.cpu().pc());
			assert(b.list == reference.m_logic[1].list);
			if (b.frame == 472) {
				retained_frames++;
				const auto found = std::find_if(b.visibility.begin(), b.visibility.end(),
					[](const auto &record) { return record.owner == 0x401480; });
				assert(found != b.visibility.end() && found->culled);
				const auto live = m.m_gameplay.record_visibility(0x401480);
				for (const auto &record : live)
					if (record.owner == 0x401480) assert(!record.culled);
				const auto &original = reference.m_logic[1].slots;
				assert(b.slots.size() == original.size() + 3);
				for (size_t i = 0; i < original.size(); i++)
					assert(b.slots[i].owner == original[i].owner && b.slots[i].polys == original[i].polys);
			}
			for (double fraction : { 0.0, 0.5 }) {
				m.render_smooth(double(m.frame_number() - 1) + fraction);
				reference.render_smooth(double(reference.frame_number() - 1) + fraction);
				if (f != 474) continue;
				size_t car_pixels = 0;
				for (int y = 240; y < 340; y++)
					for (int x = 0; x < 200; x++) {
						const size_t i = size_t(y) * m.frame_w() + x;
						car_pixels += m.frame().data()[i] != reference.frame().data()[i];
					}
				assert(car_pixels > 1000);
				checked_pictures++;
			}
		}
		assert(retained_frames == 2 && checked_pictures == 2);
		if (native) assert(rec::fallback_count() == 0);
		printf("VR opening car (%s, %s): all three parts retained for one update, visible in both missing-frame pictures; CPU, RAM and lists unchanged\n",
			extended ? "extended" : "arcade", native ? "native" : "interpreter");
	}

	// SWA's places are tens of thousands of units: the camera's move between two lists fits them
	// (no cut, every part blended), and keyed copies on the stars' slot blend with their pairs.
	static void swa_far_blend()
	{
		model1::regions regions;
		regions.title = game::id{ "swa" };
		model1 m(std::move(regions));
		model1::logic_frame z, a, b;
		z.frame = 8, a.frame = 10, b.frame = 12;
		auto copy = [](uint64_t key, float x, float z) {
			game::render_object o{ 0, 0x100, 0, 0, std::nullopt };
			o.key = key;
			o.transform = std::array<float, 12>{ 1, 0, 0, 0, 1, 0, 0, 0, 1, x, 0, z };
			return o;
		};
		model1::slot stars;
		stars.polys = { 0x10 };
		z.slots.push_back(stars);
		a.slots.push_back(stars);
		b.slots.push_back(stars);
		a.slots[0].extra = { copy(1, 1000, 5000), copy(2, 0, 9000), copy(2, 40000, 9000), copy(3, 0, 20000), copy(0, 0, 3000) };
		b.slots[0].extra = { copy(1, 1100, 5000), copy(2, 40010, 9000), copy(2, 10, 9000), copy(3, 0, 6000), copy(0, 0, 3000) };
		uint32_t seed = 1;
		auto next = [&] { seed = seed * 1103515245 + 12345; return float((seed >> 8) & 0xffff) / 65536; };
		for (int i = 0; i < 24; i++) {
			model1::slot sa;
			sa.polys = { 0x200u + i };
			sa.mat[9] = (next() - 0.5f) * 90000, sa.mat[10] = (next() - 0.5f) * 30000, sa.mat[11] = 3000 + next() * 60000;
			const float v[3] = { 50 + (next() - 0.5f) * 60, -30 + (next() - 0.5f) * 60, -150 + (next() - 0.5f) * 60 };
			model1::slot sz = sa, sb = sa;
			for (int k = 0; k < 3; k++) sz.mat[9 + k] -= v[k], sb.mat[9 + k] += v[k];
			z.slots.push_back(sz);
			a.slots.push_back(sa);
			b.slots.push_back(sb);
		}
		m.blend_slots(z, a, 0.5);
		m.blend_slots(a, b, 0.5);
		assert(!m.m_blend_cut && m.m_camera_sure && !m.m_camera_cut);
		for (size_t i = 0; i < b.slots.size(); i++) assert(m.m_blend_ok[i]);
		m.m_replaying = true;
		m.m_replay_slot = 0;
		const float expect[] = { 1050, 40005, 5, 0, 0 };   // (the last two: a far jump and no key, unblended)
		const float expect_z[] = { 5000, 9000, 9000, 6000, 3000 };
		for (size_t n = 0; n < b.slots[0].extra.size(); n++) {
			float out[12];
			m.smooth_extra(n, *b.slots[0].extra[n].transform, out);
			assert(std::fabs(out[9] - expect[n]) < 0.01f && std::fabs(out[11] - expect_z[n]) < 0.01f);
		}
		m.m_replaying = false;
		printf("SWA far places: no cut, all parts blended; keyed copies blended with their nearest pair\n");
	}

	// SWA's Death Star surface: cells of a few models round the camera, the list's order changing as
	// the camera crosses into the next cell (one leaving it, one coming in). Each cell pairs with
	// the older copy the camera's move carries onto it, the new one is moved with the camera, and
	// all of them blend by one move (cells drawn from their own pairs' moves cracked apart); a
	// ship of its own model, moving by itself, keeps its pair.
	static void swa_surface_cells()
	{
		model1::regions regions;
		regions.title = game::id{ "swa" };
		model1 m(std::move(regions));
		model1::logic_frame z, a, b;
		z.frame = 8, a.frame = 10, b.frame = 12;
		const double turn = 2 * M_PI / 180, T[3] = { 40, -20, -300 };
		const double c = std::cos(turn), s = std::sin(turn);
		auto move = [&](model1::slot sl, int times) {   // the camera's move: x' = R x + T, `times` over
			for (int t = 0; t < times; t++) {
				float out[12];
				for (int col = 0; col < 4; col++) {
					const float *v = sl.mat + 3 * col;
					out[3 * col + 0] = float(c * v[0] + s * v[2] + (col == 3 ? T[0] : 0));
					out[3 * col + 1] = float(v[1] + (col == 3 ? T[1] : 0));
					out[3 * col + 2] = float(-s * v[0] + c * v[2] + (col == 3 ? T[2] : 0));
				}
				std::copy_n(out, 12, sl.mat);
			}
			return sl;
		};
		std::vector<model1::slot> cells;
		for (int i = 0; i < 30; i++) {
			model1::slot sl;
			sl.polys = { 0x300u + uint32_t(i % 3) };
			sl.mat[9] = float((i % 6 - 2.5) * 3000), sl.mat[10] = float(-400 - (i % 4) * 600), sl.mat[11] = float(800 + (i / 6) * 3500);
			cells.push_back(sl);
		}
		model1::slot ship;
		ship.polys = { 0x900 };
		ship.mat[9] = 200, ship.mat[10] = -100, ship.mat[11] = 2000;
		model1::slot stars;
		stars.polys = { 0x10 };
		for (auto *lf : { &z, &a }) lf->slots.push_back(stars);
		for (size_t i = 0; i < cells.size(); i++) z.slots.push_back(cells[i]), a.slots.push_back(move(cells[i], 1));
		z.slots.push_back(ship), a.slots.push_back(move(ship, 1));
		// the newer list in another order: cell 4 left it, one came in at the back, of a model in use
		b.slots.push_back(move(stars, 0));
		for (size_t i = 5; i < cells.size(); i++) b.slots.push_back(move(cells[i], 2));
		for (size_t i = 0; i < 4; i++) b.slots.push_back(move(cells[i], 2));
		model1::slot arrived;
		arrived.polys = { 0x301 };
		arrived.mat[9] = -9000, arrived.mat[10] = -400, arrived.mat[11] = 21000;
		b.slots.push_back(move(arrived, 2));
		model1::slot ship_b = move(ship, 2);
		ship_b.mat[9] += 60;   // (its own move, 3% of its depth)
		b.slots.push_back(ship_b);
		m.blend_slots(z, a, 0.5);
		m.blend_slots(a, b, 0.5);
		assert(!m.m_blend_cut && m.m_camera_sure && !m.m_camera_cut);
		for (size_t i = 0; i < b.slots.size(); i++) assert(m.m_blend_ok[i]);
		// every cell from its own older place (the camera's move undone), every one blended by one
		// move: the turn halfway back, from the newer place
		const double ch = std::cos(-turn / 2), sh = std::sin(-turn / 2);
		const size_t first = 1, last = b.slots.size() - 2;
		for (size_t i = first; i <= last; i++) {
			const float *pb = b.slots[i].mat, *from = m.m_blend_from[i].data();
			const double x = pb[9] - T[0], y = pb[10] - T[1], zz = pb[11] - T[2];
			assert(std::fabs(from[9] - (c * x - s * zz)) < 0.5 && std::fabs(from[10] - y) < 0.5 && std::fabs(from[11] - (s * x + c * zz)) < 0.5);
			const float *b0 = b.slots[first].mat, *m0 = m.m_blend[first].data(), *mi = m.m_blend[i].data();
			const double dx = pb[9] - b0[9], dy = pb[10] - b0[10], dz = pb[11] - b0[11];
			assert(std::fabs((mi[9] - m0[9]) - (ch * dx + sh * dz)) < 0.5);
			assert(std::fabs((mi[10] - m0[10]) - dy) < 0.5);
			assert(std::fabs((mi[11] - m0[11]) - (-sh * dx + ch * dz)) < 0.5);
		}
		const size_t s_i = b.slots.size() - 1;
		for (int k = 0; k < 12; k++) assert(m.m_blend_from[s_i][k] == a.slots.back().mat[k]);
		// a fit most slots are off (the trench's walls, a move one cell along fitting a third of
		// them): not the camera's, nothing moved by it; each blends from its own pair
		{
			model1::regions tr;
			tr.title = game::id{ "swa" };
			model1 t(std::move(tr));
			model1::logic_frame tb;
			tb.frame = 12;
			tb.slots.push_back(stars);
			for (size_t i = 0; i < cells.size(); i++) {
				model1::slot sl = move(cells[i], 2);
				if (i % 3) sl.mat[9] += float((i % 2 ? 0.08 : -0.08) * sl.mat[11]);
				tb.slots.push_back(sl);
			}
			model1::logic_frame ta = a;
			ta.slots.pop_back();
			model1::logic_frame tz = z;
			tz.slots.pop_back();
			t.blend_slots(tz, ta, 0.5);
			t.blend_slots(ta, tb, 0.5);
			assert(!t.m_cam_carried);
			int own = 0;
			for (size_t i = 1; i < tb.slots.size(); i++)
				if ((i - 1) % 3 && t.m_blend_ok[i]) {
					for (int k = 0; k < 12; k++) assert(t.m_blend_from[i][k] == ta.slots[i].mat[k]);
					own++;
				}
			assert(own >= 12);
		}
		printf("SWA surface cells: re-ordered and new cells paired by the camera's move, all blended by one move; a ship keeps its pair; a fit most are off moves none\n");
	}

	// A camera swaying (turning back and forth over a degree a list) about a car it keeps in place:
	// its own turn, not evened out, for a while; a steady uneven turn still is.
	static void swaying_camera()
	{
		model1::regions regions;
		regions.title = game::id{ "vr" };
		model1 m(std::move(regions));
		auto at = [](float degrees) {
			model1::logic_frame l;
			l.has_camera = true;
			const glm::mat3 r = glm::mat3_cast(glm::angleAxis(glm::radians(degrees), glm::vec3(0, 1, 0)));
			for (int c = 0; c < 3; c++) for (int e = 0; e < 3; e++) l.camera[c * 3 + e] = r[c][e];
			return l;
		};
		auto off = [](const model1::logic_frame &l) {
			const glm::quat t(l.turn[0], l.turn[1], l.turn[2], l.turn[3]);
			glm::mat3 cam;
			for (int c = 0; c < 3; c++) for (int e = 0; e < 3; e++) cam[c][e] = l.camera[c * 3 + e];
			return glm::degrees(glm::angle(t * glm::inverse(glm::quat_cast(cam))));
		};
		// uneven steady turn: smoothed (off the camera's own), never held
		model1::logic_frame before;
		float yaw = 0, evened = 0;
		for (int i = 0; i < 12; i++) {
			yaw += i % 2 ? 0.1f : 0.9f;
			model1::logic_frame l = at(yaw);
			m.smooth_turn(l, before);
			assert(l.turn_hold == 0);
			assert(off(l) <= 1.01f);
			evened = std::max(evened, off(l));
			before = l;
		}
		assert(evened > 0.1f);
		// swaying: the camera's own turn from the first swing back, and on for 8 lists from the last
		const float sway[] = { 2.8f, -1.6f, -1.3f, 2.6f, 1.2f, -1.7f, -2.4f, -1.8f, -1.8f, -1.8f, -1.8f, -1.8f, -1.8f, -1.8f, -1.8f };
		const int hold[] = { 0, 8, 7, 8, 7, 8, 7, 6, 5, 4, 3, 2, 1, 0, 0 };
		for (size_t i = 0; i < std::size(sway); i++) {
			yaw += sway[i];
			model1::logic_frame l = at(yaw);
			m.smooth_turn(l, before);
			assert(l.turn_hold == hold[i]);
			if (i > 0) assert(off(l) < 0.01f);      // held, then on from the camera's own speed
			before = l;
		}
		// slowing after a swing: evened out from the speed it slows from, never far ahead of it
		{
			model1::logic_frame last = before;
			for (int i = 0; i < 12; i++) {
				model1::logic_frame l = at(yaw += i % 2 ? 2.4f : -2.4f);
				m.smooth_turn(l, last);
				last = l;
			}
			assert(last.turn_hold > 0);
			float most = 0, first = -1;
			for (float step : { -1.8f, -1.8f, -1.8f, -1.8f, -1.8f, -1.2f, -1.2f, -1.2f, -0.77f, -0.74f, -0.68f, -0.45f, -0.38f, -0.3f, -0.19f, -0.19f, -0.08f, 0.05f, -0.12f, -0.04f }) {
				model1::logic_frame l = at(yaw += step);
				m.smooth_turn(l, last);
				if (l.turn_hold == 0 && first < 0) first = off(l);
				if (l.turn_hold == 0) most = std::max(most, off(l));
				last = l;
			}
			assert(last.turn_hold == 0 && first < 0.01f && most < 0.6f);   // (from the swing's speed: 0.4 at once, then 0.71)
		}
		// the cars: one the camera keeps in place (turning unevenly) eased, a quarter a list, to as
		// drawn; one parked on the track turned with the world
		{
			model1::logic_frame last;
			std::vector<float> kept;
			for (int i = 0; i < 12; i++) {
				model1::logic_frame l = at(yaw += i % 2 ? 0.1f : 0.9f);
				glm::mat3 cam;
				for (int c = 0; c < 3; c++) for (int e = 0; e < 3; e++) cam[c][e] = l.camera[c * 3 + e];
				const glm::vec3 parked = cam * glm::vec3(2, 0, 10);
				l.slots.resize(2);
				l.slots[0].owner = 0x400d00;
				l.slots[0].polys = { 1 };
				l.slots[0].mat[11] = 10;
				l.slots[1].owner = 0x401000;
				l.slots[1].polys = { 1 };
				for (int k = 0; k < 3; k++) l.slots[1].mat[9 + k] = parked[k];
				m.smooth_turn(l, last);
				m.follow_records(l, last);
				assert(l.follows.size() == (i ? 2u : 0u));
				if (i) {
					kept.push_back(l.follows[0].weight);
					assert(l.follows[1].owner == 0x401000 && l.follows[1].weight == 0);
				}
				last = l;
			}
			for (size_t i = 1; i < kept.size(); i++) assert(kept[i] == std::min(kept[i - 1] + 0.25f, 1.0f) || kept[i] == kept[i - 1]);
			assert(kept.back() == 1);
		}
		// a start from still: not a swing back
		before = model1::logic_frame{};
		for (float step : { 0.0f, 0.05f, -1.2f }) {
			yaw += step;
			model1::logic_frame l = at(yaw);
			m.smooth_turn(l, before);
			assert(l.turn_hold == 0);
			before = l;
		}
	}

	// The attract drive past the bridge, the camera turning unevenly, then swaying about the lead car
	// and slowing: on screen, the cars move on evenly as the game has them, never back and forth.
	static void swaying_car(const char *dir)
	{
		model1 m(load_regions(dir, game::id{ "vr" }));
		game::vr::set_extended_draw_distance(m, true);
		m.set_smooth(true);
		m.set_aspect(16.0f / 9);
		m.reset();
		const uint32_t cars[] = { 0x400d00, 0x401c00, 0x401980 };   // the lead car, one behind, one close by
		std::vector<float> xs[3];
		int held = 0, followed = 0;
		for (int f = 1; f <= 1578; f++) {
			m.run_frame();
			if (f < 1440) continue;
			const auto &b = m.m_logic[1];
			held += b.turn_hold > 0;
			for (const auto &r : b.follows) followed += r.owner == cars[0] && r.weight == 1;
			for (double fraction : { 0.0, 0.5 }) {
				m.render_smooth(double(m.frame_number() - 1) + fraction);
				for (int c = 0; c < 3; c++)
					for (size_t i = 0; i < b.slots.size(); i++)
						if (b.slots[i].owner == cars[c] && !b.slots[i].polys.empty()) {
							assert(i < m.m_blend_ok.size() && m.m_blend_ok[i]);
							const float *x = m.m_blend[i].data();
							assert(x[11] > 1);
							if (c == 0 || f >= 1530) xs[c].push_back(x[9] / x[11]);
							break;
						}
			}
		}
		assert(held > 10 && followed > 30 && xs[0].size() == 278 && xs[1].size() == 98 && xs[2].size() == 98);
		// the lead car right to 0.32 of the depth and back to 0.23; the others, out of the swing, as the
		// game has them (the turn smoothed against them, back and forth: up to 0.0016, 0.0016, 0.0077)
		const float most_allowed[] = { 0.0005f, 0.001f, 0.0025f };
		for (int c = 0; c < 3; c++) {
			int turns = 0;
			float most = 0;
			for (size_t i = 2; i < xs[c].size(); i++) {
				const float was = xs[c][i - 1] - xs[c][i - 2], now = xs[c][i] - xs[c][i - 1];
				most = std::max(most, std::fabs(now - was));
				turns += was * now < 0;
			}
			printf("VR swaying camera: car %x moves on evenly (%zu pictures, speed change up to %.5f, %d turns)\n", cars[c], xs[c].size(), most, turns);
			assert(most < most_allowed[c] && turns <= 1);
		}
		printf("VR swaying camera: %d lists the camera's own turn, %d the lead car as drawn\n", held, followed);
	}

	static void culled_records()
	{
		model1::regions regions;
		regions.title = game::id{ "vr" };
		model1 m(std::move(regions));
		model1::logic_frame previous, current;
		previous.frame = 470;
		current.frame = 472;
		previous.has_camera = current.has_camera = true;
		for (int i : { 0, 4, 8 }) previous.camera[i] = current.camera[i] = 1;
		current.camera[9] = 2;
		model1::slot car;
		car.owner = 0x401480;
		car.polys = { 0x1234 };
		car.objects = { { 0x40000, 0x1234, 0 } };
		car.mat[9] = 10;
		car.mat[11] = 20;
		car.visibility = game::record_visibility{ car.owner, { 10, 0, 20 }, false };
		previous.slots = { car };
		current.visibility = { { car.owner, { 11, 0, 20 }, true } };
		m.m_gameplay.record_visibility = [](uint32_t) -> std::vector<game::record_visibility> {
			assert(false); // Replay must not sample the next update's RAM.
			return {};
		};
		const auto ram = m.work_ram(), nvram = m.nvram(), rom = m.program_rom();
		m.retain_culled_records(current, previous);
		assert(current.slots.size() == 1 && current.slots[0].retained);
		assert(current.slots[0].owner == car.owner);
		assert(current.slots[0].objects.front().polygon == 0x1234);
		assert(std::fabs(current.slots[0].mat[9] - 13) < .0001f);
		assert(std::fabs(current.slots[0].mat[11] - 20) < .0001f);
		assert(m.work_ram() == ram && m.nvram() == nvram && m.program_rom() == rom);
		model1::logic_frame next = current;
		next.frame += 2;
		next.slots.clear();
		m.retain_culled_records(next, current);
		assert(next.slots.empty());
		current.slots = { car };
		m.retain_culled_records(current, previous);
		assert(current.slots.size() == 1 && !current.slots[0].retained);
		current.slots.clear();
		current.visibility[0].culled = false;
		m.retain_culled_records(current, previous);
		assert(current.slots.empty());
		current.visibility.clear();
		m.retain_culled_records(current, previous);
		assert(current.slots.empty());
		current.visibility = { { car.owner, { 11, 0, 20 }, true } };
		current.camera[9] = 20;
		m.retain_culled_records(current, previous);
		assert(current.slots.empty());
		current.camera[9] = 2;
		current.visibility[0].position[0] = 100;
		m.retain_culled_records(current, previous);
		assert(current.slots.empty());
		current.visibility[0].position[0] = 11;
		current.frame = 476;
		m.retain_culled_records(current, previous);
		assert(current.slots.empty());
	}

	static void extra_scenery()
	{
		model1::regions regions;
		regions.title = game::id{ "vr" };
		model1 m(std::move(regions));
		for (bool smooth : { false, true }) {
			m.set_smooth(smooth);
			bool pending = true;
			m.m_gameplay.matrix_objects = [&](uint32_t &) {
				if (!pending) return std::vector<game::render_object>{};
				pending = false;
				return std::vector<game::render_object>{ { 0x40000, 0x1234, 0 } };
			};
			m.list_written(0, 8);
			assert(m.m_list_extra[0].at(8).front().polygon == 0x1234);
			m.list_written(0, 9);
			assert(m.m_list_extra[0].count(8) == 1 && m.m_list_extra[0].count(9) == 0);
			model1::logic_frame frame;
			frame.list.resize(0x8000);
			frame.list[8] = 0xb;
			frame.list[34] = 0xf;
			m.m_picture_buffer = 0;
			m.index_objects(frame);
			assert(frame.slots.size() == 2 && frame.slots[1].extra.size() == 1);
			assert(frame.slots[1].polys == std::vector<uint32_t>{ 0x1234 });
			assert(frame.objects == 0);
			m.list_written(0, 8);
			assert(m.m_list_extra[0].empty());
			assert(frame.slots[1].extra.front().polygon == 0x1234);
			// a write naming an earlier matrix command: its extras replaced, the written word's dropped
			m.m_gameplay.matrix_objects = [&](uint32_t &word) {
				if (word != 20) return std::vector<game::render_object>{};
				word = 8;
				return std::vector<game::render_object>{ { 0x40000, 0x5678, 0 } };
			};
			m.list_written(0, 8);
			m.m_list_extra[0][20] = { { 0x40000, 0x9abc, 0 } };
			m.list_written(0, 20);
			assert(m.m_list_extra[0].size() == 1 && m.m_list_extra[0].at(8).front().polygon == 0x5678);
			m.m_gameplay.matrix_objects = [&](uint32_t &word) {
				word = 8;
				return std::vector<game::render_object>{};
			};
			m.list_written(0, 21);
			assert(m.m_list_extra[0].empty());
			m.m_gameplay.matrix_objects = {};
		}
	}

	static void scenery_records()
	{
		model1::regions regions;
		regions.title = game::id{ "vr" };
		model1 m(std::move(regions));
		m.set_smooth(true);
		m.cpu().pc_ref() = 0xff8500;
		m.cpu().reg_ptr()[19] = 0x401700;
		m.cpu().reg_ptr()[25] = 0x40a300;
		m.cpu().reg_ptr()[31] = 0x500100;
		for (uint32_t caller : { 0xffddd4, 0xffddeb, 0xffde6e, 0xffdedc, 0xffdf42, 0xffcec9, 0xff8470 }) {
			m.write32(0x500100, caller);
			m.list_written(0, 8);
			assert(m.m_list_owner[0][8] == (caller == 0xff8470 ? 0x401700 : 0x40a300));
		}
		m.cpu().pc_ref() = 0xff85d8;
		m.list_written(1, 8);
		assert(m.m_list_owner[1][8] == 0x40a300);
		m.cpu().pc_ref() = 0xff8500;
		m.cpu().reg_ptr()[19] = 0x2e0000;
		m.list_written(0, 8);
		assert(m.m_list_owner[0][8] == 1);

		model1::logic_frame a, b;
		a.frame = 0;
		b.frame = 2;
		a.has_camera = b.has_camera = true;
		for (int i : { 0, 4, 8 }) a.camera[i] = b.camera[i] = 1;
		b.camera[9] = 1;
		model1::slot car, palm;
		car.owner = 0x401700;
		car.polys = { 0x13b908 };
		car.mat[11] = 20;
		palm.owner = 0x40a300;
		palm.polys = { 0x172c3f };
		palm.mat[9] = 10;
		palm.mat[11] = 20;
		a.slots = { car, palm };
		car.mat[9] = 6;
		palm.mat[9] = 11;
		palm.polys = { 0x172fa6 };
		model1::slot wheel = car;
		wheel.polys = { 0x128975 };
		b.slots = { car, palm, wheel };
		m.blend_slots(a, b, .5);
		assert(m.m_blend_ok[1]);
		assert(std::fabs(m.m_blend[1][9] - 10.5f) < .0001f);
		a.slots[1].owner = b.slots[1].owner = car.owner;
		m.blend_slots(a, b, .5);
		assert(std::fabs(m.m_blend[1][9] - 10.5f) > 2);
		m.set_smooth(false);
		m.cpu().set_reg(19, 0x401700);
		m.list_written(0, 40);
		assert(m.m_list_owner[0][40] == 0x401700);
	}

	static void scenery_motion(const char *dir, bool extended)
	{
		model1 m(load_regions(dir, game::id{ "vr" }));
		game::vr::set_extended_draw_distance(m, extended);
		m.set_smooth(true);
		m.reset();
		std::set<uint32_t> palm_records;
		int checked = 0, views = 0;
		m.on_instruction = [&](uint32_t pc) {
			if (pc != 0xff8500) return;
			const uint32_t poly = m.read32(m.cpu().reg(0));
			if (poly >= 0x16b000 && poly < 0x180000) palm_records.insert(m.cpu().reg(25));
		};
		for (int f = 1; f <= 2000; f++) {
			m.run_frame();
			const auto &a = m.m_logic[0], &b = m.m_logic[1];
			if (!a.has_camera || !b.has_camera || b.frame != f - 1) continue;
			m.blend_slots(a, b, .5);
			if (m.m_blend_cut) continue;
			bool palms = false;
			for (size_t i = 0; i < b.slots.size(); i++) {
				const auto &s = b.slots[i];
				if (s.polys.empty() || s.polys[0] < 0x16b000 || s.polys[0] >= 0x180000) continue;
				assert(palm_records.count(s.owner));
				palms = true;
				std::vector<size_t> old_parts, new_parts;
				for (size_t j = 0; j < a.slots.size(); j++)
					if (a.slots[j].owner == s.owner && !a.slots[j].polys.empty()) old_parts.push_back(j);
				for (size_t j = 0; j < b.slots.size(); j++)
					if (b.slots[j].owner == s.owner && !b.slots[j].polys.empty()) new_parts.push_back(j);
				if (old_parts.size() != new_parts.size()) continue;
				const auto part = std::find(new_parts.begin(), new_parts.end(), i) - new_parts.begin();
				const auto &old = a.slots[old_parts[part]];
				assert(m.m_blend_ok[i]);
				for (int k = 0; k < 12; k++)
					assert(std::fabs(m.m_blend_from[i][k] - old.mat[k]) < .0001f);
				checked++;
			}
			if (palms) views++;
		}
		assert(views > 100 && checked > 500 && palm_records.size() > 1);
		printf("VR palm motion (%s): %d views, %d parts paired to their own scenery records\n",
			extended ? "extended" : "arcade", views, checked);
	}

	static void object_bounds()
	{
		model1::regions regions;
		regions.title = game::id{ "swa" };
		regions.polygons.resize(17 * sizeof(uint32_t));
		model1 m(std::move(regions));
		m.m_quadpt = m.m_quaddb.get();
		m.m_pointpt = m.m_pointdb.get();
		m.m_view->set_viewport(248, 192, 0, 495, 0, 383);
		m.m_view->set_zoom(128, 128);
		m.m_view->viewx = 0;
		const auto check = [&](uint32_t texture, uint32_t polygon, uint32_t size, size_t quads = 0) {
			m.m_quadpt = m.m_quaddb.get();
			m.m_pointpt = m.m_pointdb.get();
			float old_z = 0;
			m.push_object(texture, polygon, size, old_z);
			assert(m.m_quadpt == m.m_quaddb.get() + quads);
			assert(m.m_pointpt <= m.m_pointdb.get() + 4);
			assert(m.m_view->viewx == 0);
		};
		check(0x7d082d7d, 0x082d7d08, 0);
		check(0x2d000520, 0xd0f4212d, 0);
		check(0x3ffff, 0, 0);
		check(0x3fffe, 0, 0);
		check(0x100000, 0, 0);
		check(0x40000, 0, 0x1000000);
		check(0x40000, 12, 0);
		check(0x40000, 0x800000 | (0x400000 - 5), 0);
		check(0x40000, 11, 0);
		check(0x40000, 0x800000 | (0x400000 - 6), 0);
		auto *rom = reinterpret_cast<uint32_t *>(m.m_regions.polygons.data());
		rom[6] = 0x1001;
		check(0xfffff, 0, 0);
		rom[6] = 1;
		rom[16] = 1;
		check(0x40000, 0, 0);
		rom[16] = 0;
		check(0x40000, 0, 0);
		// The final word may terminate a stream without another complete record.
		check(0xffffe, 0, 1);
		rom[6] = 0;
		check(0x40000, 0, 0);
		std::fill_n(m.m_view->translation, 12, 0.0f);
		m.m_view->translation[0] = m.m_view->translation[4] = m.m_view->translation[8] = 1;
		m.m_view->ayyc = 1;
		m.m_view->ayys = 0;
		const std::array<uint32_t, 17> visible{
			0xbf800000, 0xbf800000, 0x3f800000,
			0xbf800000, 0x3f800000, 0x3f800000,
			0x5201, 0, 0, 0x3f800000,
			0x3f800000, 0xbf800000, 0x3f800000,
			0x3f800000, 0x3f800000, 0x3f800000, 0
		};
		std::copy(visible.begin(), visible.end(), rom);
		check(0xffffe, 0, 0, 1);
		check(0x3ffff, 0, 0, 1);
		rom[6] = 0x4201;
		check(0x3ffff, 0, 0);
		rom[6] = 0x5201;
		check(0x7d082d7d, 0x082d7d08, 0);
		check(0xffffe, 0, 1, 1);
		puts("Model 1 object address and polygon boundary tests passed");
	}

	static void swa_destroyer_bounds(const char *path)
	{
		const game::id title{ "swa" };
		model1 m(load_regions(path, title));
		m.reset();
		auto script = game::make_auto_play(title, "progression", 12345, false);
		assert(script);
		const auto press = [&](uint8_t button) {
			m.m_inputs[0] = uint8_t(~button);
			for (int f = 0; f < 10; ++f) m.run_frame();
			m.m_inputs[0] = 0xff;
			for (int f = 0; f < 60; ++f) m.run_frame();
		};
		for (int f = 0; f < 1300; ++f) m.run_frame();
		press(4);
		for (int i = 0; i < 7; ++i) press(8);
		press(4);
		for (int row = 0; row < 3; ++row) {
			press(8);
			for (int i = 0; i < 3; ++i) press(4);
		}
		press(8); press(8); press(4);
		for (int i = 0; i < 4; ++i) press(8);
		press(4);
		for (int f = 0; f < 600; ++f) m.run_frame();
		bool malformed = false;
		m.on_instruction = [&](uint32_t pc) {
			if (pc == 0xc197f && m.cpu().reg(0) == 0xd93ce) {
				assert(m.read32(0xd93ce) == 0x082d7d08);
				assert(m.read32(0xd93d2) == 0x7d082d7d);
				malformed = true;
			}
		};
		uint64_t stage = 0;
		for (uint64_t f = 1; f <= 14500; ++f) {
			const bool destroyer = m.read8(0x40f008) == 13 && m.read8(0x40f009) == 5;
			if (destroyer && !stage) stage = f;
			auto in = script->frame(f, m);
			if (destroyer) {
				in.analog[0] = 127;
				in.analog[1] = f - stage < 60 ? 227 : 127;
				in.analog[2] = 28;
			}
			for (size_t c = 0; c < in.analog.size(); ++c)
				if (in.analog[c] >= 0) assert(m.set_analog_input(c, uint8_t(in.analog[c])));
			for (size_t p = 0; p < in.pressed.size(); ++p) m.m_inputs[p] = uint8_t(~in.pressed[p]);
			m.set_drawing(f % 4 == 0 || f % 150 == 0);
			m.run_frame();
		}
		assert(stage && malformed);
		puts("SWA real-input destroyer malformed-object route survived");
	}

	// an extra object's fade scales its polygons' colours toward black
	static void object_fade()
	{
		model1::regions regions;
		regions.title = game::id{ "swa" };
		model1 m(std::move(regions));
		m.m_view->set_viewport(248, 192, 0, 495, 0, 383);
		m.m_view->set_zoom(128, 128);
		float *poly = reinterpret_cast<float *>(m.m_poly_ram.get());
		const float object[] = { -1, -1, 10, 1, -1, 10, 0, 0, 0, -1, 1, 1, 10, -1, 1, 10 };
		std::copy(std::begin(object), std::end(object), poly);
		const uint32_t flags = 0x00004000 | (1 << 8) | 1;   // a two-sided quad linked to the first edge
		std::memcpy(poly + 6, &flags, 4);
		m.m_tgp_ram[0] = 0x400 | 1;   // unlit: full intensity
		m.m_paletteram16[0x1001] = 0x7fff;
		for (int channel : { 0, 0x2000, 0x4000 }) m.m_color_xlat[(31 << 8) | 0x3f | channel] = 31 << 3;
		const auto colour = [&](float fade) {
			m.m_quadpt = m.m_quaddb.get();
			m.m_pointpt = m.m_pointdb.get();
			float old_z = 0;
			m.m_object_fade = fade;
			m.push_object(0x40000, 0x800000, 1, old_z);
			m.m_object_fade = 1;
			assert(m.m_quadpt > m.m_quaddb.get());
			return m.m_quaddb[0].col & 0xffffff;
		};
		const uint32_t lit = colour(1), half = colour(.5f), none = colour(0);
		assert(lit != 0 && none == 0);
		for (int sh = 0; sh <= 16; sh += 8)
			assert(std::abs(int((half >> sh) & 0xff) - int(std::lround(((lit >> sh) & 0xff) * .5f))) <= 1);
	}

	static void foreground_order()
	{
		model1::regions regions;
		regions.title = game::id{ "vr" };
		model1 m(std::move(regions));
		std::array<model1::quad_t, 5> quads;
		quads[0].z = -1;
		quads[1].z = 10;
		quads[2].z = 0;
		quads[3].z = .1f;
		quads[3].foreground = true;
		quads[4].z = .2f;
		quads[4].foreground = true;
		assert(quads[1].compare(&quads[0]) < 0);
		assert(quads[2].compare(&quads[0]) < 0);
		for (int i = 0; i < 3; i++) {
			assert(quads[i].compare(&quads[3]) < 0);
			assert(quads[3].compare(&quads[i]) > 0);
		}

		assert(quads[4].compare(&quads[3]) < 0);
		std::array<model1::point_t, 4> points;
		m.m_quadpt = m.m_quaddb.get();
		m.m_pointpt = m.m_pointdb.get();
		m.m_view->set_viewport(248, 192, 0, 495, 0, 383);
		m.m_view->set_zoom(128, 128);
		points[0] = { -.2f, -.2f, 1 };
		points[1] = { .2f, -.2f, 1 };
		points[2] = { .2f, .2f, 1 };
		points[3] = { -.2f, .2f, -.1f };
		model1::quad_t q(0xffffff, -.1f, &points[0], &points[1], &points[2], &points[3]);
		q.foreground = true;
		m.fclip_push_quad(0, q);
		assert(m.m_quadpt > m.m_quaddb.get());
		for (auto *p = m.m_quaddb.get(); p < m.m_quadpt; p++)
			assert(p->foreground && p->z == -.1f);
	}

	// widescreen: a direct poly across the arcade's whole screen (SWA's hit flash) reaches the wide
	// picture's edges; one touching a single edge stays
	static void wide_direct_overlay()
	{
		model1::regions regions;
		regions.title = game::id{ "swa" };
		model1 m(std::move(regions));
		m.set_aspect(16.0f / 9);
		m.m_wide_frame = true;
		const float ext = float(m.m_ext);
		assert(ext > 0);
		auto direct = [&](std::initializer_list<std::pair<float, float>> corners) {
			m.m_pointpt = m.m_pointdb.get();
			m.m_quadpt = m.m_quaddb.get();
			model1::quad_t q;
			int i = 0;
			for (auto [x, y] : corners) {
				model1::point_t *p = m.m_pointpt++;
				p->fx = (x + ext) * m.m_view->scale;
				p->fy = y * m.m_view->scale;
				p->s.x = int32_t(p->fx);
				q.p[i++] = p;
			}
			*m.m_quadpt++ = q;
			m.widen_direct(m.m_pointdb.get(), m.m_quaddb.get());
			std::vector<float> xs;
			for (model1::point_t *p = m.m_pointdb.get(); p < m.m_pointpt; p++) xs.push_back(p->fx / m.m_view->scale - ext);
			return xs;
		};
		const auto flash = direct({ { 0, -1 }, { 496, -1 }, { 496, 383 }, { 0, 383 } });
		assert(flash[0] == -ext && flash[1] == 496 + ext && flash[2] == 496 + ext && flash[3] == -ext);
		const auto panel = direct({ { 0, 55 }, { 384, 55 }, { 384, 223 }, { 0, 223 } });
		assert(panel[0] == 0 && panel[1] == 384);
		m.m_wide_frame = false;
		const auto narrow = direct({ { 0, -1 }, { 496, -1 }, { 496, 383 }, { 0, 383 } });
		assert(narrow[0] == 0 && narrow[1] == 496);
		m.m_pointpt = m.m_pointdb.get();
		m.m_quadpt = m.m_quaddb.get();
		puts("Model 1 widescreen direct overlay checks passed");
	}

	// widescreen, SWA: an opaque layer isn't stretched; its sides continue a plain edge's colour, else black
	static void unstretched_backdrops()
	{
		constexpr uint32_t KEY = 0xff000000;
		model1::regions regions;
		regions.title = game::id{ "swa" };
		model1 m(std::move(regions));
		m.set_aspect(16.0f / 9);
		m.m_wide_frame = true;
		assert(!m.stretch_backdrops());
		const int ext = m.m_ext, w = model1::SCREEN_W + 2 * ext;
		bitmap_rgb32 px;
		px.allocate(w, model1::SCREEN_H);
		px.fill(KEY);
		for (int y = 0; y < model1::SCREEN_H; y++)
			for (int x = ext; x < ext + model1::SCREEN_W; x++) px.pix(y, x) = 0x00ffffff;
		px.pix(100, ext + model1::SCREEN_W - 1) = 0x00123456;    // art reaching the right edge
		m.fill_backdrop_sides(px);
		for (int y = 0; y < model1::SCREEN_H; y++) {
			assert(px.pix(y, 0) == 0x00ffffff && px.pix(y, ext - 1) == 0x00ffffff);
			assert(px.pix(y, ext + model1::SCREEN_W) == 0 && px.pix(y, w - 1) == 0);
		}
		m.m_wide_frame = false;
		px.fill(KEY);
		px.pix(0, ext) = 0x00ffffff;
		m.fill_backdrop_sides(px);
		assert(px.pix(0, 0) == KEY);

		model1::regions vr;
		vr.title = game::id{ "vr" };
		assert(model1(std::move(vr)).stretch_backdrops());
		puts("Model 1 unstretched widescreen backdrop checks passed");
	}

	// A split-mode layer pair (SWA's opening starfield) drawn wide: the two maps sit side by side as
	// one 1024-pixel strip, so the sides carry on from it instead of staying empty.
	static void wide_split_tiles()
	{
		constexpr int ext = 83, h = 20;
		segas24_tile t;
		std::vector<uint32_t> pens(0x2000);
		for (size_t i = 0; i < pens.size(); i++) pens[i] = uint32_t(i);
		for (int i = 0; i < 16; i++) { t.char_w(16 + i, 0x1111, 0xffff); t.char_w(32 + i, 0x2222, 0xffff); }
		for (int e = 0; e < 0x1000; e++) { t.tile_w(0x2000 + e, 1, 0xffff); t.tile_w(0x3000 + e, 2, 0xffff); }
		t.tile_w(0x5002, 0x200 | h, 0xffff);     // layer 4's scroll, the first map on the left
		t.tile_w(0x5006, 0x4000, 0xffff);        // split side by side
		t.update(pens.data());
		bitmap_rgb32 wide, plain;
		wide.allocate(496 + 2 * ext, 384);
		plain.allocate(496 + 2 * ext, 384);
		wide.fill(0xff000000);
		plain.fill(0xff000000);
		t.m_ox = ext;
		t.draw_wide(wide, 4, segas24_tile::DRAW_OPAQUE, ext);
		t.draw(plain, plain.cliprect(), 4, 0, segas24_tile::DRAW_OPAQUE);
		for (int y = 0; y < 384; y += 50)
			for (int x = -ext; x < 496 + ext; x++) {
				const uint32_t want = (x < h || x >= 512 + h) ? 1 : 2;
				assert(wide.pix(y, x + ext) == want);
				if (x >= 0 && x < 496) assert(plain.pix(y, x + ext) == want);
			}
		puts("Model 1 wide split-mode tile checks passed");
	}

	static void hud_edge_pinning()
	{
		model1::regions regions;
		regions.title = game::id{ "vr" };
		model1 m(std::move(regions));
		m.set_aspect(16.0f / 9);
		m.m_wide_frame = true;
		m.m_ram_b[0x1138] = 0x14;
		assert(m.hud_view_offset(149, 12) == m.m_ext);
		assert(m.hud_view_offset(-232, 134) == -m.m_ext);
		assert(m.hud_view_offset(0, -30) == 0);
		assert(m.hud_edge_pinning_active());

		constexpr uint32_t KEY = 0xff000000;
		const int ext = m.m_ext;
		bitmap_rgb32 pixels;
		pixels.allocate(model1::SCREEN_W + 2 * ext, model1::SCREEN_H);
		pixels.fill(KEY);
		pixels.pix(100, ext + 20) = 0x000000ff;
		pixels.pix(100, ext + model1::SCREEN_W / 2) = 0x0000ff00;
		pixels.pix(100, ext + 400) = 0x00ff0000;
		m.pin_hud_tiles(pixels, 1);
		assert(pixels.pix(100, 20) == 0x000000ff);
		assert(pixels.pix(100, ext + 20) == KEY);
		assert(pixels.pix(100, ext + model1::SCREEN_W / 2) == 0x0000ff00);
		assert(pixels.pix(100, 400 + 2 * ext) == 0x00ff0000);
		assert(pixels.pix(100, ext + 400) == KEY);

		bitmap_rgb32 untouched;
		untouched.allocate(model1::SCREEN_W + 2 * ext, model1::SCREEN_H);
		untouched.fill(KEY);
		untouched.pix(100, ext + 20) = 0x000000ff;
		m.pin_hud_tiles(untouched, 2);
		assert(untouched.pix(100, ext + 20) == 0x000000ff);
		m.m_ram_b[0x1138] = 0x03;
		for (int x = 16; x < 136; x++) untouched.pix(352, ext + x) = 0x00ffff00;
		for (int x = 400; x < 480; x++) untouched.pix(368, ext + x) = 0x0000ffff;
		for (int x = 180; x < 316; x++) untouched.pix(296, ext + x) = 0x00ffffff;
		m.pin_hud_tiles(untouched, 1);
		assert(untouched.pix(100, ext + 20) == 0x000000ff);
		for (int x = 16; x < 136; x++) {
			assert(untouched.pix(352, x) == 0x00ffff00);
			if (ext + x >= 136) assert(untouched.pix(352, ext + x) == KEY);
		}
		for (int x = 400; x < 480; x++) {
			assert(untouched.pix(368, 2 * ext + x) == 0x0000ffff);
			if (ext + x < 2 * ext + 400) assert(untouched.pix(368, ext + x) == KEY);
		}
		for (int x = 180; x < 316; x++) assert(untouched.pix(296, ext + x) == 0x00ffffff);
		m.m_ram_b[0x1138] = 0x14;
		for (int x = 120; x <= 376; x++) untouched.pix(350, ext + x) = 0x00ffffff;
		m.pin_hud_tiles(untouched, 1);
		for (int x = 120; x <= 376; x++) assert(untouched.pix(350, ext + x) == 0x00ffffff);
		m.m_ram_b[0x1138] = 0x12;
		untouched.pix(100, ext + 20) = 0x000000ff;
		m.pin_hud_tiles(untouched, 1);
		assert(untouched.pix(100, 20) == 0x000000ff);
		assert(untouched.pix(100, ext + 20) == KEY);
		assert(m.hud_edge_pinning_active());
		m.m_ram_b[0x1138] = 0x10;
		untouched.pix(100, ext + 20) = 0x000000ff;
		m.pin_hud_tiles(untouched, 1);
		assert(untouched.pix(100, ext + 20) == 0x000000ff);
		assert(!m.hud_edge_pinning_active());

		m.m_ram_b[0x1138] = 0x14;
		m.m_view->set_viewport(248, 192, 0, 495, 0, 383);
		model1::point_t left{ .x = -200 }, center{ .x = 0 }, right{ .x = 200 };
		for (model1::point_t *point : { &left, &center, &right }) m.m_view->project_point_direct(point);
		m.pin_hud_point(&left, m.hud_edge_offset(48, 100));
		m.pin_hud_point(&center, m.hud_edge_offset(230, 266));
		m.pin_hud_point(&right, m.hud_edge_offset(400, 448));
		assert(left.s.x == 48 && center.s.x == 248 + ext && right.s.x == 448 + 2 * ext);
		assert(m.hud_edge_offset(0, model1::SCREEN_W - 1) == 0);
		// A graphic crossing the old per-vertex threshold must receive one uniform translation.
		model1::point_t a{}, b{};
		a.fx = float(ext + 400);
		b.fx = float(ext + 350);
		const float offset = m.hud_edge_offset(350, 400);
		m.pin_hud_point(&a, offset);
		m.pin_hud_point(&b, offset);
		assert(a.fx - b.fx == 50 && offset == ext);
		m.m_ram_b[0x1138] = 0x10;
		model1::point_t unchanged{ .x = -200 };
		m.m_view->project_point_direct(&unchanged);
		const int old_x = unchanged.s.x;
		m.pin_hud_point(&unchanged, m.hud_edge_offset(48, 100));
		assert(unchanged.s.x == old_x);
		for (float aspect : { 16.0f / 10, 16.0f / 9, 21.0f / 9, 32.0f / 9 })
			for (int scale : { 1, 2 })
			for (uint8_t phase : { 0x12, 0x14 }) {
				m.set_aspect(aspect);
				m.set_render_scale(scale);
				m.m_ram_b[0x1138] = phase;
				m.m_view->set_viewport(248, 192, 0, 495, 0, 383);
				auto *points = m.m_pointdb.get();
				auto *quads = m.m_quaddb.get();
				for (int i = 0; i < 8; i++) {
					points[i] = {};
					points[i].x = float((i < 4 ? 300 : 0) + (i % 2) * (i < 4 ? 100 : 496) - 248);
					m.m_view->project_point_direct(&points[i]);
				}
				quads[0] = model1::quad_t(0, 0, points, points + 1, points + 2, points + 3);
				quads[1] = model1::quad_t(0, 0, points + 4, points + 5, points + 6, points + 7);
				m.m_pointpt = points + 8;
				m.m_quadpt = quads + 2;
				const float original_right = points[0].fx, original_curtain = points[4].fx;
				m.pin_hud_direct(points, quads);
				assert(points[0].fx == original_right + m.m_ext * scale);
				assert(points[1].fx - points[0].fx == 100 * scale);
				assert(points[4].fx == original_curtain);
				assert(points[5].fx - points[4].fx == 496 * scale);
			}
	}

	static void native_motion(const char *dir)
	{
		model1 reference(load_regions(dir, game::id{ "vr" })), native(load_regions(dir, game::id{ "vr" }));
		rec::hints map;
		assert(map.read("assets/games/vr/vr_map.txt"));
		rec::attach(native.cpu(), "vr");
		assert(rec::build_native(native.program_rom(), map));
		native.run_cpu = [](int n) { rec::run(n); };
		for (auto *m : { &reference, &native }) {
			m->set_smooth(true);
			m->set_render_scale(2);
			m->set_aspect(16.0f / 9);
			m->reset();
		}
		int pictures = 0;
		for (int f = 1; f <= 1200; f++) {
			reference.run_frame();
			native.run_frame();
			for (int buffer = 0; buffer < 2; buffer++)
				assert(reference.m_list_owner[buffer] == native.m_list_owner[buffer]);
			if (f < 850 || f % 5) continue;
			for (double fraction : { .25, .75 }) {
				reference.render_smooth(double(reference.frame_number()) - fraction);
				native.render_smooth(double(native.frame_number()) - fraction);
				const size_t pixels = size_t(reference.frame_w()) * reference.frame_h();
				assert(std::equal(reference.frame().data(), reference.frame().data() + pixels, native.frame().data()));
				pictures++;
			}
		}
		assert(pictures > 100 && rec::fallback_count() == 0);
		printf("VR native palm motion: %d 2x widescreen pictures identical, zero fallback\n", pictures);
	}

	// GOAL at the end of a race flies in on screen, handed from the player's car record to the still
	// world: smoothed, it goes between its own two places (it used to be put where the camera's move
	// took the world, far off, flashing small between the game's pictures)
	static void goal_fly_in(const char *dir)
	{
		model1 m(load_regions(dir, game::id{ "vr" }));
		m.set_smooth(true);
		m.set_aspect(16.0f / 9);
		m.reset();
		auto play = game::make_auto_play(game::id{ "vr" }, "qualifying", 12345, false);
		assert(play);
		size_t checked = 0, handoffs = 0;
		for (int f = 1; f <= 20100; f++) {
			const auto in = play->frame(f, m);
			for (size_t c = 0; c < in.analog.size(); c++)
				if (in.analog[c] >= 0) assert(m.set_analog_input(c, uint8_t(in.analog[c])));
			for (size_t c = 0; c < in.pressed.size(); c++)
				m.m_inputs[c] = uint8_t(~in.pressed[c]);
			m.run_frame();
			if (f < 19900) continue;
			for (double fraction : { .25, .75 }) {
				m.render_smooth(double(m.frame_number()) - fraction);
				const auto &a = m.m_logic[0], &b = m.m_logic[1];
				auto goal = [](const auto &s) { return !s.polys.empty() && s.polys[0] == 0x101777; };
				auto older = std::find_if(a.slots.begin(), a.slots.end(), goal);
				if (older == a.slots.end() || m.m_blend_cut) continue;
				for (size_t i = 0; i < b.slots.size(); i++) {
					if (!goal(b.slots[i]) || !m.m_blend_ok[i]) continue;
					if (older->owner != b.slots[i].owner) handoffs++;
					const float eps = 1e-3f * std::max(std::fabs(b.slots[i].mat[11]), 1.0f);
					for (int k = 9; k < 12; k++) {
						const float lo = std::min(older->mat[k], b.slots[i].mat[k]), hi = std::max(older->mat[k], b.slots[i].mat[k]);
						if (m.m_blend[i][k] < lo - eps || m.m_blend[i][k] > hi + eps)
							fprintf(stderr, "VR GOAL off its way: frame %d, %d: %f not in %f..%f\n", b.frame, k, m.m_blend[i][k], lo, hi);
						assert(m.m_blend[i][k] >= lo - eps && m.m_blend[i][k] <= hi + eps);
					}
					checked++;
				}
			}
		}
		assert(checked > 100 && handoffs > 0);
		printf("VR GOAL flying in: %zu smoothed pictures between the game's (%zu handoffs)\n", checked, handoffs);
	}

	// The attract pit scene hands the SEGA logo from one car record to another: smoothed, it stays
	// between its two places on screen (it used to move with the new record's car and jump back).
	static void logo_handoff(const char *dir)
	{
		model1 m(load_regions(dir, game::id{ "vr" }));
		m.set_smooth(true);
		m.set_aspect(21.0f / 9);
		m.reset();
		size_t checked = 0, handoffs = 0, notice_pixels = 0;
		for (int f = 1; f <= 2800; f++) {
			m.run_frame();
			if (f < 2600) continue;
			for (double fraction : { .25, .75 }) {
				m.render_smooth(double(m.frame_number()) - fraction);
				assert(m.work_ram()[0x1138] == 0x07 && m.hud_edge_pinning_active());
				if (f == 2700 && fraction == .25) {
					bitmap_rgb32 original;
					original.allocate(model1::SCREEN_W, model1::SCREEN_H);
					original.fill(model1::KEY);
					m.m_tiles.draw(original, original.cliprect(), 1, 0, 0);
					const auto &pinned = m.cached_layer(1, 0).px;
					for (int y = 344; y < model1::SCREEN_H; y++)
						for (int x = 0; x < model1::SCREEN_W; x++) {
							const uint32_t value = original.pix(y, x);
							if (value == model1::KEY || (x >= 160 && x < 360)) continue;
							const int target = x < 160 ? x : x + 2 * m.m_ext;
							assert(pinned.pix(y, target) == value);
							notice_pixels++;
						}
				}
				const auto &a = m.m_logic[0], &b = m.m_logic[1];
				for (size_t i = 0; i < b.slots.size(); i++) {
					if (b.slots[i].polys.empty() || b.slots[i].polys[0] != 0x1031c7 || !m.m_blend_ok[i]) continue;
					auto older = std::find_if(a.slots.begin(), a.slots.end(), [](const auto &s) { return !s.polys.empty() && s.polys[0] == 0x1031c7; });
					if (older == a.slots.end()) continue;
					if (older->owner != b.slots[i].owner) handoffs++;
					const float eps = 1e-3f * std::max(std::fabs(b.slots[i].mat[11]), 1.0f);
					for (int k = 9; k < 12; k++) {
						const float lo = std::min(older->mat[k], b.slots[i].mat[k]), hi = std::max(older->mat[k], b.slots[i].mat[k]);
						if (m.m_blend[i][k] < lo - eps || m.m_blend[i][k] > hi + eps)
							fprintf(stderr, "VR SEGA logo off its way: frame %d, %d: %f not in %f..%f\n", b.frame, k, m.m_blend[i][k], lo, hi);
						assert(m.m_blend[i][k] >= lo - eps && m.m_blend[i][k] <= hi + eps);
					}
					checked++;
				}
			}
		}
		assert(checked > 100 && handoffs > 0 && notice_pixels > 100);
		printf("VR SEGA logo handed between records: %zu smoothed pictures in place (%zu handoffs)\n", checked, handoffs);
	}

	static void logo(const char *dir, bool smooth, bool extended, int scale = 1, float aspect = 4.0f / 3)
	{
		model1 m(load_regions(dir, game::id{ "vr" }));
		game::vr::set_extended_draw_distance(m, extended);
		m.set_render_scale(scale);
		m.set_aspect(aspect);
		m.set_smooth(smooth);
		m.reset();
		const auto *video = m.m_video;
		auto original_video = *video;
		original_video.foreground_objects.clear();
		size_t tested = 0, logo_pixels = 0, originally_hidden = 0;
		for (int f = 1; f <= 3600; f++) {
			m.run_frame();
			if (f < 1700 || (f > 1900 && f < 3350) || f % 5) continue;
			if (smooth) m.render_smooth(double(m.frame_number()) - 0.5);
			const size_t n = size_t(m.frame_w()) * m.frame_h();
			const std::vector<uint32_t> full(m.frame().data(), m.frame().data() + n);
			m.m_video = &original_video;
			m.redraw();
			const std::vector<uint32_t> baseline(m.frame().data(), m.frame().data() + n);
			m.m_video = video;
			auto original = m.m_gameplay.object;
			m.m_gameplay.object = [](uint32_t, uint32_t, uint32_t poly, uint32_t, const float *) {
				game::object_effects effect;
				effect.hidden = poly != 0x1031c7;
				return effect;
			};
			m.redraw();
			const std::vector<uint32_t> isolated(m.frame().data(), m.frame().data() + n);
			m.m_gameplay.object = [](uint32_t, uint32_t, uint32_t, uint32_t, const float *) {
				game::object_effects effect;
				effect.hidden = true;
				return effect;
			};
			m.redraw();
			size_t pixels = 0, hidden = 0;
			for (size_t i = 0; i < n; i++) {
				if (isolated[i] == m.frame().data()[i]) {
					assert(full[i] == baseline[i]);
					continue;
				}
				pixels++;
				if (baseline[i] != isolated[i]) originally_hidden++;
				if (full[i] != isolated[i]) {
					if (!hidden)
						fprintf(stderr, "First hidden logo pixel: %zu,%zu (%06x instead of %06x)\n",
							i % m.frame_w(), i / m.frame_w(), full[i], isolated[i]);
					hidden++;
				}
			}
			m.m_gameplay.object = std::move(original);
			m.redraw();
			if (hidden)
				fprintf(stderr, "VR SEGA logo: frame %d (%s, %s): %zu/%zu pixels hidden\n",
					f, smooth ? "smooth" : "arcade motion", extended ? "extended distance" : "arcade distance", hidden, pixels);
			assert(hidden == 0);
			tested++;
			logo_pixels += pixels;
		}
		assert(tested > 80 && logo_pixels > 10000);
		assert(originally_hidden > 100);
		printf("VR SEGA logo (%s, %s, %dx, %.3f): %zu views, %zu foreground pixels preserved (%zu previously hidden), other pixels unchanged\n",
			smooth ? "smooth" : "arcade motion", extended ? "extended distance" : "arcade distance",
			scale, aspect, tested, logo_pixels, originally_hidden);
	}
	// SWA's free-play attract run: a TIE overtakes the X-wing on the Death Star surface, closing on
	// the camera (TIME 147, game frames 54188-54191). Its parts were moved with the camera as
	// scenery, from a place nearer the camera than either list had it: each list's pictures started
	// too big and shrank, a trail of copies. Its wings, a finer model as it nears, have no older copy.
	static void swa_passing_tie(const char *dir)
	{
		model1 m(load_regions(dir, game::id{ "swa" }));
		m.reset();
		m.set_drawing(false);
		m.set_smooth(true);
		m.set_aspect(64.0f / 27);
		m.set_edge_aa(false);
		for (int i = 0; i < 60 && game::swa::coin_setting(m) < 0; i++) m.run_frame();
		const int64_t coin = game::swa::coin_setting(m);
		assert(coin >= 0);
		game::swa::set_coin_setting(m, game::swa::with_coin_entry(coin, game::swa::COIN_SETTING_FREE_PLAY));
		m.reset();
		game::swa::set_lod(m, false);
		game::swa::set_full_draw_distance(m, true);
		while (m.frame_number() < FROM - 8) m.run_frame();
		m.set_drawing(true);
		std::vector<size_t> blue;
		while (m.frame_number() < 54191) {
			m.run_frame();
			const int f = int(m.frame_number());
			if (f < FROM) continue;
			for (int q = 0; q < 4; q++) {
				m.render_smooth(f - 1 + q / 4.0);
				size_t n = 0;
				const uint32_t *px = m.frame().data();
				for (int y = m.frame_h() / 6; y < m.frame_h(); y++)
					for (int x = 0; x < m.frame_w(); x++) {
						const uint32_t c = px[size_t(y) * m.frame_w() + x];
						n += (c & 255) > 120 && (c >> 16 & 255) < 60 && (c >> 8 & 255) < 60;
					}
				blue.push_back(n);
			}
		}
		// the TIE's wings (blue) fill more of the screen in each picture, never shrinking or leaping
		std::string seq;
		for (size_t n : blue) seq += " " + std::to_string(n);
		printf("SWA passing TIE, wing pixels by picture:%s\n", seq.c_str());
		assert(blue.size() == size_t(54191 - FROM + 1) * 4);
		assert(blue.back() > 10 * blue.front());
		for (size_t i = 1; i < blue.size(); i++) {
			assert(blue[i] * 50 >= blue[i - 1] * 49);
			assert(blue[i] * 3 <= blue[i - 1] * 4 + 3000);
		}
	}
	static constexpr uint64_t FROM = 54188;
	// The same run at TIME 151 (game frames 53947-53949), drawn as at 60 frames a second: one
	// picture a frame, a quarter and three quarters into each pair of lists. A dark polygon lies
	// under the surface, hidden by its cells. The pair's camera move was fitted to cells still
	// paired with other copies of their model, and none followed it, and a cell was paired with
	// another copy a cell along as a ship. The first picture of the pair swept the cells off it:
	// a dark slab at the top of the surface for one picture, as the arcade never shows.
	static void swa_surface_slab(const char *dir)
	{
		model1 m(load_regions(dir, game::id{ "swa" }));
		m.reset();
		m.set_drawing(false);
		m.set_smooth(true);
		m.set_aspect(64.0f / 27);
		m.set_edge_aa(false);
		for (int i = 0; i < 60 && game::swa::coin_setting(m) < 0; i++) m.run_frame();
		const int64_t coin = game::swa::coin_setting(m);
		assert(coin >= 0);
		game::swa::set_coin_setting(m, game::swa::with_coin_entry(coin, game::swa::COIN_SETTING_FREE_PLAY));
		m.reset();
		game::swa::set_lod(m, false);
		game::swa::set_full_draw_distance(m, true);
		while (m.frame_number() < 53940) m.run_frame();
		m.set_drawing(true);
		std::string seq;
		size_t worst = 0;
		while (m.frame_number() < 53956) {
			m.run_frame();
			const int f = int(m.frame_number());
			m.render_smooth(f - 0.5);
			if (f < 53946) continue;
			// the slab's grey, over the surface at the top of the screen, left of the trench
			size_t n = 0;
			const uint32_t *px = m.frame().data();
			const int w = m.frame_w(), h = m.frame_h();
			for (int y = h * 10 / 100; y < h * 16 / 100; y++)
				for (int x = w * 28 / 100; x < w * 38 / 100; x++) {
					const uint32_t c = px[size_t(y) * w + x];
					const int r = c >> 16 & 255, g = c >> 8 & 255, b = c & 255;
					n += r >= 35 && r <= 65 && g >= 35 && g <= 65 && b >= 35 && b <= 65;
				}
			seq += " " + std::to_string(n);
			worst = std::max(worst, n);
		}
		printf("SWA surface at TIME 151, slab pixels by picture:%s\n", seq.c_str());
		assert(worst < 50);
	}

	// At TIME 150 cells of the trench's rim were paired as ships with another copy most of their
	// depth away, beyond the ships' far guard, so they were jumped: drawn a picture unblended.
	static void swa_trench_jump(const char *dir)
	{
		model1 m(load_regions(dir, game::id{ "swa" }));
		m.reset();
		m.set_drawing(false);
		m.set_smooth(true);
		m.set_aspect(64.0f / 27);
		m.set_edge_aa(false);
		for (int i = 0; i < 60 && game::swa::coin_setting(m) < 0; i++) m.run_frame();
		const int64_t coin = game::swa::coin_setting(m);
		assert(coin >= 0);
		game::swa::set_coin_setting(m, game::swa::with_coin_entry(coin, game::swa::COIN_SETTING_FREE_PLAY));
		m.reset();
		game::swa::set_lod(m, false);
		game::swa::set_full_draw_distance(m, true);
		while (m.frame_number() < 54020) m.run_frame();
		m.set_drawing(true);
		std::string seq;
		int total = 0;
		while (m.frame_number() < 54053) {
			m.run_frame();
			const int f = int(m.frame_number());
			m.render_smooth(f - 0.5);
			if (f < 54030) continue;
			seq += " " + std::to_string(m.m_smooth_stats[1]);
			total += m.m_smooth_stats[1];
		}
		printf("SWA trench at TIME 150, jumped slots by picture:%s\n", seq.c_str());
		assert(total == 0);
	}
	// The attract's run over the Death Star in the cockpit (TIME 124): as the camera crossed into the
	// next cell, the surface's list in another order, the camera's move was lost, dozens of its cells
	// drawn unblended for a picture, the surface shaking. Each picture's unblended cells counted.
	static void swa_cockpit_surface(const char *dir)
	{
		model1 m(load_regions(dir, game::id{ "swa" }));
		m.reset();
		m.set_drawing(false);
		m.set_smooth(true);
		m.set_aspect(64.0f / 27);
		m.set_edge_aa(false);
		for (int i = 0; i < 60 && game::swa::coin_setting(m) < 0; i++) m.run_frame();
		const int64_t coin = game::swa::coin_setting(m);
		assert(coin >= 0);
		game::swa::set_coin_setting(m, game::swa::with_coin_entry(coin, game::swa::COIN_SETTING_FREE_PLAY));
		m.reset();
		game::swa::set_lod(m, false);
		game::swa::set_full_draw_distance(m, true);
		while (m.frame_number() < 28280) m.run_frame();
		m.set_drawing(true);
		int worst = 0, worst_frame = 0;
		while (m.frame_number() < 28420) {
			m.run_frame();
			const int f = int(m.frame_number());
			m.render_smooth(f - 0.5);
			if (f < 28290 || m.m_replay_older) continue;   // (the camera's move not known yet; a cut)
			const auto &slots = m.m_logic[1].slots;
			int n = 0;
			for (size_t i = 0; i < slots.size() && i < m.m_blend_ok.size(); i++)
				n += !slots[i].polys.empty() && slots[i].polys[0] >= 0x1020000 && slots[i].polys[0] < 0x1050000 && !m.m_blend_ok[i];
			if (n > worst) worst = n, worst_frame = f;
		}
		printf("SWA cockpit over the Death Star (TIME 124): at most %d surface cells unblended a picture (frame %d)\n", worst, worst_frame);
		assert(worst == 0);
	}
	// In the trench (attract, TIME 159) the walls' places lie nearly in a plane, and the camera's
	// fitted turn came out as their mirror image: blended by it, the walls were turned inside out,
	// culled, and the trench went black for a picture, several times a run.
	static void swa_trench_mirror(const char *dir)
	{
		model1 m(load_regions(dir, game::id{ "swa" }));
		m.reset();
		m.set_drawing(false);
		m.set_smooth(true);
		m.set_aspect(16.0f / 9);
		m.set_edge_aa(false);
		game::swa::set_lod(m, false);
		game::swa::set_full_draw_distance(m, true);
		while (m.frame_number() < 53470) m.run_frame();
		m.set_drawing(true);
		double worst = 0;
		int worst_frame = 0;
		while (m.frame_number() < 53540) {
			m.run_frame();
			const int f = int(m.frame_number());
			m.render_smooth(f - 0.5);
			if (f < 53480) continue;
			const uint32_t *px = m.frame().data();
			const int w = m.frame_w(), h = m.frame_h();
			size_t n = 0;
			for (int y = h / 8; y < h; y++)
				for (int x = 0; x < w; x++) {
					const uint32_t c = px[size_t(y) * w + x];
					n += (c >> 16 & 255) + (c >> 8 & 255) + (c & 255) < 20;
				}
			const double black = double(n) / (size_t(w) * (h - h / 8));
			if (black > worst) worst = black, worst_frame = f;
		}
		printf("SWA trench at TIME 159: at most %.0f%% of the picture black (frame %d)\n", worst * 100, worst_frame);
		assert(worst < 0.3);
	}

	// The third-person asteroid run (attract, free play, TIME 158): the Full fleet's and asteroids'
	// copies hang on the stars' matrix, its own place passing the camera's plane. Taken for a fast
	// move, or paired by the camera's move (no copies), the copies weren't blended: the star
	// cruisers stepped every other picture. Each picture's copies on the stars' matrix are blended.
	static void swa_fleet_carrier(const char *dir)
	{
		model1 m(load_regions(dir, game::id{ "swa" }));
		m.reset();
		m.set_drawing(false);
		m.set_smooth(true);
		m.set_aspect(16.0f / 9);
		m.set_edge_aa(false);
		for (int i = 0; i < 60 && game::swa::coin_setting(m) < 0; i++) m.run_frame();
		const int64_t coin = game::swa::coin_setting(m);
		assert(coin >= 0);
		game::swa::set_coin_setting(m, game::swa::with_coin_entry(coin, game::swa::COIN_SETTING_FREE_PLAY));
		m.reset();
		game::swa::set_lod(m, false);
		game::swa::set_full_draw_distance(m, true);
		while (m.frame_number() < 34262) m.run_frame();
		m.set_drawing(true);
		int unblended = 0, pictures = 0;
		std::string at;
		while (m.frame_number() < 34300) {
			m.run_frame();
			const int f = int(m.frame_number());
			m.render_smooth(f - 0.5);
			if (f < 34270 || m.m_replay_older) continue;
			const auto &slots = m.m_logic[1].slots;
			size_t c = 0;
			for (size_t i = 0; i < slots.size(); i++) if (slots[i].extra.size() > slots[c].extra.size()) c = i;
			assert(slots[c].extra.size() > 100);
			pictures++;
			if (c >= m.m_blend_ok.size() || !m.m_blend_ok[c] || !m.m_extra_from[c]) unblended++, at += " " + std::to_string(f);
		}
		printf("SWA asteroid run (TIME 158): fleet copies unblended in %d of %d pictures%s\n", unblended, pictures, at.c_str());
		assert(pictures > 10 && unblended == 0);
	}

	// The same run's start (TIME 178 to 172), the top of the screen. The destroyer manager
	// reordered its copies' sources between lists: a star cruiser's copy, paired by its key with
	// another cruiser 44000 units off, swept across the top. And the camera flew through a big
	// asteroid, its faces behind the eye in one list, in front in the next: blended, they came out
	// just by the eye, a brown face over the top half for a picture. Each copy is paired with a
	// cruiser near it; the picture between the asteroid's lists has no more of it than the lists.
	static void swa_asteroid_top_edge(const char *dir)
	{
		model1 m(load_regions(dir, game::id{ "swa" }));
		m.reset();
		m.set_drawing(false);
		m.set_smooth(true);
		m.set_aspect(16.0f / 9);
		m.set_edge_aa(false);
		for (int i = 0; i < 60 && game::swa::coin_setting(m) < 0; i++) m.run_frame();
		const int64_t coin = game::swa::coin_setting(m);
		assert(coin >= 0);
		game::swa::set_coin_setting(m, game::swa::with_coin_entry(coin, game::swa::COIN_SETTING_FREE_PLAY));
		m.reset();
		game::swa::set_lod(m, false);
		game::swa::set_full_draw_distance(m, true);
		while (m.frame_number() < 33460) m.run_frame();
		m.set_drawing(true);
		float farthest = 0;
		int pictures = 0, mispaired = 0;
		while (m.frame_number() < 33500) {
			m.run_frame();
			m.render_smooth(m.frame_number() - 0.5);
			if (m.m_replay_older) continue;
			const auto &slots = m.m_logic[1].slots;
			size_t c = 0;
			for (size_t i = 0; i < slots.size(); i++) if (slots[i].extra.size() > slots[c].extra.size()) c = i;
			if (c >= m.m_blend_ok.size() || !m.m_blend_ok[c] || !m.m_extra_from[c]) continue;
			pictures++;
			const auto &to = slots[c].extra, &from = *m.m_extra_from[c];
			const auto &pairs = m.m_extra_pair[c];
			for (size_t k = 0; k < to.size() && k < pairs.size(); k++) {
				if (pairs[k] < 0) continue;
				const auto &tb = *to[k].transform, &ta = *from[pairs[k]].transform;
				const float d = std::hypot(tb[9] - ta[9], tb[10] - ta[10], tb[11] - ta[11]);
				float nearest = d;
				for (const auto &e : from)
					if (e.transform && e.polygon == to[k].polygon)
						nearest = std::min(nearest, std::hypot(tb[9] - (*e.transform)[9], tb[10] - (*e.transform)[10], tb[11] - (*e.transform)[11]));
				if (d > 4 * nearest + 100) mispaired++, farthest = std::max(farthest, d);
			}
		}
		printf("SWA asteroid run (TIME 178): %d copies paired past a nearer copy of their model (up to %.0f units) over %d pictures\n", mispaired, farthest, pictures);
		m.set_drawing(false);
		while (m.frame_number() < 33836) m.run_frame();
		m.set_drawing(true);
		int brown = 0;
		while (m.frame_number() < 33843) {
			m.run_frame();
			m.render_smooth(m.frame_number() - 0.75);
			m.render_smooth(m.frame_number() - 0.25);
		}
		m.render_smooth(33842.25);
		const int w = m.frame_w(), h = m.frame_h();
		for (int y = 0; y < h / 3; y++)
			for (int x = 0; x < w; x++) {
				const uint32_t p = m.frame().data()[size_t(y) * w + x];
				const int r = (p >> 16) & 255, g = (p >> 8) & 255, b = p & 255;
				brown += r > 70 && r < 200 && r * 10 > g * 13 && g > b;
			}
		printf("SWA asteroid run (TIME 172): asteroid over the top third %d pixels (lists up to ~38000)\n", brown);
		assert(pictures > 10 && mispaired == 0 && brown < 45000);
	}


	// The Death Star demo (TIME 175), 16:9: an X-wing passing 300 units to the left of the camera,
	// seen only at the wide screen's edge, its z 0 to 200. Its move each list, 5% of its distance,
	// was over 60% of its z: taken for a jump, it stood still a list and leapt, over and over (and
	// so did the ships and cells at both edges, 780 of the run's 1120 jumps). Each picture blends it.
	static void swa_side_ship(const char *dir)
	{
		model1 m(load_regions(dir, game::id{ "swa" }));
		m.reset();
		m.set_drawing(false);
		m.set_smooth(true);
		m.set_aspect(16.0f / 9);
		m.set_edge_aa(false);
		game::swa::set_lod(m, false);
		game::swa::set_full_draw_distance(m, true);
		while (m.frame_number() < 52505) m.run_frame();
		m.set_drawing(true);
		int pictures = 0, unblended = 0;
		std::string at;
		while (m.frame_number() < 52530) {
			m.run_frame();
			const int f = int(m.frame_number());
			for (double k : { 0.25, 0.75 }) {
				m.render_smooth(f - 1 + k);
				if (f < 52512) continue;
				const auto &slots = m.m_logic[1].slots;
				for (size_t i = 0; i < slots.size(); i++) {
					if (slots[i].polys.empty() || slots[i].polys[0] != 0x108764c || slots[i].mat[9] > -200) continue;
					pictures++;
					if (i >= m.m_blend_ok.size() || !m.m_blend_ok[i]) unblended++, at += " " + std::to_string(f);
				}
			}
		}
		printf("SWA Death Star demo (TIME 175), an X-wing at the screen's left edge: unblended in %d of %d pictures%s\n", unblended, pictures, at.c_str());
		assert(pictures > 20 && unblended == 0);
	}

	// each list's surface cells within `slope_x`, `slope_y` of the view (Full, LOD off), by the
	// list's frame, over the frame ranges given
	using swa_cell = std::tuple<uint32_t, int, int, int>;
	static std::map<int, std::set<swa_cell>> swa_cells_in_view(const char *dir, float aspect,
		std::initializer_list<std::pair<int, int>> ranges, float slope_x, float slope_y)
	{
		model1 m(load_regions(dir, game::id{ "swa" }));
		m.reset();
		m.set_drawing(false);
		m.set_smooth(true);
		m.set_aspect(aspect);
		m.set_edge_aa(false);
		game::swa::set_lod(m, false);
		game::swa::set_full_draw_distance(m, true);
		std::map<int, std::set<swa_cell>> lists;
		for (auto [from, to] : ranges) {
			m.set_drawing(false);
			while (m.frame_number() < uint64_t(from)) m.run_frame();
			m.set_drawing(true);
			while (m.frame_number() < uint64_t(to)) {
				m.run_frame();
				m.render_smooth(m.frame_number() - 0.5);
				auto &out = lists[m.m_logic[1].frame];
				const auto add = [&](uint32_t polygon, const float *at) {
					if (!m.m_video->still_model(polygon) || at[2] < 100 || std::fabs(at[0]) > slope_x * at[2] || std::fabs(at[1]) > slope_y * at[2]) return;
					out.emplace(polygon, int(std::lround(at[0] / 4)), int(std::lround(at[1] / 4)), int(std::lround(at[2] / 4)));
				};
				for (const auto &s : m.m_logic[1].slots) {
					for (uint32_t p : s.polys) add(p, &s.mat[9]);
					for (const auto &e : s.extra) {
						if (!e.transform) continue;
						const auto &t = *e.transform;
						float at[3];
						for (int r = 0; r < 3; r++) at[r] = s.mat[9 + r] + s.mat[r] * t[9] + s.mat[3 + r] * t[10] + s.mat[6 + r] * t[11];
						add(e.polygon, at);
					}
				}
			}
		}
		return lists;
	}

	// The Death Star demo's low flight (TIME 145-140), draw distance Full: 16:9 left out far cells
	// 4:3 draws, in the middle of the picture. The far layer's side cells past the last band the game
	// draws a cell in were never attached, and the far ring started past the farthest of the merged
	// headings' lists, so cells nearer than it that no list has were drawn by none: holes in the
	// distance that came and went as the heading turned. Every surface cell 4:3 draws in view is
	// drawn at 16:9 too.
	static void swa_wide_far_cells(const char *dir)
	{
		const std::initializer_list<std::pair<int, int>> ranges = { { 53798, 53806 }, { 54290, 54298 }, { 54322, 54328 } };
		const auto narrow = swa_cells_in_view(dir, 4.0f / 3, ranges, 1.2f, 1), wide = swa_cells_in_view(dir, 16.0f / 9, ranges, 1.2f, 1);
		int lists = 0, cells = 0, missing = 0;
		std::string at;
		for (const auto &[frame, in43] : narrow) {
			const auto w = wide.find(frame);
			if (w == wide.end()) continue;
			lists++;
			cells += int(in43.size());
			int here = 0;
			for (const auto &c : in43) here += !w->second.count(c);
			if (here) at += " " + std::to_string(frame) + ":" + std::to_string(here);
			missing += here;
		}
		printf("SWA Death Star demo (TIME 145-140), Full distance: %d of 4:3's %d surface cells in view missing at 16:9 over %d lists%s\n",
			missing, cells, lists, at.c_str());
		assert(lists > 5 && cells > 1000 && missing == 0);
	}

	// The Death Star demo's low flight (TIME 136-133), 4:3, draw distance Full: the far ring past one
	// heading's list spans only that list's spread, so the horizon's corners at the picture's left and
	// right were empty, the gaps jumping as the camera turned the heading a step (3,148 of 19,432 cells
	// 16:9 draws within 4:3's view weren't drawn at 4:3). Full takes the neighbouring headings' cells,
	// as 16:9 does: all but one hidden cell over two lists are drawn at 4:3 too.
	static void swa_horizon_corners(const char *dir)
	{
		const std::initializer_list<std::pair<int, int>> ranges = { { 54862, 54872 }, { 54928, 54938 }, { 54966, 54976 } };
		const auto narrow = swa_cells_in_view(dir, 4.0f / 3, ranges, 1.2f, 0.93f), wide = swa_cells_in_view(dir, 16.0f / 9, ranges, 1.2f, 0.93f);
		int lists = 0, cells = 0, missing = 0;
		std::string at;
		for (const auto &[frame, in169] : wide) {
			const auto n = narrow.find(frame);
			if (n == narrow.end()) continue;
			lists++;
			cells += int(in169.size());
			int here = 0;
			for (const auto &c : in169) here += !n->second.count(c);
			if (here) at += " " + std::to_string(frame) + ":" + std::to_string(here);
			missing += here;
		}
		printf("SWA Death Star demo (TIME 136-133), Full distance: %d of 16:9's %d surface cells within 4:3's view missing at 4:3 over %d lists%s\n",
			missing, cells, lists, at.c_str());
		assert(lists > 5 && cells > 1000 && missing < 50);
	}

	// The Death Star demo (TIME 123), 16:9: the far ground under the left of the picture, one face
	// 270,000 units across, turns with the view, and a corner far off to the side crossed the eye's
	// plane between two lists: taken for a face passing through the eye (the asteroid's), it was left
	// out, and the backdrop showed black through a quarter of the picture for four pictures. The
	// lists draw it grey there.
	static void swa_far_ground(const char *dir)
	{
		model1 m(load_regions(dir, game::id{ "swa" }));
		m.reset();
		m.set_drawing(false);
		m.set_smooth(true);
		m.set_aspect(16.0f / 9);
		m.set_edge_aa(false);
		game::swa::set_lod(m, false);
		game::swa::set_full_draw_distance(m, true);
		while (m.frame_number() < 55618) m.run_frame();
		m.set_drawing(true);
		int pictures = 0, black = 0;
		while (m.frame_number() < 55622) {
			m.run_frame();
			for (double k : { 0.25, 0.75 }) {
				m.render_smooth(double(m.frame_number()) - 1 + k);
				pictures++;
				const int w = m.frame_w(), h = m.frame_h();
				for (int y = h / 3; y < h * 2 / 3; y++)
					for (int x = 0; x < w / 6; x++) black += (m.frame().data()[size_t(y) * w + x] & 0xffffff) == 0;
			}
		}
		printf("SWA Death Star demo (TIME 123), 16:9: %d black pixels under the left of the picture over %d pictures\n", black, pictures);
		assert(pictures == 8 && black < 500);
	}

	// The attract's trench run in the cockpit (TIME 160): the game draws ten rows of the trench from
	// a row behind, about 14,000 units ahead, black beyond. Draw distance Full draws its rows on to
	// the table's end: the picture's middle, black there with the arcade's distance, is the trench.
	static void swa_trench_distance(const char *dir)
	{
		auto black_middle = [&](bool full) {
			model1 m(load_regions(dir, game::id{ "swa" }));
			m.reset();
			m.set_drawing(false);
			m.set_aspect(16.0f / 9);
			m.set_edge_aa(false);
			game::swa::set_full_draw_distance(m, full);
			while (m.frame_number() < 26099) m.run_frame();
			m.set_drawing(true);
			m.run_frame();
			const int w = m.frame_w(), h = m.frame_h();
			int black = 0;
			for (int y = h * 42 / 100; y < h * 46 / 100; y++)
				for (int x = w * 51 / 100; x < w * 535 / 1000; x++) black += (m.frame().data()[size_t(y) * w + x] & 0xffffff) == 0;
			const auto state = std::static_pointer_cast<game::swa::gameplay_state>(m.game_state());
			return std::pair{ black, state->trench_cells };
		};
		const auto [arcade, arcade_cells] = black_middle(false);
		const auto [full, full_cells] = black_middle(true);
		printf("SWA trench (TIME 160): %d black pixels at the vanishing point with the arcade's distance, %d with Full (%llu cells added)\n",
			arcade, full, (unsigned long long)full_cells);
		assert(arcade_cells == 0 && full_cells > 0 && arcade > 100 && full * 5 < arcade);
	}

	// The Death Star demo (TIME 150), 16:9: the surface blocks have steps with no face between the
	// two tops, in a block and between neighbouring ones, which the lists' eyes only see side-on.
	// The blended eyes, a little to one side, looked into them at the screen's left edge: big black
	// wedges for a single picture. Pixels black in one picture but in neither neighbour at the left
	// edge, where the wedges were.
	static void swa_open_steps(const char *dir)
	{
		model1 m(load_regions(dir, game::id{ "swa" }));
		m.reset();
		m.set_drawing(false);
		m.set_smooth(true);
		m.set_aspect(16.0f / 9);
		m.set_edge_aa(false);
		game::swa::set_lod(m, false);
		game::swa::set_full_draw_distance(m, true);
		while (m.frame_number() < 53558) m.run_frame();
		m.set_drawing(true);
		std::vector<std::vector<uint8_t>> black;
		std::vector<double> times;
		int pictures = 0, flashes = 0;
		std::string at;
		while (m.frame_number() < 53622) {
			m.run_frame();
			for (double k : { 0.25, 0.75 }) {
				const double t = double(m.frame_number()) - 1 + k;
				m.render_smooth(t);
				const int w = m.frame_w(), h = m.frame_h(), strip = w / 8;
				std::vector<uint8_t> b(size_t(strip) * h);
				for (int y = 0; y < h; y++)
					for (int x = 0; x < strip; x++) b[size_t(y) * strip + x] = (m.frame().data()[size_t(y) * w + x] & 0xffffff) == 0;
				black.push_back(std::move(b));
				times.push_back(t);
				if (black.size() < 3) continue;
				pictures++;
				int here = 0;
				for (size_t i = 0; i < black[1].size(); i++) here += black[1][i] && !black[0][i] && !black[2][i];
				if (here > 200) at += " " + std::to_string(times[1]).substr(0, 8) + ":" + std::to_string(here);
				flashes += here;
				black.erase(black.begin());
				times.erase(times.begin());
			}
		}
		printf("SWA Death Star demo (TIME 150), 16:9: %d one-picture black pixels at the left edge over %d pictures%s\n", flashes, pictures, at.c_str());
		assert(pictures > 100 && flashes < 1000);
	}


};

int main(int argc, char **argv)
{
	model1_video_test::copy_bounds_and_sort();
	if (argc > 2 && (std::string(argv[2]) == "--swa-wrap" || std::string(argv[2]) == "--swa-wrap-smooth")) {
		for (bool native : { false, true })
			for (bool lod : { false, true }) {
				if (std::string(argv[2]) == "--swa-wrap") model1_video_test::swa_wrap_transition(argv[1], native, lod);
				model1_video_test::swa_wrap_smooth(argv[1], native, lod);
			}
		return 0;
	}
	model1_video_test::object_bounds();
	if (argc > 2 && std::string(argv[2]) == "--swa-object-bounds") {
		model1_video_test::swa_destroyer_bounds(argv[1]);
		return 0;
	}
	if (argc > 2 && std::string(argv[2]) == "--swa-ships") {
		model1_video_test::swa_passing_tie(argv[1]);
		model1_video_test::swa_surface_slab(argv[1]);
		model1_video_test::swa_trench_jump(argv[1]);
		model1_video_test::swa_cockpit_surface(argv[1]);
		model1_video_test::swa_trench_mirror(argv[1]);
		model1_video_test::swa_fleet_carrier(argv[1]);
		model1_video_test::swa_asteroid_top_edge(argv[1]);
		model1_video_test::swa_side_ship(argv[1]);
		model1_video_test::swa_wide_far_cells(argv[1]);
		model1_video_test::swa_horizon_corners(argv[1]);
		model1_video_test::swa_open_steps(argv[1]);
		model1_video_test::swa_far_ground(argv[1]);
		model1_video_test::swa_trench_distance(argv[1]);
		return 0;
	}
	model1_video_test::overlay_priority();
	if (argc > 2 && std::string(argv[2]) == "--swa-priority") {
		model1_video_test::swa_name_entry(argv[1]);
		return 0;
	}
	model1_video_test::trackside_collisions();
	if (argc > 2 && std::string(argv[2]) == "--direction-signs") {
		for (bool native : { false, true })
			for (bool smooth : { false, true }) {
				model1_video_test::trackside_distance(argv[1], native, smooth, !smooth, true);
				model1_video_test::trackside_distance(argv[1], native, smooth, !smooth);
			}
		return 0;
	}
	if (argc > 2 && std::string(argv[2]) == "--trackside") {
		model1_video_test::trackside_distance(argv[1], false);
		return 0;
	}
	model1_video_test::backface_specular();
	model1_video_test::crash_materials();
	if (argc > 2 && std::string(argv[2]) == "--attract-scenery") {
		for (bool native : { false, true }) {
			model1_video_test::trackside_distance(argv[1], native);
			model1_video_test::trackside_distance(argv[1], native, false, false);
		}
		for (bool native : { false, true }) {
			for (bool full : { false, true })
				model1_video_test::attract_scenery(argv[1], full, true, true, native);
			model1_video_test::attract_scenery(argv[1], true, false, false, native);
		}
		return 0;
	}
	if (argc > 2 && std::string(argv[2]) == "--crash-livery") {
		for (bool native : { false, true })
			for (bool smooth : { false, true }) model1_video_test::crash_livery(argv[1], smooth, native);
		return 0;
	}
	if (argc > 2 && std::string(argv[2]) == "--scenery-distance") {
		for (bool lod : { false, true })
			for (bool smooth : { false, true }) model1_video_test::scenery_distance(argv[1], smooth, lod);
		return 0;
	}
	if (argc > 2 && std::string(argv[2]) == "--logo-shading") {
		model1_video_test::logo_shading(argv[1]);
		return 0;
	}
	if (argc > 2 && std::string(argv[2]) == "--grid-hud") {
		model1_video_test::race_grid_hud(argv[1]);
		return 0;
	}
	if (argc > 2 && std::string(argv[2]) == "--pit") {
		model1_video_test::logo_handoff(argv[1]);
		return 0;
	}
	if (argc > 2 && std::string(argv[2]) == "--car-visibility") {
		model1_video_test::culled_records();
		model1_video_test::car_visibility(argv[1], false);
		model1_video_test::car_visibility(argv[1], true);
		model1_video_test::car_visibility(argv[1], true, true);
		return 0;
	}
	model1_video_test::foreground_order();
	model1_video_test::extra_scenery();
	model1_video_test::object_fade();
	model1_video_test::scenery_records();
	model1_video_test::culled_records();
	model1_video_test::hud_edge_pinning();
	model1_video_test::wide_direct_overlay();
	model1_video_test::unstretched_backdrops();
	model1_video_test::wide_split_tiles();
	model1_video_test::swa_far_blend();
	model1_video_test::swa_surface_cells();
	model1_video_test::swaying_camera();
	if (argc > 2 && std::string(argv[2]) == "--goal") {
		model1_video_test::goal_fly_in(argv[1]);
		return 0;
	}
	if (argc > 2 && std::string(argv[2]) == "--motion") {
		model1_video_test::swaying_car(argv[1]);
		model1_video_test::scenery_motion(argv[1], false);
		model1_video_test::scenery_motion(argv[1], true);
		model1_video_test::native_motion(argv[1]);
		model1_video_test::car_visibility(argv[1], false);
		model1_video_test::car_visibility(argv[1], true);
		model1_video_test::car_visibility(argv[1], true, true);
		return 0;
	}
	if (argc > 1)
		for (bool smooth : { false, true })
			for (bool extended : { false, true })
				model1_video_test::logo(argv[1], smooth, extended);
	if (argc > 1) {
		model1_video_test::trackside_distance(argv[1], false);
		model1_video_test::trackside_distance(argv[1], false, true, false, true);
		for (bool full : { false, true })
			model1_video_test::attract_scenery(argv[1], full, true, true, false);
		for (bool smooth : { false, true }) model1_video_test::crash_livery(argv[1], smooth);
		for (bool lod : { false, true })
			for (bool smooth : { false, true }) model1_video_test::scenery_distance(argv[1], smooth, lod);
		model1_video_test::logo_shading(argv[1]);
		model1_video_test::race_grid_hud(argv[1]);
		model1_video_test::logo_handoff(argv[1]);
		model1_video_test::goal_fly_in(argv[1]);
		model1_video_test::logo(argv[1], true, true, 2, 16.0f / 9);
		model1_video_test::logo(argv[1], true, true, 2, 21.0f / 9);
		model1_video_test::logo(argv[1], false, false, 2);
		model1_video_test::scenery_motion(argv[1], false);
		model1_video_test::scenery_motion(argv[1], true);
		model1_video_test::native_motion(argv[1]);
	}
	if (argc > 1) {
		model1_video_test::swaying_car(argv[1]);
		model1_video_test::car_visibility(argv[1], false);
		model1_video_test::car_visibility(argv[1], true);
		model1_video_test::car_visibility(argv[1], true, true);
	}
	puts("Model 1 video tests passed");
}

