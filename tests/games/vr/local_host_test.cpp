#include "../../../src/host/game_host.h"
#include "../../../src/games/vr/profile.h"
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
	model1 machine(std::move(regions));
	machine.work_ram()[0x1138] = 0x14;
	machine.write8(0x40dc8c, 2);
	machine.write8(0x40d826, 3);
	const auto status = host->local_status(machine);
	assert(status.phase == 0x14 && status.role == 2 && status.participants == 3);
	assert(host->local_boot_complete(machine) == machine.past_boot());
	puts("VR local multiplayer host capabilities and diagnostics passed");
}
