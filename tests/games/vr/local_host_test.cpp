#include "../../../src/host/game_host.h"
#include "../../../src/games/vr/profile.h"
#include "../../../src/games/vr/controls.h"
#include <cassert>
#include <cstdio>

int main()
{
	auto host = make_game_host();
	const auto *local = host->local_multiplayer();
	assert(local && local->live_monitor && host->supports_lan());
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
	context.link_player = 3;
	machine.m_inputs[1] &= uint8_t(~game::vr::CAMERA_REAR_INPUT);
	host->before_frame();
	assert(!machine.rear_view());
	puts("VR local multiplayer host capabilities and diagnostics passed");
}
