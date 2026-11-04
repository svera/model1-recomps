#include "../../../src/host/regions.h"
#include "../../../src/host/png.h"
#include "../../../src/games/settings.h"
#include "../../../src/games/vr/gameplay.h"
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

// Test-only relay receiver: the racing nodes keep their existing two-cabinet protocol.
// The monitor receives both published car blocks and the master's additional status bytes.
static void receive_live(m1comm &relay, const std::array<m1comm::memory, 2> &snapshots)
{
	assert(relay.cn() & 1);
	assert(relay.read(1) == 0);
	relay.write(0, 1);
	relay.write(2, 0);
	relay.write(3, 2);
	for (int p = 0; p < 2; p++)
		if (snapshots[p][4])
			for (unsigned i = 0; i < 0x1c4; i++)
				relay.write(0x10 + (p + 1) * 0x1c4 + i, snapshots[p][0x10 + i]);
	for (unsigned i = 6; i < 16; i++) relay.write(i, snapshots[0][i]);
	relay.write(5, 0);
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
	std::array<std::unique_ptr<model1>, 3> machines;
	std::array<rec::context, 3> runtimes;
	for (int p = 0; p < 3; p++) {
		machines[p] = std::make_unique<model1>(load_regions(argv[1], game::id{"vr"}));
		auto &machine = *machines[p];
		if (native) {
			runtimes[p].attach(machine.cpu(), "vr");
			assert(rec::build_native(machine.maincpu_rom(), map));
			machine.run_cpu = [&, p](int n) { runtimes[p].run(n); };
		}
		machine.set_drawing(false);
		machine.enable_sound();
		machine.reset();
		for (int f = 0; f < 900 && !machine.past_boot(); f++) machine.run_frame();
		assert(machine.past_boot());
		game::vr::set_coin_setting(machine, game::vr::FREE_PLAY);
		game::vr::set_language(machine, 1);
		game::vr::set_machine_setting(machine, game::vr::machine_setting::car_color, p == 1 ? 1 : 0);
		assert(game::edit_eeprom_settings(machine, 5, [p](uint16_t *words) {
			words[6] = uint16_t((words[6] & 0x00ff) | ((p + 1) << 8));
			return true;
		}));
		machine.reset();
		machine.forget_camera();
		if (native) {
			runtimes[p].attach(machine.cpu(), "vr");
			rec::reset_fallbacks();
		}
	}
	std::array<m1comm::memory, 2> snapshots{};
	for (int p = 0; p < 2; p++)
		machines[p]->enable_link([&, p](const auto &, auto &peer) {
			peer = snapshots[1 - p];
			return true;
		}, true);
	machines[2]->enable_link([](const auto &, auto &) {
		assert(false && "the test relay must never publish racing data");
		return false;
	}, true);
	std::array<int, 3> previous_phase{-1, -1, -1};
	std::array<uint32_t, 3> previous_camera{};
	std::array<std::deque<std::array<uint32_t, 3>>, 2> positions;
	std::array<std::set<std::array<uint32_t, 3>>, 2> replicated_positions;
	std::array<int, 2> replicated{};
	std::set<uint32_t> broadcast_cameras;
	int racing = 0, monitor_pictures = 0, audible_frames = 0;
	uint64_t last_link_tick = 0;
	for (int frame = 0; frame < 7000; frame++) {
		const auto tick = machines[0]->link()->ticks();
		const bool capture = tick >= 600 && tick != last_link_tick && tick % 300 == 0;
		for (int p : {0, 2}) machines[p]->set_drawing(capture);
		for (int p = 0; p < 2; p++) {
			auto &machine = *machines[p];
			const bool start = (tick >= 400 && tick < 406) || (tick >= 520 && tick < 526);
			machine.m_inputs[0] = uint8_t(~(start ? 0x10 : 0));
			assert(machine.set_analog_input(0, 0x80));
			assert(machine.set_analog_input(1, p ? 0x90 : 0xff));
			assert(machine.set_analog_input(2, 0x30));
		}
		for (int line = 0; line < model1::FRAME_LINES; line++) {
			if (line == model1::FRAME_VBLANK &&
				machines[0]->link()->ready() && machines[1]->link()->ready()) {
				for (int p = 0; p < 2; p++) snapshots[p] = machines[p]->link()->snapshot();
				for (int p = 0; p < 2; p++) machines[p]->link()->tick();
				auto &relay = *machines[2]->link();
				if ((relay.cn() & 1) && relay.read(1) == 0) receive_live(relay, snapshots);
			}
			for (auto &machine : machines) machine->run_scanline();
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
				memcpy(&position[axis], &snapshots[p][0x10 + 0x1e + axis * 4], 4);
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
				if (p == 2 && machine.frame_quads() > 0) monitor_pictures++;
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
}
