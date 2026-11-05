#include "../../../src/host/regions.h"
#include "../../../src/host/png.h"
#include "../../../src/games/settings.h"
#include "../../../src/games/script.h"
#include "../../../src/games/vr/gameplay.h"
#include "../../../src/machine/linked_pair.h"
#include "../../../src/rt/native.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <memory>
#include <set>
#include <string>
#include <utility>

using machines_type = std::array<std::unique_ptr<model1>, 3>;

struct model1_video_test
{
	static int linked_notice(model1 &m)
	{
		if (m.work_ram()[0x1138] != 0x07 || m.tile_word(0x5000) != 0x8000 ||
			m.tile_word(0x5004) != 0x4000) return 0;
		std::array<uint16_t, model1::SCREEN_H> scroll;
		for (int y = 0; y < model1::SCREEN_H; y++) {
			scroll[y] = m.tile_word(0x4000 + y);
			m.tile_write(0x4000 + y, 0);
		}
		m.m_tiles.update(m.m_pens);
		bitmap_rgb32 artwork;
		artwork.allocate(1024, model1::SCREEN_H);
		artwork.fill(model1::KEY);
		m.m_tiles.m_ox = 256;
		m.m_tiles.draw_wide(artwork, 1, 0, 256);
		m.m_tiles.m_ox = 0;
		for (int y = 0; y < model1::SCREEN_H; y++) m.tile_write(0x4000 + y, scroll[y]);
		const float original_aspect = m.aspect();
		int side_pixels = 0, settled_pixels = 0;
		for (float aspect : {4.0f / 3, 16.0f / 10, 16.0f / 9, 64.0f / 27, 32.0f / 9, 64.0f / 9}) {
			m.set_aspect(aspect);
			m.redraw();
			const auto &cached = m.cached_layer(1, 0).px;
			// Bottom notices are separately edge-pinned; the linked-play artwork is above them.
			for (int y = 0; y < 336; y++) {
				const int shift = scroll[y] + (scroll[y] * m.m_ext + 240) / 480;
				for (int x = 0; x < cached.width(); x++) {
					const int source = x - m.m_ext - shift;
					const uint32_t expected = source >= 0 && source < 512
						? artwork.pix(y, 256 + source) : model1::KEY;
					assert(cached.pix(y, x) == expected);
					if (expected == model1::KEY) continue;
					side_pixels += scroll[y] && x >= m.m_ext + model1::SCREEN_W;
					settled_pixels += !scroll[y];
				}
			}
			bitmap_rgb32 combined;
			uint64_t key = 0, gen = 0;
			m.parts_composite(combined, key, gen, {1});
			assert(std::equal(cached.data(), cached.data() + size_t(cached.width()) * cached.height(), combined.data()));
			for (int scale : {1, 2}) {
				m.set_render_scale(scale);
				bitmap_rgb32 whole;
				whole.allocate(cached.width() * scale, cached.height() * scale);
				whole.fill(model1::KEY);
				m.draw_tiles(whole, whole.cliprect(), 1, 0);
				for (int y = 0; y < whole.height(); y++)
					for (int x = 0; x < whole.width(); x++)
						assert(whole.pix(y, x) == cached.pix(y / scale, x / scale));
			}
			m.set_render_scale(1);
		}
		m.set_aspect(original_aspect);
		m.redraw();
		return (side_pixels > 0 ? 1 : 0) | (settled_pixels > 0 ? 2 : 0);
	}

	struct observed_race {
		std::array<int, 16> copies{};
		int corrected = 0;
	};

	static void watch_observer(model1 &m, observed_race &proof)
	{
		const auto read = m.m_gameplay.read_value;
		m.m_gameplay.read_value = [&, read](uint32_t pc, uint32_t address, uint16_t value) {
			const auto state = std::static_pointer_cast<game::vr::gameplay_state>(m.game_state());
			const uint32_t car = m.cpu().reg(25);
			const bool copying = state->observed_racers && m.work_ram()[0x1138] == 0x14 &&
				car >= 0x400d00 + uint32_t(state->observed_racers) * 0x280 &&
				car <= 0x403280 && (car - 0x400d00) % 0x280 == 0 && address == car + 0x34;
			std::array<uint8_t, 0x10> header{};
			uint32_t before = 0;
			if (copying) {
				std::copy_n(m.nvram().begin() + car - 0x400000 + 4, header.size(), header.begin());
				before = m.read32(car + 0x1e);
			}
			const uint16_t result = read(pc, address, value);
			if (copying) {
				const int index = int((car - 0x400d00) / 0x280);
				const auto &snapshot = state->observed_cars[index];
				assert(m.read32(car) == snapshot.flags);
				assert(std::equal(snapshot.race.begin(), snapshot.race.end(),
					m.nvram().begin() + car - 0x400000 + 0x14));
				assert(std::equal(header.begin(), header.end(), m.nvram().begin() + car - 0x400000 + 4));
				proof.copies[index]++;
				proof.corrected += before != m.read32(car + 0x1e);
			}
			return result;
		};
	}

	static int distant_cars(const model1 &m)
	{
		int count = 0;
		for (const auto &[word, objects] : m.m_list_extra[m.m_picture_buffer])
			for (const auto &object : objects)
				count += object.owner >= 0x400d00 && object.owner <= 0x403280;
		return count;
	}

	static int monitor_zoom(model1 &m)
	{
		const auto &a = m.m_logic[0], &b = m.m_logic[1];
		if (b.frame - a.frame != 2 || m.work_ram()[0x1138] != 0x14) return 0;
		int checked = 0;
		for (const auto &[view, to] : b.zooms) {
			if ((view[0] & 0xffff) != 248 || !to.uniform) continue;
			const auto from = a.zooms.find(view);
			if (from == a.zooms.end() || !from->second.uniform ||
				std::fabs(from->second.value[0] - to.value[0]) < 0.01f) continue;
			for (double fraction : {0.25, 0.5, 0.75}) {
				m.render_smooth(a.frame + 2 + 2 * fraction, false);
				if (m.m_blend_cut || m.m_replay_older) continue;
				const auto &zoom = m.m_blend_zooms.at(view);
				for (int axis = 0; axis < 2; axis++) {
					const float expected = float(from->second.value[axis] +
						(to.value[axis] - from->second.value[axis]) * fraction);
					assert(std::fabs(zoom[axis] - expected) < 0.0001f);
				}
				for (const auto &[last_view, blended] : m.m_blend_zooms)
					if ((last_view[0] & 0xffff) == uint32_t(m.m_view->xc) &&
						last_view[6] == std::bit_cast<uint32_t>(m.m_view->viewx) &&
						last_view[7] == std::bit_cast<uint32_t>(m.m_view->viewy)) {
						assert(std::fabs(m.m_view->zoomx - 4 * blended[0]) < 0.0001f);
						assert(std::fabs(m.m_view->zoomy - 4 * blended[1]) < 0.0001f);
					}
				checked++;
			}
		}
		return checked;
	}

	static void monitor_commentator(model1 &m)
	{
		const float original_aspect = m.aspect();
		for (int scale : {1, 2}) {
			m.set_render_scale(scale);
			m.set_aspect(4.0f / 3);
			m.redraw();
			assert(m.m_view->xc == 405 && m.m_view->yc == 59);
			assert(m.m_view->lx1 == 327 && m.m_view->lx2 == 482);
			const std::vector<uint32_t> original(m.frame().data(),
				m.frame().data() + size_t(m.frame_w()) * m.frame_h());
			for (float aspect : {16.0f / 9, 64.0f / 27, 32.0f / 9, 128.0f / 27, 64.0f / 9}) {
				m.set_aspect(aspect);
				assert(m.aspect() == aspect);
				m.redraw();
				assert(m.m_wide_frame);
				assert(m.m_view->xc == 405 + m.m_ext);
				assert(m.m_view->lx1 == 327 + m.m_ext && m.m_view->lx2 == 482 + m.m_ext);
				// Portrait, background, name and speech stay together, with the original right margin.
				for (int y = 7 * scale; y < 113 * scale; y++)
					for (int x = 327 * scale; x < 483 * scale; x++)
						assert(m.frame().pix(y, 2 * m.m_ext * scale + x) ==
							original[size_t(y) * model1::SCREEN_W * scale + x]);
			}
		}
		m.set_render_scale(1);
		m.set_aspect(original_aspect);
		m.redraw();
	}

	static std::array<int, 2> monitor_commentator_smooth(model1 &m, bool force_cut = false)
	{
		const auto &a = m.m_logic[0];
		auto &b = m.m_logic[1];
		if (b.frame - a.frame != 2 || m.work_ram()[0x1138] != 0x14) return {};
		const auto camera = b.camera;
		const int cut_frame = m.m_cut_frame;
		// Exercise cut extrapolation with real inset models even if sampled broadcast shots don't cut.
		if (force_cut) b.camera[9] += 1000;
		std::array<int, 2> checked{};
		const auto begin = m.m_gameplay.render_begin;
		m.m_gameplay.render_begin = [&] {
			if (begin) begin();
			if (!m.m_replaying || (m.m_replay_older && !m.m_replay_extra)) return;
			const auto &to = m.m_logic[m.m_replay_older ? 0 : 1];
			const auto &from = m.m_replay_older ? m.m_logic_prev : a;
			for (size_t i = 0; i < to.slots.size(); i++) {
				const auto &slot = to.slots[i];
				if (!slot.screen_space || slot.polys.empty() || !m.m_blend_ok[i]) continue;
				for (const auto &old : from.slots) {
					if (!old.screen_space || old.polys != slot.polys ||
						!std::equal(std::begin(old.mat), std::end(old.mat), slot.mat)) continue;
					if (!m.m_replay_older && std::none_of(m.m_logic_prev.slots.begin(), m.m_logic_prev.slots.end(),
						[&](const auto &previous) {
							return previous.screen_space && previous.polys == slot.polys &&
								std::equal(std::begin(previous.mat), std::end(previous.mat), slot.mat);
						})) continue;
					for (int k = 0; k < 12; k++) {
						if (std::fabs(m.m_blend[i][k] - slot.mat[k]) >= 0.0001f)
							fprintf(stderr, "Portrait frame=%d slot=%zu poly=%x owner=%x older=%d extra=%d axis=%d raw=%g blend=%g\n",
								to.frame, i, slot.polys[0], slot.owner, m.m_replay_older, m.m_replay_extra,
								k, slot.mat[k], m.m_blend[i][k]);
						assert(std::fabs(m.m_blend[i][k] - slot.mat[k]) < 0.0001f);
					}
					checked[0]++;
					checked[1] += m.m_blend_cut && m.m_replay_older && m.m_replay_extra;
					break;
				}
			}
		};
		for (double fraction : {0.25, 0.5, 0.75})
			m.render_smooth(a.frame + 2 + 2 * fraction, false);
		m.m_gameplay.render_begin = begin;
		b.camera = camera;
		m.m_cut_frame = cut_frame;
		return checked;
	}

	static void monitor_hud(model1 &m)
	{
		const float original_aspect = m.aspect();
		for (int scale : {1, 2}) {
			m.set_render_scale(scale);
			m.set_aspect(4.0f / 3);
			m.redraw();
			bitmap_rgb32 original;
			original.allocate(m.frame_w(), m.frame_h());
			original.fill(model1::KEY);
			m.draw_tiles(original, original.cliprect(), 1, 0);
			int artwork_pixels = 0;
			for (int y = 0; y < 104 * scale; y++)
				for (int x = 0; x < 128 * scale; x++)
					artwork_pixels += original.pix(y, x) != model1::KEY;
			assert(artwork_pixels > 1000 * scale * scale);
			for (float aspect : {16.0f / 9, 32.0f / 9, 128.0f / 27, 64.0f / 9}) {
				m.set_aspect(aspect);
				assert(m.aspect() == aspect);
				m.redraw();
				assert(m.m_wide_frame);
				bitmap_rgb32 wide;
				wide.allocate(m.frame_w(), m.frame_h());
				wide.fill(model1::KEY);
				m.draw_tiles(wide, wide.cliprect(), 1, 0);
				// The LIVE logo and camera label move to the edge, never scale horizontally.
				for (int y = 0; y < 104 * scale; y++)
					for (int x = 0; x < 128 * scale; x++)
						assert(wide.pix(y, x) == original.pix(y, x));
			}
		}
		m.set_render_scale(1);
		m.set_aspect(original_aspect);
		m.redraw();
	}

	static void monitor_artwork(model1 &m)
	{
		assert(m.work_ram()[0x1138] == 0x10);
		assert(m.read8(0x40dc8c) == 3);
		const float original_aspect = m.aspect();
		for (int scale : {1, 2}) {
			m.set_render_scale(scale);
			m.set_aspect(4.0f / 3);
			m.redraw();
			const std::vector<uint32_t> original(m.frame().data(),
				m.frame().data() + size_t(m.frame_w()) * m.frame_h());
			for (float aspect : {16.0f / 9, 32.0f / 9, 128.0f / 27, 64.0f / 9}) {
				m.set_aspect(aspect);
				assert(m.aspect() == aspect);
				m.redraw();
				assert(!m.m_wide_frame);
				for (int y = 0; y < m.frame_h(); y++)
					for (int x = 0; x < model1::SCREEN_W * scale; x++)
						assert(m.frame().pix(y, m.m_ext * scale + x) ==
							original[size_t(y) * model1::SCREEN_W * scale + x]);
			}
		}
		m.set_render_scale(1);
		m.set_aspect(original_aspect);
		m.redraw();
	}
};

// Independent reference: reverse both board delivery and CPU execution, while
// retaining the same vblank snapshot boundary as the production scheduler.
static void run_reversed_frame(machines_type &machines, std::array<m1comm::memory, 2> &snapshots)
{
	for (const auto &machine : machines) assert(machine->scanline() == 0);
	machines[2]->observe(machines[0].get());
	for (int line = 0; line < model1::FRAME_LINES; line++) {
		if (line == model1::FRAME_VBLANK &&
			machines[0]->link()->ready() && machines[1]->link()->ready()) {
			for (int p = 0; p < 2; p++) snapshots[p] = machines[p]->link()->snapshot();
			if (machines[2]->link()->ready())
				machines[2]->link()->receive(snapshots[0], snapshots[1]);
			machines[1]->link()->tick();
			machines[0]->link()->tick();
		}
		for (int p : {2, 1, 0}) machines[p]->run_scanline();
		for (const auto &machine : machines)
			assert(machine->scanline() == (line + 1) % model1::FRAME_LINES);
	}
}

static void assert_same_state(machines_type &machines, machines_type &reference, bool solo = false)
{
	for (int p = 0; p < 3; p++) {
		if (solo && p == 1) continue;
		auto &machine = *machines[p];
		auto &other = *reference[p];
		assert(machine.scanline() == 0 && other.scanline() == 0);
		assert(machine.frame_number() == other.frame_number());
		assert(*machine.cpu().m_cycles == *other.cpu().m_cycles);
		assert(machine.work_ram() == other.work_ram() && machine.nvram() == other.nvram());
		assert(machine.camera() == other.camera());
		assert(machine.link()->snapshot() == other.link()->snapshot());
		assert(machine.link()->ticks() == other.link()->ticks());
		assert(machine.link()->cn() == other.link()->cn() && machine.link()->fg() == other.link()->fg());
		assert(machine.m_audio_out == other.m_audio_out);
	}
}

int main(int argc, char **argv)
{
	if ((argc != 3 && argc != 4) || (argc == 4 && strcmp(argv[3], "--interp"))) {
		fprintf(stderr, "usage: vr_live_monitor_test regions output-directory [--interp]\n");
		return 2;
	}
	const bool native = argc == 3;
	std::filesystem::create_directories(argv[2]);
	rec::hints map;
	assert(map.read("assets/games/vr/vr_map.txt"));
	machines_type machines, reference;
	std::array<rec::context, 3> runtimes, reference_runtimes;
	auto initialize = [&](machines_type &nodes, std::array<rec::context, 3> &contexts) {
		for (int p = 0; p < 3; p++) {
			nodes[p] = std::make_unique<model1>(load_regions(argv[1], game::id{"vr"}));
			auto &machine = *nodes[p];
			if (native) {
				contexts[p].attach(machine.cpu(), "vr");
				assert(rec::build_native(machine.maincpu_rom(), map));
				machine.run_cpu = [context = &contexts[p]](int n) { context->run(n); };
			}
			machine.set_drawing(false);
			machine.enable_sound();
			machine.reset();
			for (int f = 0; f < 900 && !machine.past_boot(); f++) machine.run_frame();
			assert(machine.past_boot());
			game::vr::set_coin_setting(machine, game::vr::FREE_PLAY);
			game::vr::set_language(machine, 1);
			game::vr::set_machine_setting(machine, game::vr::machine_setting::car_color, p == 0 ? 3 : p == 1 ? 1 : 0);
			assert(game::edit_eeprom_settings(machine, 5, [p](uint16_t *words) {
				words[6] = uint16_t((words[6] & 0x00ff) | ((p + 1) << 8));
				return true;
			}));
			machine.reset();
			machine.forget_camera();
			if (native) {
				contexts[p].attach(machine.cpu(), "vr");
				rec::reset_fallbacks();
			}
		}
	};
	initialize(machines, runtimes);
	initialize(reference, reference_runtimes);
	model1_video_test::observed_race pair_race;
	model1_video_test::watch_observer(*machines[2], pair_race);
	linked_pair pair(*machines[0], *machines[1], machines[2].get());
	std::array<m1comm::memory, 2> reference_snapshots{};
	for (int p = 0; p < 2; p++)
		reference[p]->enable_link([&, p](const auto &, auto &peer) {
			peer = reference_snapshots[1 - p];
			return true;
		}, true);
	reference[2]->enable_link([](const auto &, auto &) -> bool {
		throw std::logic_error("a relay must not publish racing data");
	}, true);
	for (int p = 0; p < 3; p++)
		machines[p]->on_line = [&, p](int line) {
			for (int other = 0; other < 3; other++)
				assert(machines[other]->scanline() ==
					(other < p ? (line + 1) % model1::FRAME_LINES : line));
		};
	auto run_both = [&] {
		std::array<uint64_t, 3> frames{};
		for (int p = 0; p < 3; p++) frames[p] = machines[p]->frame_number();
		pair.run_frame();
		run_reversed_frame(reference, reference_snapshots);
		assert_same_state(machines, reference);
		for (int p = 0; p < 3; p++) assert(machines[p]->frame_number() == frames[p] + 1);
	};
	std::array<int, 2> notice_checks{};
	for (int frame = 0; frame < 2800; frame++) {
		for (int p : {0, 2}) {
			machines[p]->set_drawing(frame >= 2100);
			reference[p]->set_drawing(frame >= 2100);
		}
		run_both();
		if (frame >= 2100 && frame % 10 == 0)
			for (int p : {0, 2}) {
				const int checked = model1_video_test::linked_notice(*machines[p]);
				assert(checked == model1_video_test::linked_notice(*reference[p]));
				notice_checks[p / 2] |= checked;
			}
		for (auto &machine : machines) machine->m_audio_out.clear();
		for (auto &machine : reference) machine->m_audio_out.clear();
	}
	assert(notice_checks[0] == 3 && notice_checks[1] == 3);
	puts("Linked notice: complete sliding and settled artwork, six aspect ratios, 1x/2x, whole/parts parity");
	for (int p = 0; p < 3; p++) {
		machines[p]->reset();
		machines[p]->forget_camera();
		reference[p]->reset();
		reference[p]->forget_camera();
		machines[p]->set_drawing(false);
		reference[p]->set_drawing(false);
		if (native) {
			runtimes[p].attach(machines[p]->cpu(), "vr");
			reference_runtimes[p].attach(reference[p]->cpu(), "vr");
		}
	}
	std::array<int, 3> previous_phase{-1, -1, -1};
	std::array<uint32_t, 3> previous_camera{};
	std::array<std::deque<std::array<uint32_t, 3>>, 2> positions;
	std::array<std::set<std::array<uint32_t, 3>>, 2> replicated_positions;
	std::array<int, 2> replicated{};
	std::set<uint32_t> broadcast_cameras;
	int racing = 0, monitor_pictures = 0, audible_frames = 0;
	uint64_t last_link_tick = 0;
	bool artwork_checked = false, commentator_checked = false;
	for (int frame = 0; frame < 7000; frame++) {
		const auto tick = machines[0]->link()->ticks();
		const bool capture = tick >= 600 && tick != last_link_tick && tick % 300 == 0;
		for (int p : {0, 2}) {
			machines[p]->set_drawing(capture);
			reference[p]->set_drawing(capture);
		}
		for (int p = 0; p < 2; p++) {
			auto &machine = *machines[p];
			const bool start = (tick >= 400 && tick < 406) || (tick >= 520 && tick < 526);
			machine.m_inputs[0] = uint8_t(~(start ? 0x10 : 0));
			assert(machine.set_analog_input(0, 0x80));
			assert(machine.set_analog_input(1, p ? 0x90 : 0xff));
			assert(machine.set_analog_input(2, 0x30));
			reference[p]->m_inputs[0] = machine.m_inputs[0];
			assert(reference[p]->set_analog_input(0, 0x80));
			assert(reference[p]->set_analog_input(1, p ? 0x90 : 0xff));
			assert(reference[p]->set_analog_input(2, 0x30));
		}
		run_both();
		if (!artwork_checked && machines[2]->work_ram()[0x1138] == 0x10 && tick >= 600) {
			model1_video_test::monitor_artwork(*machines[2]);
			model1_video_test::monitor_artwork(*reference[2]);
			artwork_checked = true;
		}
		for (auto &machine : reference) machine->m_audio_out.clear();
		assert(machines[0]->link()->ticks() == machines[1]->link()->ticks());
		for (int p = 0; p < 3; p++)
			if (machines[p]->link()->ticks()) {
				assert((machines[p]->link()->fg() & 0x80) == 0);
				assert(machines[p]->link()->read(2) == (p + 1) % 3);
				assert(machines[p]->link()->read(3) == 2);
			}
		for (int p = 0; p < 3; p++) {
			auto &machine = *machines[p];
			const int phase = machine.work_ram()[0x1138];
			if (phase != previous_phase[p] || (p == 2 && phase == 0x14 && machine.camera() != previous_camera[p])) {
				fprintf(stderr, "frame=%d tick=%llu node=%d role=%u link=%u/%u phase=%02x camera=%08x\n",
					frame, (unsigned long long)tick, p + 1, machine.read8(0x40dc8c),
					machine.read8(0x40d802), machine.read8(0x40d801), phase, machine.camera());
				previous_phase[p] = phase;
				previous_camera[p] = machine.camera();
			}
			if (p == 2 && std::any_of(machine.m_audio_out.begin(), machine.m_audio_out.end(),
				[](int16_t sample) { return sample != 0; })) audible_frames++;
			machine.m_audio_out.clear();
		}
		if (machines[0]->work_ram()[0x1138] == 0x14) racing++;
		for (int p = 0; p < 2; p++) {
			std::array<uint32_t, 3> position{};
			for (int axis = 0; axis < 3; axis++)
				memcpy(&position[axis], &machines[p]->link()->snapshot()[0x10 + 0x1e + axis * 4], 4);
			positions[p].push_back(position);
			if (positions[p].size() > 8) positions[p].pop_front();
		}
		auto &monitor = *machines[2];
		if (monitor.work_ram()[0x1138] == 0x14) {
			broadcast_cameras.insert(monitor.camera());
			for (int p = 0; p < 2; p++) {
				std::array<uint32_t, 3> position{};
				for (int axis = 0; axis < 3; axis++)
					position[axis] = monitor.read32(0x400d00 + p * 0x280 + 0x1e + axis * 4);
				if (std::find(positions[p].begin(), positions[p].end(), position) != positions[p].end()) {
					replicated[p]++;
					replicated_positions[p].insert(position);
				}
			}
		}
		if (capture) {
			last_link_tick = tick;
			for (int p : {0, 2}) {
				auto &machine = *machines[p];
				const std::string path = std::string(argv[2]) + "/node-" + std::to_string(p + 1) +
					"-" + std::to_string(tick) + ".png";
				assert(write_png(path.c_str(), machine.frame().data(), machine.frame_w(), machine.frame_h()));
				assert(machine.frame_w() == reference[p]->frame_w() && machine.frame_h() == reference[p]->frame_h());
				assert(std::equal(machine.frame().data(),
					machine.frame().data() + size_t(machine.frame_w()) * machine.frame_h(),
					reference[p]->frame().data()));
				if (p == 2 && machine.frame_quads() > 0) monitor_pictures++;
			}
			if (tick == 6000) {
				model1_video_test::monitor_commentator(*machines[2]);
				model1_video_test::monitor_commentator(*reference[2]);
				commentator_checked = true;
			}
		}
	}
	fprintf(stderr, "LIVE probe (%s): %d racing frames, %d monitor pictures, %zu camera targets, "
		"%d audible frames, %d/%d exact player positions (%zu/%zu distinct)\n", native ? "native" : "interpreter",
		racing, monitor_pictures, broadcast_cameras.size(), audible_frames, replicated[0], replicated[1],
		replicated_positions[0].size(), replicated_positions[1].size());
	if (native)
		for (int p = 0; p < 3; p++) {
			runtimes[p].activate();
			fprintf(stderr, "node %d: %zu native fallback calls (first %08x)\n",
				p + 1, rec::fallback_count(), rec::first_fallback_address());
		}
	assert(machines[2]->read8(0x40dc8c) == 3);
	assert(machines[2]->read8(0x40d802) == 0);
	assert(machines[2]->read8(0x40d801) == 2);
	assert(machines[2]->past_boot() && racing > 120 && monitor_pictures > 0);
	assert(broadcast_cameras.size() >= 2 && audible_frames > 120);
	assert(replicated[0] > 120 && replicated[1] > 120);
	assert(replicated_positions[0].size() > 120 && replicated_positions[1].size() > 120);
	assert(machines[2]->link()->ticks() > 6000);
	assert(artwork_checked);
	assert(commentator_checked);
	assert(pair_race.copies[0] == 0 && pair_race.copies[1] == 0);
	for (int i = 2; i < 16; i++) assert(pair_race.copies[i] > 120);
	assert(pair_race.corrected > 120);
	model1_video_test::monitor_hud(*machines[2]);
	model1_video_test::monitor_hud(*reference[2]);
	auto monitor_line_check = std::move(machines[2]->on_line);
	machines[2]->on_line = {};
	machines[2]->run_scanline();
	reference[2]->run_scanline();
	bool unaligned = false;
	try { pair.run_frame(); }
	catch (const std::logic_error &) { unaligned = true; }
	assert(unaligned);
	const auto racer_ticks = machines[0]->link()->ticks();
	machines[2]->reset();
	machines[2]->on_line = std::move(monitor_line_check);
	reference[2]->reset();
	machines[2]->m_audio_out.clear();
	reference[2]->m_audio_out.clear();
	machines[2]->set_drawing(false);
	reference[2]->set_drawing(false);
	if (native) {
		runtimes[2].attach(machines[2]->cpu(), "vr");
		reference_runtimes[2].attach(reference[2]->cpu(), "vr");
	}
	assert(!machines[2]->link()->ready());
	for (int frame = 0; frame < 900; frame++) {
		run_both();
		for (auto &machine : machines) machine->m_audio_out.clear();
		for (auto &machine : reference) machine->m_audio_out.clear();
	}
	assert(machines[0]->link()->ticks() == racer_ticks + 900);
	assert(machines[1]->link()->ticks() == racer_ticks + 900);
	assert(machines[2]->past_boot() && machines[2]->link()->ticks() > 800);
	assert((machines[2]->link()->fg() & 0x80) == 0);
	for (const auto &machine : machines) assert(machine->link()->read(3) == 2);
	pair.disconnect();
	for (const auto &machine : machines) assert(!machine->link());
	assert(std::static_pointer_cast<game::vr::gameplay_state>(machines[2]->game_state())->observed_racers == 0);
	bool disconnected = false;
	try { pair.run_frame(); }
	catch (const std::logic_error &) { disconnected = true; }
	assert(disconnected);
	puts("Relay lifecycle: frame-boundary guard, independent reboot and disconnect passed");
	puts("Three-machine synchronization: scanline lockstep and reversed CPU/delivery order match for 7900 frames");
	for (int p : {0, 2}) {
		machines[p]->on_line = {};
		machines[p]->reset();
		machines[p]->forget_camera();
		if (native) runtimes[p].attach(machines[p]->cpu(), "vr");
		machines[p]->set_drawing(false);
		reference[p]->reset();
		reference[p]->forget_camera();
		if (native) reference_runtimes[p].attach(reference[p]->cpu(), "vr");
		reference[p]->set_drawing(false);
		machines[p]->set_smooth(true);
		reference[p]->set_smooth(true);
	}
	linked_pair solo(*machines[0], nullptr, machines[2].get());
	for (int p : {0, 2}) {
		reference[p]->enable_link([](const auto &, auto &) -> bool {
			throw std::logic_error("a solo session must not publish to another racer");
		}, true);
		machines[p]->on_line = [&, p](int line) {
			assert(machines[0]->scanline() == (p == 0 ? line : (line + 1) % model1::FRAME_LINES));
			assert(machines[2]->scanline() == line);
		};
	}
	int solo_racing = 0, solo_pictures = 0, solo_positions = 0, zoom_pictures = 0;
	std::array<int, 2> commentator_pictures{};
	std::array<int, 2> distant_car_pictures{};
	std::deque<std::array<uint32_t, 3>> solo_history;
	auto driver = game::make_auto_play(game::id{"vr"}, "qualifying", 12345, false);
	const auto pair_copies = pair_race.copies;
	float max_ai_separation = 0;
	int max_lap = 0;
	for (int frame = 0; frame < 15000; frame++) {
		const auto tick = machines[0]->link()->ticks();
		const bool start = (tick >= 400 && tick < 406) || (tick >= 520 && tick < 526);
		machines[0]->m_inputs[0] = uint8_t(~(start ? 0x10 : 0));
		machines[0]->set_analog_input(0, 0x80);
		machines[0]->set_analog_input(1, 0xff);
		machines[0]->set_analog_input(2, 0x30);
		machines[0]->m_inputs[1] = 0xff;
		auto controls = driver->frame(frame + 6200, *machines[0]);
		if (machines[0]->work_ram()[0x1138] == 0x14) {
			for (int c = 0; c < 3; c++) machines[0]->set_analog_input(c, uint8_t(controls.analog[c]));
			machines[0]->m_inputs[1] = uint8_t(~controls.pressed[1]);
		}
		machines[2]->set_drawing(true);
		reference[0]->m_inputs[0] = machines[0]->m_inputs[0];
		reference[0]->set_analog_input(0, 0x80);
		reference[0]->set_analog_input(1, 0xff);
		reference[0]->set_analog_input(2, 0x30);
		reference[0]->m_inputs[1] = machines[0]->m_inputs[1];
		if (machines[0]->work_ram()[0x1138] == 0x14) {
			for (int c = 0; c < 3; c++) reference[0]->set_analog_input(c, uint8_t(controls.analog[c]));
		}
		reference[2]->set_drawing(true);
		solo.run_frame();
		reference[2]->observe(reference[0].get());
		for (int line = 0; line < model1::FRAME_LINES; line++) {
			if (line == model1::FRAME_VBLANK && reference[0]->link()->ready()) {
				const auto snapshot = reference[0]->link()->snapshot();
				if (reference[2]->link()->ready()) reference[2]->link()->receive(snapshot);
				reference[0]->link()->tick(true);
			}
			for (int p : {2, 0}) reference[p]->run_scanline();
		}
		assert_same_state(machines, reference, true);
		if (frame % 30 == 0) {
			const bool force_cut = commentator_pictures[0] > 30 && commentator_pictures[1] == 0;
			const auto portraits = model1_video_test::monitor_commentator_smooth(*machines[2], force_cut);
			assert(portraits == model1_video_test::monitor_commentator_smooth(*reference[2], force_cut));
			for (int i = 0; i < 2; i++) commentator_pictures[i] += portraits[i];
			const int checked = model1_video_test::monitor_zoom(*machines[2]);
			assert(checked == model1_video_test::monitor_zoom(*reference[2]));
			zoom_pictures += checked;
		}
		for (int p : {0, 2}) machines[p]->m_audio_out.clear();
		for (int p : {0, 2}) reference[p]->m_audio_out.clear();
		if (machines[0]->link()->ticks()) assert(machines[0]->link()->read(3) == 1);
		if (machines[2]->link()->ticks())
			assert(machines[2]->link()->read(2) == 0 && machines[2]->link()->read(3) == 1);
		std::array<uint32_t, 3> position{};
		for (int axis = 0; axis < 3; axis++)
			memcpy(&position[axis], &machines[0]->link()->snapshot()[0x10 + 0x1e + axis * 4], 4);
		solo_history.push_back(position);
		if (solo_history.size() > 8) solo_history.pop_front();
		if (machines[2]->work_ram()[0x1138] == 0x14) {
			solo_racing++;
			if (frame > 2400 && machines[0]->work_ram()[0x1138] == 0x14) {
				max_lap = std::max(max_lap, int(machines[0]->read8(0x400d70)));
				for (uint32_t car = 0x400f80; car <= 0x403280; car += 0x280) {
					float squared = 0;
					for (int axis = 0; axis < 3; axis++) {
						const float delta = std::bit_cast<float>(machines[0]->read32(car + 0x1e + axis * 4)) -
							std::bit_cast<float>(machines[2]->read32(car + 0x1e + axis * 4));
						assert(std::isfinite(delta));
						squared += delta * delta;
					}
					max_ai_separation = std::max(max_ai_separation, std::sqrt(squared));
				}
			}
			for (int p : {0, 2})
				distant_car_pictures[p / 2] += model1_video_test::distant_cars(*machines[p]) > 0;
			for (int axis = 0; axis < 3; axis++)
				position[axis] = machines[2]->read32(0x400d00 + 0x1e + axis * 4);
			// The ROM extrapolates a moving linked player between published updates.
			solo_positions += std::any_of(solo_history.begin(), solo_history.end(), [&](const auto &published) {
				for (int axis = 0; axis < 3; axis++)
					if (std::fabs(std::bit_cast<float>(published[axis]) -
						std::bit_cast<float>(position[axis])) > 0.25f) return false;
				return true;
			});
			if (frame % 300 == 0 && machines[2]->frame_quads()) {
				solo_pictures++;
				assert(std::equal(machines[2]->frame().data(),
					machines[2]->frame().data() + size_t(machines[2]->frame_w()) * machines[2]->frame_h(),
					reference[2]->frame().data()));
				const auto path = std::string(argv[2]) + "/solo-monitor.png";
				assert(write_png(path.c_str(), machines[2]->frame().data(),
					machines[2]->frame_w(), machines[2]->frame_h()));
			}
		}
	}
	fprintf(stderr, "Solo LIVE: %d race frames, %d broadcast pictures, %d replicated positions\n",
		solo_racing, solo_pictures, solo_positions);
	assert(solo_racing > 120 && solo_pictures > 0 && solo_positions > 120);
	fprintf(stderr, "Monitor zoom: %d fractional pictures track the ROM's changing projection\n", zoom_pictures);
	assert(zoom_pictures > 30);
	fprintf(stderr, "Monitor commentator: %d stationary inset matrices, %d extrapolated through camera cuts\n",
		commentator_pictures[0], commentator_pictures[1]);
	assert(commentator_pictures[0] > 30 && commentator_pictures[1] > 0);
	assert(distant_car_pictures[0] > 100 && distant_car_pictures[1] > 100);
	fprintf(stderr, "Distant cars: player %d, Live Monitor %d race frames include renderer-only cars\n",
		distant_car_pictures[0], distant_car_pictures[1]);
	assert(pair_race.copies[0] == 0);
	for (int i = 1; i < 16; i++) assert(pair_race.copies[i] - pair_copies[i] > 120);
	fprintf(stderr, "Authoritative AI: every rival copied exactly, %d divergent poses corrected; human headers untouched\n",
		pair_race.corrected);
	fprintf(stderr, "Authoritative AI: maximum end-of-video-frame separation %g world units\n", max_ai_separation);
	assert(std::isfinite(max_ai_separation) && max_ai_separation < 3.0f);
	assert(max_lap >= 2);
	puts("Solo monitor: one racer, auto-driven race and reversed CPU/delivery order match for 15000 frames");
}
