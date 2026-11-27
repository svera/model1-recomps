#include "../../../src/host/game_host.h"
#include "../../../src/games/vr/profile.h"
#include "../../../src/games/vr/controls.h"
#include "../../../src/games/script.h"
#include "../../../src/host/regions.h"
#include "../../../src/rt/native.h"
#include "../../../src/host/cabinet_display.h"
#include <cassert>
#include <cstdio>

static void race_restart_with_roms(const char *dir, bool native)
{
	model1 machine(load_regions(dir, game::vr::ID));
	rec::context runtime;
	machine.set_drawing(false);
	auto host = make_game_host();
	bool pc_mode = true;
	host_context context{.machine = machine, .pc_mode = pc_mode};
	host->start(context);
	machine.reset();
	if (native) {
		rec::hints map;
		assert(map.read("assets/games/vr/vr_map.txt"));
		runtime.attach(machine.cpu(), "vr");
		assert(rec::build_native(machine.maincpu_rom(), map));
		machine.run_cpu = [&](int cycles) { runtime.run(cycles); };
		context.restart_cpu_runtime = [&] { runtime.reset_run_state(); };
	}
	auto play = game::make_auto_play(game::vr::ID, "", 12345, false);
	assert(play);
	int racing = 0, restarts = 0;
	bool saw_transition = false;
	for (int frame = 1; frame <= 6500; frame++) {
		const auto input = play->frame(frame, machine);
		for (size_t port = 0; port < input.pressed.size(); port++)
			machine.m_inputs[port] = uint8_t(~input.pressed[port]);
		for (size_t channel = 0; channel < input.analog.size(); channel++)
			if (input.analog[channel] >= 0)
				assert(machine.set_analog_input(int(channel), uint8_t(input.analog[channel])));
		host->before_frame();
		machine.run_frame();
		host->after_frame();
		const int phase = machine.work_ram()[0x1138];
		if (phase == 0x13) saw_transition = true;
		if (phase == 0x14 && ++racing == 60) {
			assert(saw_transition);
			assert(host->quick_restart_race());
			assert(machine.work_ram()[0x1138] == 0x12);
			if (++restarts == 2) {
				printf("VR real-ROM countdown and repeated race restart passed (%s)\n",
					native ? "native" : "interpreter");
				return;
			}
			racing = 0;
			saw_transition = false;
		}
	}
	assert(false && "script did not reach an active race");
}

int main(int argc, char **argv)
{
	if (argc == 2 || (argc == 3 && std::string(argv[2]) == "--native")) {
		race_restart_with_roms(argv[1], argc == 3 && std::string(argv[2]) == "--native");
		return 0;
	}
	if (argc != 1) {
		fprintf(stderr, "Usage: %s [regions-directory [--native]]\n", argv[0]);
		return 1;
	}
	for (auto size : {SDL_Point{800, 600}, SDL_Point{801, 601}})
		for (auto layout : {cabinet_layout::side_by_side, cabinet_layout::top_bottom}) {
			const int w = size.x, h = size.y, half_w = w / 2, half_h = h / 2;
			auto check = [&](int player, int count, bool monitor, SDL_Rect expected) {
				const auto actual = cabinet_composition_viewport(layout, w, h, player, float(w) / h, count, monitor);
				assert(actual.x == expected.x && actual.y == expected.y &&
					actual.w == expected.w && actual.h == expected.h);
			};
			check(0, 3, false, {0, 0, w, half_h});
			check(1, 3, false, {0, half_h, half_w, h - half_h});
			check(2, 3, false, {half_w, half_h, w - half_w, h - half_h});
			for (int slot = 0; slot < 4; slot++) {
				const SDL_Rect quadrant{slot % 2 ? half_w : 0, slot / 2 ? half_h : 0,
					slot % 2 ? w - half_w : half_w, slot / 2 ? h - half_h : half_h};
				check(slot, 4, false, quadrant);
				check(slot ? slot - 1 : 3, 3, true, quadrant);
			}
		}
	auto host = make_game_host();
	const auto *local = host->local_multiplayer();
	assert(local && local->live_monitor && local->max_players == 4 && host->supports_lan());
	const auto lan = host->local_lan_config();
	assert(lan.game[0] == 'M' && lan.game[1] == '1' && lan.game[2] == 'V' && lan.game[3] == 'R');
	assert(lan.version == 3 && lan.beacon_port == 19794 && lan.mode == LAN_LINK && lan.max_devices == 4);

	model1::regions regions;
	regions.title = game::vr::ID;
	regions.maincpu.resize(0x1000000);
	model1 machine(std::move(regions));
	machine.work_ram()[0x1138] = 0x14;
	machine.write8(0x40dc8c, 2);
	machine.write8(0x40d826, 3);
	const auto status = host->local_status(machine);
	assert(status.phase == 0x14 && status.role == 2 && status.participants == 3);
	assert(host->local_boot_complete(machine) == machine.past_boot());
	bool pc_mode = true;
	host_context context{.machine = machine, .pc_mode = pc_mode};
	bool runtime_reset = false;
	context.restart_cpu_runtime = [&] { runtime_reset = true; };
	host->start(context);
	for (int port : {1, 2}) {
		std::fill(std::begin(machine.m_inputs), std::end(machine.m_inputs), uint8_t(0xff));
		machine.m_inputs[port] &= uint8_t(~game::vr::CAMERA_REAR_INPUT);
		host->before_frame();
		assert(machine.rear_view());
		assert((machine.m_inputs[port] & game::vr::CAMERA_REAR_INPUT) != 0);
		assert(!machine.free_cam());
		machine.m_inputs[port] = 0xff;
		host->before_frame();
		assert(!machine.rear_view());
	}
	machine.work_ram()[0x1138] = 0x10;
	machine.m_inputs[1] &= uint8_t(~game::vr::CAMERA_REAR_INPUT);
	host->before_frame();
	assert(!machine.rear_view());
	machine.work_ram()[0x1138] = 0x14;
	assert(host->supports_quick_restart_race());
	machine.work_ram()[0x1138] = 0x12;
	machine.cpu().set_reg(0, 0x12345678);
	*machine.cpu().m_cycles = 73;
	machine.work_ram()[0x200] = 0x5a;
	host->before_frame();
	host->after_frame();
	machine.work_ram()[0x1138] = 0x13;
	host->before_frame();
	host->after_frame();
	machine.work_ram()[0x1138] = 0x14;
	host->after_frame();
	for (int frame = 0; frame < 3; frame++) {
		host->before_frame();
		host->after_frame();
	}
	machine.cpu().set_reg(0, 0x87654321);
	*machine.cpu().m_cycles = 99;
	machine.work_ram()[0x200] = 0xa5;
	assert(host->quick_restart_race());
	assert(machine.cpu().reg(0) == 0x12345678 && *machine.cpu().m_cycles == 73 &&
		machine.work_ram()[0x200] == 0x5a && machine.work_ram()[0x1138] == 0x12);
	assert(runtime_reset);
	host->before_frame();
	host->after_frame();
	machine.work_ram()[0x1138] = 0x14;
	host->before_frame();
	host->after_frame();
	machine.work_ram()[0x200] = 0xff;
	assert(host->quick_restart_race());
	assert(machine.work_ram()[0x1138] == 0x12 && machine.work_ram()[0x200] == 0x5a);
	machine.work_ram()[0x1138] = 0x10;
	host->after_frame();
	assert(!host->quick_restart_race());
	machine.work_ram()[0x1138] = 0x14;
	context.link_player = 3;
	machine.m_inputs[1] &= uint8_t(~game::vr::CAMERA_REAR_INPUT);
	host->before_frame();
	assert(!machine.rear_view());
	host->after_frame();
	assert(!host->quick_restart_race());
	puts("VR local multiplayer host capabilities and diagnostics passed");
}
