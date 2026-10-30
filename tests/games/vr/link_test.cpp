#include "../../../src/host/local_link.h"
#include "../../../src/host/regions.h"
#include "../../../src/games/vr/gameplay.h"
#include "../../../src/rt/native.h"
#include "../../../src/machine/linked_pair.h"
#include "../../../src/host/input_replay.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <thread>
#include <deque>
#ifndef _WIN32
#include <sys/wait.h>
#endif

static void board_protocol()
{
	m1comm::memory sent{}, peer{};
	peer[1] = 2;
	peer[4] = 1;
	std::fill_n(peer.begin() + 0x10, 0x1c4, 0x52);
	m1comm board([&](const auto &out, auto &in) { sent = out; in = peer; return true; });
	assert(board.cn() == 0xfe && board.fg() == 0xfe);
	board.tick();
	assert(board.ticks() == 0);
	board.write(1, 1);
	board.write(4, 1);
	for (unsigned i = 0x10; i < 0x1d4; i++) board.write(i, 0x31);
	board.set_cn(1);
	board.set_fg(1);
	board.tick();
	assert(board.cn() == 0xff && board.fg() == 0x7f);
	assert(board.read(0) == 1 && board.read(2) == 1 && board.read(3) == 2);
	assert(board.read(5) == 0 && board.ticks() == 1);
	for (unsigned i = 0; i < 0x1c4; i++) {
		assert(sent[0x10 + i] == 0x31);
		assert(board.read(0x1d4 + i) == 0x31);
		assert(board.read(0x398 + i) == 0x52);
	}
	peer[4] = 0;
	peer[0x10] = 0x77;
	board.tick();
	assert(board.read(0x398) == 0x52);
	board.reset();
	assert(board.cn() == 0xfe && board.fg() == 0xfe && board.read(0x398) == 0);
	m1comm slave([&](const auto &, auto &in) {
		in = peer; in[1] = 1; in[6] = 0xa5; in[15] = 0x5a; return true;
	});
	slave.write(1, 2);
	slave.set_cn(1);
	slave.tick();
	assert(slave.read(2) == 2 && slave.read(6) == 0xa5 && slave.read(15) == 0x5a);
	m1comm broken([](const auto &, auto &) { return false; });
	broken.write(1, 1);
	broken.set_cn(1);
	broken.tick();
	assert(broken.read(0) == 0xff && broken.fg() == 0xfe);
	broken.tick();
	assert(broken.read(0) == 0xff);
}

static void in_process(const std::string &dir)
{
	rec::hints map;
	assert(map.read("assets/games/vr/vr_map.txt"));
	uint64_t reference900 = 0, reference960 = 0;
	uint64_t reference_audio = 1469598103934665603ull;
	uint64_t reference_effects = 1469598103934665603ull;
	std::array<uint64_t, 2> reference_picture{};
	auto audio_hash = [](uint64_t &hash, model1 &machine) {
		for (int16_t sample : machine.m_audio_out) hash = (hash ^ uint16_t(sample)) * 1099511628211ull;
		machine.m_audio_out.clear();
	};
	auto picture_hash = [](model1 &machine) {
		uint64_t hash = 1469598103934665603ull;
		for (size_t i = 0; i < size_t(machine.frame_w()) * machine.frame_h(); i++)
			hash = (hash ^ machine.frame().data()[i]) * 1099511628211ull;
		return hash;
	};
	{
		model1 reference(load_regions(dir, game::id{"vr"}));
		rec::context runtime;
		runtime.attach(reference.cpu(), "vr");
		assert(rec::build_native(reference.maincpu_rom(), map));
		reference.run_cpu = [&](int n) { runtime.run(n); };
		reference.set_drawing(false);
		reference.enable_sound();
		reference.reset();
		for (int f = 0; f < 960; f++) {
			reference.run_frame();
			if (f < 900) audio_hash(reference_audio, reference);
			else reference.m_audio_out.clear();
			if (f == 899) {
				reference900 = input_replay::ram_hash(reference);
				for (int scale = 1; scale <= 2; scale++) {
					reference.set_render_scale(scale);
					reference.redraw();
					reference_picture[scale - 1] = picture_hash(reference);
				}
			}
		}
		reference960 = input_replay::ram_hash(reference);
		assert(game::vr::in_attract_mode(reference));
		for (uint8_t phase : {0x03, 0x07, 0x10, 0x12, 0x14, 0x19, 0x1c, 0x1e}) {
			reference.work_ram()[0x1138] = phase;
			assert(game::vr::in_attract_mode(reference) == (phase == 0x03 || phase == 0x07));
		}
		reference.work_ram()[0x1138] = 0x03;
		reference.forget_camera();
		assert(!game::vr::in_attract_mode(reference));
	}
	{
		model1 reference(load_regions(dir, game::id{"vr"}));
		rec::context runtime;
		runtime.attach(reference.cpu(), "vr");
		assert(rec::build_native(reference.maincpu_rom(), map));
		reference.run_cpu = [&](int n) { runtime.run(n); };
		reference.set_drawing(false);
		reference.enable_sound();
		reference.set_volumes(0, 1);
		reference.reset();
		for (int frame = 0; frame < 900; frame++) {
			reference.run_frame();
			audio_hash(reference_effects, reference);
		}
	}
	std::array<std::unique_ptr<model1>, 2> machines;
	std::array<rec::context, 2> runtimes;
	for (int p = 0; p < 2; p++) {
		machines[p] = std::make_unique<model1>(load_regions(dir, game::id{"vr"}));
		runtimes[p].attach(machines[p]->cpu(), "vr");
		assert(rec::build_native(machines[p]->maincpu_rom(), map));
		machines[p]->run_cpu = [&, p](int n) { runtimes[p].run(n); };
		machines[p]->set_drawing(false);
		machines[p]->set_render_scale(p + 1);
		machines[p]->enable_sound();
		machines[p]->set_volumes(p == 0 ? 1 : 0, 1);
		machines[p]->reset();
	}
	std::array<uint64_t, 2> audio{1469598103934665603ull, 1469598103934665603ull};
	for (int frame = 0; frame < 900; frame++) {
		for (int line = 0; line < model1::FRAME_LINES; line++)
			for (auto &machine : machines) machine->run_scanline();
		for (int p = 0; p < 2; p++) audio_hash(audio[p], *machines[p]);
	}
	for (auto &machine : machines) assert(input_replay::ram_hash(*machine) == reference900);
	for (int p : {1, 0, 1}) {
		machines[p]->redraw();
		assert(picture_hash(*machines[p]) == reference_picture[p]);
		assert(audio[p] == (p == 0 ? reference_audio : reference_effects));
	}
	puts("Independent surfaces: cabinet 1 full audio, cabinet 2 effects-only audio, and 1x/2x pictures match standalone cabinets");

	// Rebuilding/resetting A must not invalidate B's suspended stack, dispatch or code.
	machines[0]->reset();
	runtimes[0].attach(machines[0]->cpu(), "vr");
	assert(rec::build_native(machines[0]->maincpu_rom(), map));
	for (int frame = 0; frame < 900; frame++) { machines[0]->run_frame(); machines[0]->m_audio_out.clear(); }
	for (int frame = 0; frame < 60; frame++) { machines[1]->run_frame(); machines[1]->m_audio_out.clear(); }
	assert(input_replay::ram_hash(*machines[1]) == reference960);
	assert(machines[0]->cpu().m_cycles != machines[1]->cpu().m_cycles);
	puts("Independent runtimes: interleaved boot matches single-cabinet RAM; rebuilding A preserves suspended B");

	for (int p = 0; p < 2; p++) {
		assert(game::vr::set_coin_setting(*machines[p], game::vr::FREE_PLAY));
		assert(game::vr::set_link_player(*machines[p], p + 1));
		machines[p]->reset();
		runtimes[p].attach(machines[p]->cpu(), "vr");
	}
	linked_pair pair(*machines[0], *machines[1]);
	std::array<int, 2> racing{}, replicated{}, moving{};
	std::array<bool, 2> audible{};
	std::array<std::deque<std::array<uint32_t, 3>>, 2> positions;
	std::array<std::array<uint32_t, 3>, 2> previous{};
	for (int frame = 0; frame < 6500 && machines[0]->link()->ticks() < 5000; frame++) {
		const auto tick = machines[0]->link()->ticks();
		for (int p = 0; p < 2; p++) {
			auto &machine = *machines[p];
			const bool start = (tick >= 2400 && tick < 2406) || (tick >= 2520 && tick < 2526);
			machine.m_inputs[0] = uint8_t(~(start ? 0x10 : 0));
			assert(machine.set_analog_input(0, 0x80));
			assert(machine.set_analog_input(1, p ? 0x90 : 0xff));
			assert(machine.set_analog_input(2, 0x30));
		}
		pair.run_frame();
		for (int p = 0; p < 2; p++) {
			for (int16_t sample : machines[p]->m_audio_out) audible[p] |= sample != 0;
			machines[p]->m_audio_out.clear();
		}
		assert(machines[0]->link()->ticks() == machines[1]->link()->ticks());
		for (int p = 0; p < 2; p++) {
			std::array<uint32_t, 3> position{};
			for (int axis = 0; axis < 3; axis++)
				memcpy(&position[axis], &machines[p]->link()->snapshot()[0x10 + 0x1e + axis * 4], 4);
			positions[p].push_back(position);
			if (positions[p].size() > 8) positions[p].pop_front();
		}
		for (int p = 0; p < 2; p++) {
			auto &machine = *machines[p];
			if (machine.work_ram()[0x1138] != 0x14) continue;
			++racing[p];
			assert(machine.read8(0x40d826) == 2);
			std::array<uint32_t, 3> remote{};
			const uint32_t car = machine.read32(0x501324);
			for (int axis = 0; axis < 3; axis++) remote[axis] = machine.read32(car + 0x1e + axis * 4);
			if (std::find(positions[1 - p].begin(), positions[1 - p].end(), remote) != positions[1 - p].end()) ++replicated[p];
			if (remote != previous[p]) ++moving[p];
			previous[p] = remote;
		}
	}
	std::array<size_t, 2> fallbacks{};
	for (int p = 0; p < 2; p++) {
		fprintf(stderr, "in-process cabinet %d: %llu exchanges, %d race frames, %d exact peer positions, %d moving\n",
			p + 1, (unsigned long long)machines[p]->link()->ticks(), racing[p], replicated[p], moving[p]);
		assert(machines[p]->link()->ticks() == 5000 && racing[p] >= 120 && replicated[p] >= 100 && moving[p] >= 100);
		assert(audible[p]);
		runtimes[p].activate();
		fallbacks[p] = rec::fallback_count();
		fprintf(stderr, "cabinet %d: %zu interpreter fallback calls, first %06x\n", p + 1, fallbacks[p], rec::first_fallback_address());
	}
	runtimes[0].activate();
	rec::reset_fallbacks();
	runtimes[1].activate();
	assert(rec::fallback_count() == fallbacks[1]);
	pair.disconnect();
	assert(!machines[0]->link() && !machines[1]->link());
	bool disconnected = false;
	try { pair.run_frame(); }
	catch (const std::logic_error &) { disconnected = true; }
	assert(disconnected);
	pair.disconnect();
}

#ifndef _WIN32
static int unused_port()
{
	const int fd = socket(AF_INET, SOCK_STREAM, 0);
	assert(fd >= 0);
	sockaddr_in address{};
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	assert(bind(fd, reinterpret_cast<sockaddr *>(&address), sizeof address) == 0);
	socklen_t size = sizeof address;
	assert(getsockname(fd, reinterpret_cast<sockaddr *>(&address), &size) == 0);
	const int port = ntohs(address.sin_port);
	close(fd);
	return port;
}

static void transport()
{
	const int port = unused_port();
	local_link master, slave;
	assert(master.open(1, port) && slave.open(2, port));
	auto run = [](local_link &link, int id) {
		for (int frame = 0; frame < 300; frame++) {
			m1comm::memory out{}, in{};
			out[1] = uint8_t(id);
			std::fill(out.begin() + 16, out.end(), uint8_t(frame + id));
			assert(link.exchange(out, in));
			assert(in[1] == 3 - id);
			for (size_t i = 16; i < in.size(); i++) assert(in[i] == uint8_t(frame + 3 - id));
		}
	};
	std::thread a([&] { run(master, 1); });
	std::thread b([&] { run(slave, 2); });
	a.join();
	b.join();
	assert(!master.failed() && !slave.failed());
}

static void disconnect()
{
	const int port = unused_port();
	local_link slave;
	assert(slave.open(2, port));
	{
		local_link master;
		assert(master.open(1, port));
		std::thread a([&] {
			m1comm::memory out{}, in{};
			out[1] = 1;
			assert(master.exchange(out, in));
		});
		m1comm::memory out{}, in{};
		out[1] = 2;
		assert(slave.exchange(out, in));
		a.join();
	}
	m1comm::memory out{}, in{};
	out[1] = 2;
	assert(!slave.exchange(out, in) && slave.failed());
}

static void mapping_and_settings()
{
	model1::regions regions;
	regions.title = game::id{"vr"};
	model1 machine(std::move(regions));
	assert(machine.read16(0xb00000) == 0xffff);
	machine.enable_link([](const auto &, auto &in) { in[1] = 2; return true; });
	machine.write32(0xb00000, 0x78563412);
	assert(machine.read32(0xb00000) == 0x78563412);
	machine.write8(0xb00001, 0xab);
	machine.write8(0xb00fff, 0xcd);
	assert(machine.read16(0xb00000) == 0xab12 && machine.read16(0xb00ffe) == 0xcd00);
	machine.write8(0xb01001, 1);
	assert(machine.read8(0xb01000) == 0xfe);
	machine.write8(0xb01000, 1);
	machine.write8(0xb01002, 1);
	assert(machine.read16(0xb01000) == 0xffff && machine.read16(0xb01002) == 0xffff);
	machine.link()->reset();
	assert(machine.read32(0xb00000) == 0 && machine.read8(0xb00fff) == 0);
	assert(machine.read8(0xb01000) == 0xfe && machine.read8(0xb01002) == 0xfe);

	std::fill_n(machine.eeprom(), model1::EEPROM_SIZE, 0);
	assert(!game::vr::set_link_player(machine, 1));
	assert(!game::vr::set_standalone(machine));
	machine.eeprom()[0] = 0x53; machine.eeprom()[1] = 0x45;
	machine.eeprom()[2] = 0x47; machine.eeprom()[3] = 0x41;
	machine.eeprom()[13] = 0x5a;
	assert(!game::vr::set_link_player(machine, 0));
	assert(!game::vr::set_link_player(machine, 3));
	for (int player : {1, 2, 0}) {
		assert(player ? game::vr::set_link_player(machine, player) : game::vr::set_standalone(machine));
		assert(machine.eeprom()[12] == player && machine.eeprom()[13] == 0x5a);
		assert(!(player ? game::vr::set_link_player(machine, player) : game::vr::set_standalone(machine)));
		uint16_t crc = 0;
		for (int i = 10; i < model1::EEPROM_SIZE; i++) {
			crc ^= uint16_t(machine.eeprom()[i]) << 8;
			for (int bit = 0; bit < 8; bit++) crc = uint16_t((crc << 1) ^ ((crc & 0x8000) ? 0x1021 : 0));
		}
		assert(machine.eeprom()[8] == uint8_t(crc) && machine.eeprom()[9] == uint8_t(crc >> 8));
	}
}

static int cabinet(const std::string &dir, int player, int port)
{
	model1 machine(load_regions(dir, game::id{"vr"}));
	machine.set_drawing(false);
	rec::hints map;
	assert(map.read("assets/games/vr/vr_map.txt"));
	auto mem = machine.maincpu_rom();
	mem.resize(0x2000000);
	rec::attach(machine.cpu(), "vr");
	assert(rec::build_native(mem, map));
	machine.run_cpu = [](int cycles) { rec::run(cycles); };
	machine.reset();
	for (int f = 0; f < 900; f++) machine.run_frame();
	assert(game::vr::set_coin_setting(machine, game::vr::FREE_PLAY));
	assert(game::vr::set_link_player(machine, player));
	local_link link;
	if (!link.open(player, port)) return 1;
	using position = std::array<uint32_t, 3>;
	std::deque<position> peer_positions;
	machine.enable_link([&](const auto &out, auto &in) {
		if (!link.exchange(out, in)) return false;
		position p{};
		memcpy(p.data(), in.data() + 0x10 + 0x1e, sizeof p);
		peer_positions.push_back(p);
		if (peer_positions.size() > 8) peer_positions.pop_front();
		return true;
	});
	machine.reset();
	rec::attach(machine.cpu(), "vr");
	int racing = 0, participants = 0, replicated = 0, separated = 0, moved = 0;
	position previous{};
	for (int frame = 0; machine.link()->ticks() < 5000 && frame < 6500; frame++) {
		const auto tick = machine.link()->ticks();
		const bool start = (tick >= 2400 && tick < 2406) || (tick >= 2520 && tick < 2526);
		machine.m_inputs[0] = uint8_t(~(start ? 0x10 : 0));
		assert(machine.set_analog_input(0, 0x80));
		assert(machine.set_analog_input(1, player == 1 ? 0xff : 0x90));
		assert(machine.set_analog_input(2, 0x30));
		machine.run_frame();
		if (link.failed()) return 1;
		if (machine.work_ram()[0x1138] == 0x14) {
			racing++;
			participants = std::max(participants, int(machine.read8(0x40d826)));
			const uint32_t own = machine.read32(0x501320), remote = machine.read32(0x501324);
			position own_position{}, remote_position{};
			for (int axis = 0; axis < 3; axis++) {
				own_position[axis] = machine.read32(own + 0x1e + axis * 4);
				remote_position[axis] = machine.read32(remote + 0x1e + axis * 4);
			}
			if (std::find(peer_positions.begin(), peer_positions.end(), remote_position) != peer_positions.end()) replicated++;
			if (own_position != remote_position) separated++;
			if (remote_position != previous) moved++;
			previous = remote_position;
		}
		if (tick % 600 == 0 && machine.link()->ticks() != tick)
			fprintf(stderr, "cabinet %d tick %llu: phase=%02x participants=%u player=%06x\n",
				player, (unsigned long long)tick, machine.work_ram()[0x1138], machine.read8(0x40d826), machine.read32(0x501320));
	}
	fprintf(stderr, "cabinet %d: %llu exchanges, %d racing frames, %d participants, %d exact peer positions, %d separated, %d moving\n",
		player, (unsigned long long)machine.link()->ticks(), racing, participants, replicated, separated, moved);
	if (machine.link()->ticks() != 5000 || racing < 120 || participants != 2 ||
		replicated < 100 || separated < 100 || moved < 100) return 1;
	return 0;
}
#endif

int main(int argc, char **argv)
{
	board_protocol();
	if (argc == 3 && !strcmp(argv[1], "--in-process")) {
		in_process(argv[2]);
		puts("VR in-process link tests passed");
		return 0;
	}
#ifndef _WIN32
	mapping_and_settings();
	if (argc == 3 && !strcmp(argv[1], "--roms")) {
		const int port = unused_port();
		const pid_t child = fork();
		assert(child >= 0);
		if (!child) _exit(cabinet(argv[2], 2, port));
		const int result = cabinet(argv[2], 1, port);
		int status = 0;
		assert(waitpid(child, &status, 0) == child);
		if (result || !WIFEXITED(status) || WEXITSTATUS(status)) return 1;
	} else {
		assert(argc == 1);
		transport();
		disconnect();
	}
#else
	assert(argc == 1);
#endif
	puts("VR link tests passed");
}
