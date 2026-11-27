#include "../../../src/host/game_host.h"
#include "../../../src/games/vr/profile.h"
#include "../../../src/games/vr/controls.h"
#include "../../../src/host/cabinet_display.h"
#include <cassert>
#include <cstdio>

int main()
{
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
