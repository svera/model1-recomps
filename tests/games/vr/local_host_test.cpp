#include "../../../src/host/game_host.h"
#include "../../../src/games/vr/profile.h"
#include "../../../src/games/vr/controls.h"
#include "../../../src/games/vr/gameplay.h"
#include "../../../src/games/vr/link.h"
#include "../../../src/host/race_replay.h"
#include "../../../src/games/script.h"
#include "../../../src/host/regions.h"
#include "../../../src/rt/native.h"
#include "../../../src/host/cabinet_display.h"
#include "../../../src/machine/linked_pair.h"
#include <cassert>
#include <cstdio>

static void race_replay_with_roms(const char *dir, bool native)
{
	model1 live(load_regions(dir, game::vr::ID));
	live.set_drawing(false);
	live.enable_sound();
	auto host = make_game_host();
	bool pc_mode = true;
	host_context context{.machine = live, .pc_mode = pc_mode};
	host->start(context);
	live.reset();
	rec::context live_runtime, replay_runtime;
	rec::hints map;
	if (native) {
		assert(map.read("assets/games/vr/vr_map.txt"));
		live_runtime.attach(live.cpu(), "vr");
		assert(rec::build_native(live.maincpu_rom(), map));
		live.run_cpu = [&](int cycles) { live_runtime.run(cycles); };
	}
	auto play = game::make_auto_play(game::vr::ID, "views", 12345, false);
	assert(play);
	int frame = 0;
	while (live.work_ram()[0x1138] != 0x14 && frame < 6500) {
		assert(input_replay::apply(live, play->frame(++frame, live)).empty());
		host->before_frame();
		live.run_frame();
		host->after_frame();
		live.m_audio_out.clear();
	}
	assert(live.work_ram()[0x1138] == 0x14);
	live.read16(0xc40000);
	live.audio()->reset();
	model1::simulation_state checkpoint;
	assert(live.save_simulation_state(checkpoint));
	auto live_game = std::dynamic_pointer_cast<game::vr::gameplay_state>(live.game_state());
	assert(live_game);
	const auto gameplay = *live_game;
	race_replay::race_recorder recorder(live);
	assert(recorder.start().empty());
	const auto initial_view = live.read8(0x501d02);
	bool changed_view = false;
	for (int i = 0; i < 1800; i++) {
		assert(input_replay::apply(live, play->frame(++frame, live)).empty());
		host->before_frame();
		assert(recorder.begin_frame().empty());
		if (!i) live.audio()->write8(0xc20001, 0xa5);
		live.run_frame();
		changed_view |= live.read8(0x501d02) != initial_view;
		assert(recorder.end_frame().empty());
		host->after_frame();
		live.m_audio_out.clear();
	}
	recorder.stop();
	assert(changed_view);
	bool captured_reply = false;
	for (const auto &recorded : recorder.data().frames)
		for (const auto &event : recorded.sound) captured_reply |= event.value == 0xa5;
	assert(captured_reply);
	model1 replay(load_regions(dir, game::vr::ID));
	replay.set_drawing(false);
	if (native) {
		replay_runtime.attach(replay.cpu(), "vr");
		assert(rec::build_native(replay.maincpu_rom(), map));
		replay.run_cpu = [&](int cycles) { replay_runtime.run(cycles); };
	}
	auto replay_game = std::dynamic_pointer_cast<game::vr::gameplay_state>(replay.game_state());
	assert(replay_game);
	for (int pass = 0; pass < 2; pass++) {
		assert(replay.load_simulation_state(checkpoint));
		*replay_game = gameplay;
		if (native) replay_runtime.reset_run_state();
		for (const auto &recorded : recorder.data().frames) {
			const auto error = race_replay::replay_race_frame(replay, recorded);
			if (!error.empty()) fprintf(stderr, "VR replay: %s\n", error.c_str());
			assert(error.empty());
		}
	}
	race_replay::race_player player(replay, recorder.share(), [&] {
		if (!replay.load_simulation_state(checkpoint)) return std::string("checkpoint restore failed");
		*replay_game = gameplay;
		if (native) replay_runtime.reset_run_state();
		return std::string();
	});
	assert(!player.step().empty());
	assert(player.start(120).empty());
	assert(player.advance(30).empty() && player.position() == 30 && player.seeking());
	assert(player.advance(90).empty() && player.position() == 120 && !player.seeking());
	assert(player.seek(40).empty());
	assert(player.advance(40).empty() && player.position() == 40);
	assert(player.step().empty() && player.position() == 41);
	assert(!player.seek(player.length() + 1).empty());
	assert(recorder.start().empty());
	recorder.stop();
	assert(player.seek(30).empty());
	assert(player.advance(30).empty() && player.position() == 30);
	printf("VR replay: 1800 recorded race frames and seek-to-start passed (%s)\n",
		native ? "native" : "interpreter");
}

static void linked_replay_with_roms(const char *dir, bool native)
{
	std::array<std::unique_ptr<model1>, 2> machines;
	std::array<rec::context, 2> runtimes;
	rec::hints map;
	if (native) assert(map.read("assets/games/vr/vr_map.txt"));
	for (int p = 0; p < 2; p++) {
		machines[p] = std::make_unique<model1>(load_regions(dir, game::vr::ID));
		auto &machine = *machines[p];
		machine.set_drawing(false);
		machine.enable_sound();
		machine.reset();
		if (native) {
			runtimes[p].attach(machine.cpu(), "vr");
			assert(rec::build_native(machine.maincpu_rom(), map));
			machine.run_cpu = [&, p](int cycles) { runtimes[p].run(cycles); };
		}
		for (int f = 0; f < 900; f++) {
			machine.run_frame();
			machine.m_audio_out.clear();
		}
		assert(game::vr::set_coin_setting(machine, game::vr::FREE_PLAY));
		assert(game::vr::set_link_player(machine, p + 1));
		machine.reset();
		if (native) runtimes[p].attach(machine.cpu(), "vr");
	}
	linked_pair pair(*machines[0], *machines[1]);
	auto inputs = [&] {
		const auto tick = machines[0]->link()->ticks();
		const bool start = (tick >= 1000 && tick < 1006) || (tick >= 1120 && tick < 1126);
		for (auto &machine : machines) {
			machine->m_inputs[0] = uint8_t(~(start ? 0x10 : 0));
			assert(machine->set_analog_input(0, 0x80));
			assert(machine->set_analog_input(1, 0xff));
			assert(machine->set_analog_input(2, 0x30));
		}
	};
	int frame = 0;
	while (machines[0]->work_ram()[0x1138] != 0x14 && frame++ < 7000) {
		inputs();
		pair.run_frame();
		for (auto &machine : machines) machine->m_audio_out.clear();
	}
	assert(machines[0]->work_ram()[0x1138] == 0x14);
	model1::simulation_state checkpoint;
	assert(machines[0]->save_simulation_state(checkpoint, true));
	auto live_game = std::dynamic_pointer_cast<game::vr::gameplay_state>(machines[0]->game_state());
	assert(live_game);
	const auto gameplay = *live_game;
	race_replay::race_recorder recorder(*machines[0]);
	assert(recorder.start().empty());
	for (int i = 0; i < 180; i++) {
		if (i == 90) assert(pair.retire(*machines[1], game::vr::retire_local_snapshot));
		inputs();
		assert(recorder.begin_frame().empty());
		pair.run_frame();
		assert(recorder.end_frame().empty());
		for (auto &machine : machines) machine->m_audio_out.clear();
	}
	recorder.stop();
	const auto live_hash = input_replay::ram_hash(*machines[0]);
	const auto peer_hash = input_replay::ram_hash(*machines[1]);
	const auto live_ticks = machines[0]->link()->ticks();
	const auto peer_ticks = machines[1]->link()->ticks();
	model1 replay(load_regions(dir, game::vr::ID));
	replay.set_drawing(false);
	replay.enable_link({}, true);
	rec::context replay_runtime;
	if (native) {
		replay_runtime.attach(replay.cpu(), "vr");
		assert(rec::build_native(replay.maincpu_rom(), map));
		replay.run_cpu = [&](int cycles) { replay_runtime.run(cycles); };
	}
	assert(replay.load_simulation_state(checkpoint, true));
	auto replay_game = std::dynamic_pointer_cast<game::vr::gameplay_state>(replay.game_state());
	assert(replay_game);
	*replay_game = gameplay;
	if (native) replay_runtime.reset_run_state();
	for (const auto &recorded : recorder.data().frames) {
		const auto error = race_replay::replay_race_frame(replay, recorded);
		if (!error.empty()) fprintf(stderr, "VR linked replay: %s\n", error.c_str());
		assert(error.empty());
	}
	assert(input_replay::ram_hash(*machines[0]) == live_hash && machines[0]->link()->ticks() == live_ticks);
	assert(input_replay::ram_hash(*machines[1]) == peer_hash && machines[1]->link()->ticks() == peer_ticks);
	printf("VR replay: 180 linked race frames with peer retirement passed (%s)\n", native ? "native" : "interpreter");
}

static void race_restart_with_roms(const char *dir, bool native)
{
	model1 machine(load_regions(dir, game::vr::ID));
	rec::context runtime;
	machine.set_drawing(false);
	auto host = make_game_host();
	bool pc_mode = true;
	host_context context{.machine = machine, .pc_mode = pc_mode};
	context.race_replay_enabled = true;
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
	bool saw_transition = false, completed = false, retained = false, next_race = false;
	for (int frame = 1; frame <= 20000; frame++) {
		const auto input = play->frame(frame, machine);
		for (size_t port = 0; port < input.pressed.size(); port++)
			machine.m_inputs[port] = uint8_t(~input.pressed[port]);
		for (size_t channel = 0; channel < input.analog.size(); channel++)
			if (input.analog[channel] >= 0)
				assert(machine.set_analog_input(int(channel), uint8_t(input.analog[channel])));
		host->before_frame();
		host->begin_race_record_frame();
		machine.run_frame();
		host->after_frame();
		const int phase = machine.work_ram()[0x1138];
		if (phase == 0x13) saw_transition = true;
		if (restarts < 2 && phase == 0x14 && ++racing == 60) {
			assert(saw_transition);
			const auto recording = host->race_recording(false);
			assert(recording && !recording->frames.empty());
			if (!restarts) {
				model1 shadow(load_regions(dir, game::vr::ID));
				shadow.set_drawing(false);
				rec::context shadow_runtime;
				if (native) {
					rec::hints map;
					assert(map.read("assets/games/vr/vr_map.txt"));
					shadow_runtime.attach(shadow.cpu(), "vr");
					assert(rec::build_native(shadow.maincpu_rom(), map));
					shadow.run_cpu = [&](int cycles) { shadow_runtime.run(cycles); };
				}
				assert(host->restore_race_recording(shadow));
				if (native) shadow_runtime.reset_run_state();
				for (const auto &recorded : recording->frames) {
					const auto error = race_replay::replay_race_frame(shadow, recorded);
					if (!error.empty()) fprintf(stderr, "VR host replay: %s\n", error.c_str());
					assert(error.empty());
				}
			}
			assert(host->quick_restart_race());
			assert(!host->race_recording(false));
			assert(machine.work_ram()[0x1138] == 0x12);
			if (++restarts == 2) {
				printf("VR real-ROM countdown and repeated race restart passed (%s)\n",
					native ? "native" : "interpreter");
			}
			racing = 0;
			saw_transition = false;
		}
		if (restarts == 2 && !completed && phase == 0x1c) {
			const auto recording = host->race_recording(true);
			assert(recording && !recording->frames.empty());
			assert(!host->race_recording(false));
			model1 shadow(load_regions(dir, game::vr::ID));
			shadow.set_drawing(false);
			rec::context shadow_runtime;
			if (native) {
				rec::hints map;
				assert(map.read("assets/games/vr/vr_map.txt"));
				shadow_runtime.attach(shadow.cpu(), "vr");
				assert(rec::build_native(shadow.maincpu_rom(), map));
				shadow.run_cpu = [&](int cycles) { shadow_runtime.run(cycles); };
			}
			assert(host->restore_race_recording(shadow));
			if (native) shadow_runtime.reset_run_state();
			for (const auto &recorded : recording->frames) {
				const auto error = race_replay::replay_race_frame(shadow, recorded);
				if (!error.empty()) fprintf(stderr, "VR completed replay: %s\n", error.c_str());
				assert(error.empty());
			}
			completed = true;
		} else if (completed && phase == 0x10) {
			assert(host->race_recording(true));
			retained = true;
		} else if (completed && phase == 0x12) {
			assert(retained);
			assert(!host->race_recording(true));
			if (next_race) {
				assert(host->race_recording(false));
				printf("VR replay: completed race retained until the next race (%s)\n",
					native ? "native" : "interpreter");
				return;
			}
			next_race = true;
		}
	}
	assert(false && "script did not complete two races");
}

int main(int argc, char **argv)
{
	if (argc == 2 || (argc == 3 && std::string(argv[2]) == "--native")) {
		race_restart_with_roms(argv[1], argc == 3 && std::string(argv[2]) == "--native");
		race_replay_with_roms(argv[1], argc == 3 && std::string(argv[2]) == "--native");
		linked_replay_with_roms(argv[1], argc == 3 && std::string(argv[2]) == "--native");
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
	{
		race_replay::race_recorder limited(machine, sizeof(race_replay::replay_frame) +
			sizeof(race_replay::replay_sound_event));
		assert(limited.start().empty());
		assert(limited.begin_frame().empty());
		machine.replay_sound_observer(machine.frame_number(), 0, 0x11);
		assert(limited.error().empty());
		machine.replay_sound_observer(machine.frame_number(), 1, 0x22);
		assert(limited.error() == "replay recording memory limit reached");
		limited.stop();
		assert(!machine.replay_sound_observer && !machine.replay_link_observer);
		assert(limited.start().empty());
		assert(limited.begin_frame().empty());
		assert(limited.error().empty());
		limited.stop();
	}
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
	machine.enable_link({}, true);
	auto *board = machine.link();
	board->write(1, 1);
	board->write(4, 1);
	board->write(0x10, 0x42);
	board->set_cn(1);
	board->set_fg(1);
	board->tick(true);
	model1::simulation_state linked;
	assert(!machine.save_simulation_state(linked));
	assert(machine.save_simulation_state(linked, true));
	const auto original = board->save_state();
	board->reset();
	machine.work_ram()[0x200] = 0x11;
	assert(!machine.load_simulation_state(linked));
	assert(machine.load_simulation_state(linked, true));
	assert(board->snapshot() == original.shared && board->cn() == 0xff &&
		board->fg() == 0x7f && board->ticks() == 1);
	assert(machine.work_ram()[0x200] == 0x5a);
	model1::regions replay_regions;
	replay_regions.title = game::vr::ID;
	model1 replay_machine(std::move(replay_regions));
	replay_machine.enable_link({}, true);
	assert(replay_machine.load_simulation_state(linked, true));
	replay_machine.link()->write(0x10, 0x99);
	replay_machine.link()->tick(true);
	assert(replay_machine.link()->read(0x1d4) == 0x99);
	assert(board->read(0x10) == 0x42 && board->ticks() == 1);
	board->tick(true);
	assert(board->ticks() == 2 && board->read(0x1d4) == 0x42);
	machine.enable_link({}, false);
	machine.work_ram()[0x200] = 0x21;
	assert(!machine.load_simulation_state(linked, true));
	assert(machine.work_ram()[0x200] == 0x21);
	machine.disable_link();
	assert(!machine.load_simulation_state(linked, true));
	context.link_player = 0;
	context.race_replay_enabled = true;
	machine.replay_sound_observer = [](uint64_t, int, uint8_t) {};
	machine.work_ram()[0x1138] = 0x12;
	host->begin_race_record_frame();
	assert(host->race_recording_error() == "replay recording hooks are already in use");
	assert(machine.replay_sound_observer && !host->race_recording(false));
	assert(!host->restore_race_recording(replay_machine));
	machine.replay_sound_observer = {};
	machine.work_ram()[0x1138] = 0x10;
	host->begin_race_record_frame();
	machine.work_ram()[0x1138] = 0x12;
	host->begin_race_record_frame();
	assert(host->race_recording_error().empty());
	assert(host->race_recording(false));
	host.reset();
	puts("VR local multiplayer host capabilities and diagnostics passed");
}
