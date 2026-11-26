#include "../../../src/games/wingwar/assignments.h"
#include "../../../src/games/wingwar/profile.h"
#include "../../../src/host/model1_lan.h"
#include "../../../src/host/regions.h"
#include "../../../src/machine/parallel.h"
#include "../../../src/rt/native.h"
#include <cassert>
#include <cstring>

int main(int argc, char **argv)
{
	assert(argc == 2);
	const lan_config config{{'T', 'W', 'W', '1'}, 1, 29880, 0, 0, 0, LAN_LINK, 0, 2};
	lan_session host(config), guest(config);
	assert(host.open("HOST") && guest.open("GUEST") && host.host() && guest.search());
	uint64_t now = 0;
	auto poll = [&] {
		now += 17;
		host.update(now);
		guest.update(now);
	};
	for (int i = 0; i < 180 && guest.rooms().empty(); i++) poll();
	assert(!guest.rooms().empty() && guest.join(guest.rooms().front()));
	for (int i = 0; i < 180 && !guest.in_room(); i++) poll();
	assert(guest.in_room() && host.start({}));
	for (int i = 0; i < 180 && guest.role() != LAN_PLAYING; i++) poll();

	std::array<std::unique_ptr<model1>, 2> machines;
	std::array<rec::context, 2> runtimes;
	std::array<model1_lan, 2> adapters;
	std::array<lan_session *, 2> sessions{&host, &guest};
	rec::hints hints;
	assert(hints.read("assets/games/wingwar/wingwar_map.txt"));
	for (int p = 0; p < 2; p++) {
		machines[p] = std::make_unique<model1>(load_regions(argv[1], game::wingwar::ID));
		auto &machine = *machines[p];
		const auto settings = game::wingwar::new_eeprom(machine);
		assert(settings.size() == model1::EEPROM_SIZE);
		memcpy(machine.eeprom(), settings.data(), settings.size());
		runtimes[p].attach(machine.cpu(), game::wingwar::ID.name);
		assert(rec::build_native(machine.maincpu_rom(), hints));
		machine.run_cpu = [&, p](int n) { runtimes[p].run(n); };
		machine.set_drawing(false);
		machine.reset();
		runtimes[p].attach(machine.cpu(), game::wingwar::ID.name);
		for (int frame = 0; frame < 700; frame++) machine.run_frame();
		const auto coins = game::wingwar::coin_setting(machine);
		assert(coins >= 0);
		assert(game::wingwar::set_coin_setting(machine, game::wingwar::free_play_coin_setting(coins)));
		assert(game::wingwar::set_link_role(machine, p + 1));
		assert(adapters[p].begin(*sessions[p], 123, {}, 2));
		adapters[p].attach(machine);
		machine.reset();
		runtimes[p].attach(machine.cpu(), game::wingwar::ID.name);
	}
	std::array<int, 2> frames{}, position_changes{};
	std::array<uint32_t, 2> positions{};
	std::array<bool, 2> playing{};
	for (int iteration = 0; iteration < 30000 && (!playing[0] || !playing[1] ||
			position_changes[0] < 30 || position_changes[1] < 30); iteration++) {
		poll();
		for (int p = 0; p < 2; p++) assert(adapters[p].update(*sessions[p], now, *machines[p]));
		for (int p = 0; p < 2; p++) {
			auto &machine = *machines[p];
			if (!adapters[p].ready_for_frame(machine)) continue;
			const int frame = ++frames[p];
			const int start = 1000 + p * 200;
			machine.m_inputs[0] = uint8_t(~(frame >= start && frame < start + 8 ? 0x10 : 0));
			if (frame >= 3650) {
				assert(machine.set_analog_input(0, p ? 0x20 : 0xe0));
				assert(machine.set_analog_input(1, p ? 0xe0 : 0x20));
				assert(machine.set_analog_input(2, 0xff));
			}
			adapters[p].run_frame(machine);
			machine.m_audio_out.clear();
			if (machine.read8(0x40f006) == 0x0c && machine.read8(0x40f007) == 0x0f) playing[p] = true;
			if (frame >= 3800 && machine.link()->read(3) == 2) {
				const auto position = machine.read32(0x40e000 + (1 - p) * 0x1c4 + 0x38);
				if (position && positions[p] && position != positions[p]) position_changes[p]++;
				positions[p] = position;
			}
		}
	}
	for (int p = 0; p < 2; p++) {
		assert(playing[p] && position_changes[p] >= 30 && machines[p]->link()->read(3) == 2);
		adapters[p].stop(*machines[p]);
	}
	puts("Wing War LAN: both ROMs entered play and exchanged changing aircraft positions");

	auto &machine = *machines[0];
	machine.set_drawing(true);
	machine.set_render_scale(2);
	machine.set_aspect(16.0f / 9);
	machine.set_smooth(true);
	machine.set_parts_mode(true);
	model1::gpu_parts parts;
	side_job worker;
	int early_frames = 0;
	for (int frame = 0; frame < 300 && !early_frames; frame++) {
		machine.run_frame();
		machine.m_audio_out.clear();
		const uint64_t before = machine.frame_number();
		machine.machine_free = [&] {
			++early_frames;
			worker.start([&] {
				machine.set_defer_capture(true);
				machine.run_frame();
				machine.set_defer_capture(false);
			});
		};
		machine.render_parts(double(before) - 0.5, parts);
		machine.machine_free = nullptr;
		worker.wait();
		if (early_frames) {
			machine.finish_capture();
			assert(machine.frame_number() == before + 1);
		}
	}
	assert(early_frames == 1);
	puts("Wing War: mid-picture worker handoff completed without inspecting pixels");
}
