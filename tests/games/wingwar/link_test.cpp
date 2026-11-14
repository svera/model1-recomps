#include "../../../src/games/wingwar/assignments.h"
#include "../../../src/games/wingwar/link.h"
#include "../../../src/games/wingwar/profile.h"
#include "../../../src/host/regions.h"
#include "../../../src/rt/native.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <memory>

static void checksum_settings(std::vector<uint8_t> &settings)
{
	uint32_t crc = 0xdeb00;
	for (int i = 0; i <= 0x3a; i++) {
		if (i < 0x3a) crc = (crc & 0xffffff00) | settings[0xa + i];
		for (int bit = 0; bit < 8; bit++)
			crc = (crc & 0x80000000) ? (crc << 1) ^ 0x10210000 : crc << 1;
	}
	settings[8] = uint8_t(crc >> 16);
	settings[9] = uint8_t(crc >> 24);
	memcpy(settings.data() + 0x44, settings.data() + 8, 0x3c);
}

int main(int argc, char **argv)
{
	if (argc != 2 && argc != 4) {
		fprintf(stderr, "usage: %s regions-dir [--native map]\n", argv[0]);
		return 2;
	}
	const bool native = argc == 4;
	if (native && strcmp(argv[2], "--native")) {
		fprintf(stderr, "expected --native followed by a map\n");
		return 2;
	}
	std::array<std::unique_ptr<model1>, 2> machines;
	std::array<std::unique_ptr<model1>, 2> reference;
	std::array<rec::context, 2> runtimes;
	rec::hints hints;
	if (native && !hints.read(argv[3])) {
		fprintf(stderr, "could not read native map %s\n", argv[3]);
		return 1;
	}
	for (int p = 0; p < 2; p++) {
		machines[p] = std::make_unique<model1>(load_regions(argv[1], game::wingwar::ID));
		auto &machine = *machines[p];
		assert(machine.real_ioboard() && machine.tgp_is_lle());
		machine.set_drawing(false);
		auto settings = game::wingwar::new_eeprom(machine);
		assert(settings.size() == model1::EEPROM_SIZE);
		settings[0x24] = 1;
		settings[0x25] = uint8_t(p + 1);
		settings[0xe] = game::wingwar::FREE_PLAY;
		settings[0xf] = (settings[0xf] & ~0xc0) | 0x80;
		checksum_settings(settings);
		memcpy(machine.eeprom(), settings.data(), settings.size());
		machine.reset();
		if (native) {
			reference[p] = std::make_unique<model1>(load_regions(argv[1], game::wingwar::ID));
			reference[p]->set_drawing(false);
			memcpy(reference[p]->eeprom(), settings.data(), settings.size());
			reference[p]->reset();
			runtimes[p].attach(machine.cpu(), game::wingwar::ID.name);
			if (!rec::build_native(machine.maincpu_rom(), hints)) {
				fprintf(stderr, "could not compile cabinet %d\n", p + 1);
				return 1;
			}
			machine.run_cpu = [&, p](int n) { runtimes[p].run(n); };
		}
	}
	auto link = std::make_unique<game::wingwar::local_link>(*machines[0], *machines[1]);
	std::unique_ptr<game::wingwar::local_link> reference_link;
	if (native) reference_link = std::make_unique<game::wingwar::local_link>(*reference[0], *reference[1]);
	auto each_machine = [&](int p, auto fn) {
		fn(*machines[p]);
		if (native) fn(*reference[p]);
	};
	uint64_t compared_frames = 0;
	auto compare = [&] {
		if (!native) return;
		for (int p = 0; p < 2; p++) {
			auto &a = *machines[p], &b = *reference[p];
			for (int r = 0; r < 68; r++) {
				if (a.cpu().reg(r) != b.cpu().reg(r)) {
					fprintf(stderr, "cabinet %d frame %llu: register %d native %08x interpreter %08x\n",
						p + 1, (unsigned long long)a.frame_number(), r, a.cpu().reg(r), b.cpu().reg(r));
					std::abort();
				}
			}
			assert(a.cpu().psw() == b.cpu().psw());
			assert(a.work_ram() == b.work_ram() && a.nvram() == b.nvram());
			assert(!memcmp(a.eeprom(), b.eeprom(), model1::EEPROM_SIZE));
			assert(a.tgp_cycles_executed() == b.tgp_cycles_executed());
			assert(a.advanced_ioboard_cycles() == b.advanced_ioboard_cycles());
			assert(bool(a.link()) == bool(b.link()));
			if (a.link()) assert(a.link()->snapshot() == b.link()->snapshot());
		}
		compared_frames++;
	};
	auto run_linked_frame = [&] {
		if (reference_link) reference_link->run_frame();
		link->run_frame();
		compare();
	};
	bool connected = false, guest_observed_start = false, both_started = false, both_advanced = false;
	bool both_finished = false, both_returned = false;
	std::array<std::deque<uint32_t>, 2> peer_positions;
	std::array<uint32_t, 2> previous_position{};
	std::array<int, 2> scene_matches{}, position_changes{}, timely_delivery{};
	std::array<uint64_t, 2> previous_ticks{};
	for (int frame = 1; frame <= 13000; frame++) {
		for (int p = 0; p < 2; p++) {
			const int start = 1000 + p * 200;
			each_machine(p, [&](model1 &m) {
				m.m_inputs[0] = uint8_t(~(frame >= start && frame < start + 8 ? 0x10 : 0));
				if (frame >= 3650) {
					assert(m.set_analog_input(0, p ? 0x20 : 0xe0));
					assert(m.set_analog_input(1, p ? 0xe0 : 0x20));
					assert(m.set_analog_input(2, 0xff));
				}
			});
		}
		run_linked_frame();
		auto &first = *machines[0];
		auto &second = *machines[1];
		if (frame >= 3800 && frame <= 4600) {
			for (int p = 0; p < 2; p++) {
				auto &m = *machines[p];
				const unsigned peer = unsigned(1 - p);
				const uint32_t slot = 0xb00010 + (peer + 1) * 0x1c4 + 0x30;
				const uint32_t record = 0x40e000 + peer * 0x1c4 + 0x30;
				const uint32_t scene = 0x40be00 + peer * 0x80;
				assert(m.read32(slot + 8) == machines[peer]->read32(slot + 8));
				assert(link->status(p).participants == 2);
				if (previous_ticks[p]) assert(link->status(p).exchanges == previous_ticks[p] + 1);
				previous_ticks[p] = link->status(p).exchanges;
				assert(m.read8(record + 4) == peer);
				const uint32_t position = m.read32(record + 8);
				if (previous_position[p] && position != previous_position[p]) position_changes[p]++;
				previous_position[p] = position;
				auto &history = peer_positions[p];
				history.push_back(m.read32(slot + 8));
				if (history.size() > 12) history.pop_front();
				if (position && std::find(history.begin(), history.end(), position) != history.end())
					timely_delivery[p]++;
				if (position && position == m.read32(scene + 8) &&
					m.read8(scene + 4) == peer) scene_matches[p]++;
			}
		}
		if (frame == 900) {
			for (int p = 0; p < 2; p++) {
				auto &m = *machines[p];
				assert(m.read8(0x40f824) == 1 && m.read8(0x40f825) == p + 1);
				assert(m.link()->read(0) == 1 && m.link()->read(1) == p + 1);
				assert(m.link()->read(2) == p + 1 && m.link()->read(3) == 2);
				assert(m.link()->ticks() > 0);
				const auto status = link->status(unsigned(p));
				assert(status.id == unsigned(p + 1) && status.participants == 2 &&
					status.exchanges == m.link()->ticks());
				assert(m.read8(0x40ef00) == p && m.read8(0x40ef01) == 2);
				assert(m.read8(0x40ef02) == 1 && m.read8(0x40ef03) == 0);
				assert(m.read8(0x40f006) == 2);
				assert(m.read8(0x40e000) == 1 && m.read8(0x40e1c4) == 2);
			}
			connected = true;
		}
		if (frame > 1000 && frame < 1200 && first.read8(0x40f006) == 0x0c &&
			second.read8(0x40f006) == 0x09 && second.read8(0x40e002) == 0x0c)
			guest_observed_start = true;
		if (frame >= 1200 && first.read8(0x40f006) == 0x0c && second.read8(0x40f006) == 0x0c)
			both_started = true;
		if (!both_advanced && both_started && first.read8(0x40f007) == 0x0f && second.read8(0x40f007) == 0x0f) {
			for (auto &machine : machines) {
				assert(machine->link()->read(4) == 1);
				assert(machine->read8(0x40e000) == 1 && machine->read8(0x40e1c4) == 2);
				assert(machine->read8(0x40e002) == 0x0c && machine->read8(0x40e1c6) == 0x0c);
			}
			both_advanced = true;
			printf("Wing War link: two boards connected; staggered Start presses advanced both ROMs "
				"to phase 0c/0f at frame %d\n", frame);
		}
		if (both_advanced && first.read8(0x40f006) == 0x0c && second.read8(0x40f006) == 0x0c &&
			first.read8(0x40f007) == 0x18 && second.read8(0x40f007) == 0x18) {
			both_finished = true;
			assert(link->status(0).participants == 2 && link->status(1).participants == 2);
		}
		if (both_finished && first.read8(0x40f006) == 2 && second.read8(0x40f006) == 2)
			both_returned = true;
	}
	assert(connected && guest_observed_start && both_started && both_advanced && both_finished && both_returned);
	puts("Wing War link: both cabinets left linked play and returned to the demonstration");
	assert(link->status(2).exchanges == 0);
	bool missing_native = false;
	auto check_native = [&] {
		if (!native) return;
		for (int p = 0; p < 2; p++) {
			runtimes[p].activate();
			if (rec::fallback_count()) {
				fprintf(stderr, "Wing War cabinet %d: %zu native fallbacks, first at %06x\n",
					p + 1, rec::fallback_count(), rec::first_fallback_address());
				missing_native = true;
			}
		}
	};
	check_native();
	for (int p = 0; p < 2; p++) {
		printf("Wing War cabinet %d: %d peer position changes, %d timely deliveries, %d scene copies\n",
			p + 1, position_changes[p], timely_delivery[p], scene_matches[p]);
		assert(position_changes[p] > 100 && timely_delivery[p] > 500 && scene_matches[p] > 350);
	}
	auto reset_machines = [&] {
		for (int p = 0; p < 2; p++) {
			each_machine(p, [](model1 &m) {
				std::fill_n(m.m_inputs, 3, uint8_t(0xff));
				m.reset();
			});
			if (native) runtimes[p].attach(machines[p]->cpu(), game::wingwar::ID.name);
		}
	};
	auto start_linked_play = [&] {
		for (int frame = 1; frame <= 4500; frame++) {
			for (int p = 0; p < 2; p++)
				each_machine(p, [&](model1 &m) {
					m.m_inputs[0] = uint8_t(~(frame >= 1000 && frame < 1008 ? 0x10 : 0));
				});
			run_linked_frame();
			if (frame == 900) {
				for (int p = 0; p < 2; p++)
					assert(link->status(p).participants == 2 && machines[p]->read8(0x40f006) == 2);
			}
			if (frame >= 1008 && machines[0]->read8(0x40f006) == 0x0c &&
				machines[1]->read8(0x40f006) == 0x0c &&
				machines[0]->read8(0x40f007) == 0x0f && machines[1]->read8(0x40f007) == 0x0f)
				return true;
		}
		return false;
	};
	reset_machines();
	assert(start_linked_play());
	check_native();
	reset_machines();
	assert(start_linked_play());
	check_native();
	link.reset();
	reference_link.reset();
	for (int p = 0; p < 2; p++) each_machine(p, [](model1 &m) {
		assert(!m.link());
		assert(game::wingwar::set_link_role(m, 0));
	});
	reset_machines();
	for (int frame = 0; frame < 900; frame++) {
		for (auto &machine : machines) machine->run_frame();
		if (native) for (auto &machine : reference) machine->run_frame();
		compare();
	}
	for (auto &machine : machines) {
		assert(game::wingwar::link_role(*machine) == 0);
		assert(machine->read8(0x40f006) == 2);
	}
	for (int frame = 0; frame < 20; frame++) {
		for (int p = 0; p < 2; p++)
			each_machine(p, [&](model1 &m) { m.m_inputs[0] = uint8_t(~(frame < 8 ? 0x10 : 0)); });
		for (auto &machine : machines) machine->run_frame();
		if (native) for (auto &machine : reference) machine->run_frame();
		compare();
	}
	for (auto &machine : machines) assert(machine->read8(0x40f006) == 0x0c);
	check_native();
	puts("Wing War link: reset during linked play, reconnect, disconnect and standalone boot passed");
	if (native && !missing_native) printf("Wing War link: %llu frames matched the interpreter with zero native fallbacks\n",
		(unsigned long long)compared_frames);
	if (missing_native) return 1;
}
