#include "../../../src/host/game_host.h"
#include "../../../src/games/wingwar/profile.h"
#include "../../../src/games/wingwar/assignments.h"
#include "../../../src/host/regions.h"
#include <cassert>
#include <cstdio>
#include <cstring>

int main(int argc, char **argv)
{
	model1::regions regions;
	regions.title = game::wingwar::ID;
	model1 machine(std::move(regions));
	auto host = make_game_host();
	bool pc_mode = false;
	int service_calls = 0, settings_calls = 0;
	host_context context{machine, "", "", pc_mode, [&] { settings_calls++; }, {}, {},
		[&] { service_calls++; }};
	host->start(context);
	const auto items = host->game_menu_items();
	assert(items.size() == 7 && items.back().label == "Service menu");
	assert(items[0].label == "Game difficulty (1 easy - 8 hard)");
	const auto sound = host->sound_menu_items();
	assert(sound.size() == 1 && sound[0].label == "Advertise sound");
	for (const auto &item : items) assert(item.label != "Advertise sound");
	auto assignment_menu = [&] {
		auto game = host->game_menu_items();
		game.pop_back();
		const auto sound = host->sound_menu_items();
		game.insert(game.end(), sound.begin(), sound.end());
		return game;
	};
	assert(items.back().activate && !items.back().change && !items.back().value);
	items.back().activate();
	assert(service_calls == 1 && settings_calls == 0);
	assert(host->menu_closed() == game_host::restart::none);
	if (argc == 2) {
		model1 actual(load_regions(argv[1], game::wingwar::ID));
		actual.reset();
		for (int f = 0; f < 700; f++) actual.run_frame();
		host_context live{actual, "", "", pc_mode, [] {}, {}, {}, [] {}};
		host->start(live);
		const auto menu = assignment_menu();
		const auto original = actual.nvram();
		std::array<int, 7> expected;
		for (size_t i = 0; i < expected.size(); i++) {
			const int before = game::wingwar::assignment_value(actual, i);
			assert(before >= 0);
			menu[i].change(-1);
			assert(menu[i].value() == (i == 6 ? (before == 0 ? "Off" : "On") :
				std::to_string(game::wingwar::ASSIGNMENTS[i].first +
				((before + game::wingwar::ASSIGNMENTS[i].count - 1) %
				game::wingwar::ASSIGNMENTS[i].count) * game::wingwar::ASSIGNMENTS[i].step)));
			menu[i].change(1);
			menu[i].change(1);
			expected[i] = (before + 1) % game::wingwar::ASSIGNMENTS[i].count;
		}
		const auto refreshed = assignment_menu();
		for (size_t i = 0; i < expected.size(); i++) assert(refreshed[i].value() == menu[i].value());
		assert(host->menu_closed() == game_host::restart::game);
		for (size_t i = 0; i < expected.size(); i++) {
			assert(game::wingwar::assignment_value(actual, i) == expected[i]);
		}
		for (size_t offset = 0xe; offset < 0x44; offset++) {
			bool assignment = false;
			for (const auto &a : game::wingwar::ASSIGNMENTS) assignment |= offset == a.offset;
			if (!assignment) assert(actual.nvram()[0xf800 + offset] == original[0xf800 + offset]);
		}
		const auto saved = actual.nvram();
		auto invalid = expected;
		invalid[0] = 8;
		assert(!game::wingwar::set_assignments(actual, invalid));
		assert(actual.nvram() == saved);
		actual.reset();
		for (int f = 0; f < 700; f++) actual.run_frame();
		for (size_t i = 0; i < expected.size(); i++) {
			assert(game::wingwar::assignment_value(actual, i) == expected[i]);
		}
		live.replay = true;
		const auto replay_menu = host->sound_menu_items();
		replay_menu[0].change(1);
		assert(host->menu_closed() == game_host::restart::none);
		live.replay = false;
		std::array<int, 7> maximum;
		for (size_t i = 0; i < maximum.size(); i++) maximum[i] = game::wingwar::ASSIGNMENTS[i].count - 1;
		assert(game::wingwar::set_assignments(actual, maximum));
		const auto boundaries = assignment_menu();
		for (size_t i = 0; i < maximum.size(); i++) {
			boundaries[i].change(1);
			assert(boundaries[i].value() == (i == 6 ? "On" : std::to_string(game::wingwar::ASSIGNMENTS[i].first)));
			boundaries[i].change(-1);
			boundaries[i].change(1);
		}
		assert(host->menu_closed() == game_host::restart::game);
		assert(memcmp(actual.nvram().data() + 0xf808, actual.nvram().data() + 0xf844, 0x3c) == 0);
		model1 cold(load_regions(argv[1], game::wingwar::ID));
		memcpy(cold.eeprom(), actual.eeprom(), model1::EEPROM_SIZE);
		cold.reset();
		for (int f = 0; f < 700; f++) cold.run_frame();
		for (size_t i = 0; i < maximum.size(); i++) assert(game::wingwar::assignment_value(cold, i) == 0);
		host_context cold_context{cold, "", "", pc_mode, [] {}, {}, {}, [] {}};
		host->start(cold_context);
		auto advertise = host->sound_menu_items();
		assert(advertise[0].value() == "On");
		const auto on_label = cold.read32(0x120e4) + 3;
		const auto off_label = cold.read32(0x120e8) + 3;
		assert(cold.read8(on_label) == 'O' && cold.read8(on_label + 1) == 'N');
		assert(cold.read8(off_label) == 'O' && cold.read8(off_label + 1) == 'F' && cold.read8(off_label + 2) == 'F');
		advertise[0].change(1);
		assert(advertise[0].value() == "Off");
		assert(host->menu_closed() == game_host::restart::game);
		assert(cold.read8(0x40f818) == 1);
		cold.reset();
		for (int f = 0; f < 700; f++) cold.run_frame();
		advertise = host->sound_menu_items();
		assert(advertise[0].value() == "Off");
		advertise[0].change(1);
		assert(advertise[0].value() == "On");
		assert(host->menu_closed() == game_host::restart::game);
		cold.reset();
		for (int f = 0; f < 700; f++) cold.run_frame();
		assert(cold.read8(0x40f818) == 0);
		assert(host->sound_menu_items()[0].value() == "On");
		cold.nvram()[0xf800] = 0;
		const auto corrupt = cold.nvram();
		assert(game::wingwar::assignment_value(cold, 0) == -1);
		assert(!game::wingwar::set_assignments(cold, expected));
		assert(cold.nvram() == corrupt);
		puts("Wing War assignments: ROM checksum, all seven choices, EEPROM and reboot persistence passed");
	}
	puts("Wing War Game > Service menu dispatch passed");
}
