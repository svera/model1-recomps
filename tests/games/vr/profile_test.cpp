#include "../../../src/games/game.h"
#include "../../../src/games/vr/controls.h"
#include "../../../src/games/script.h"
#include "../../../src/host/regions.h"
#include "../../../src/machine/model1.h"
#include <string>
#include <cassert>

static void keyboard_medium(const char *dir)
{
	model1 m(load_regions(dir, game::vr::ID));
	m.set_smooth(false);
	m.reset();
	game::vr::menu_steering wheel;
	auto play = game::make_auto_play(game::vr::ID, "intermediate", 12345, true);
	assert(play);
	int held = 0, released = 0;
	bool raced = false;
	for (int f = 1; f <= 4500; f++) {
		const auto input = play->frame(f);
		const bool selecting = m.work_ram()[0x1138] == 0x10 && m.camera() == 0x502150;
		const bool right = selecting && held < 48;
		const uint8_t value = wheel.update(selecting, false, right, 0x80);
		if (right) held++;
		else if (selecting && held == 48) {
			assert(value == 0xb0);
			released++;
		}
		assert(m.set_analog_input(0, value));
		for (size_t c = 1; c < input.analog.size(); c++)
			if (input.analog[c] >= 0) assert(m.set_analog_input(c, uint8_t(input.analog[c])));
		for (int c = 0; c < 3; c++) m.m_inputs[c] = uint8_t(~input.pressed[c]);
		m.run_frame();
		const auto error = play->check(f, m);
		if (!error.empty()) fprintf(stderr, "%s\n", error.c_str());
		assert(error.empty());
		if (m.work_ram()[0x1138] == 0x14) raced = true;
	}
	assert(held == 48 && released > 10 && raced);
	printf("VR keyboard: Medium selected with Right, retained on release, and confirmed into race\n");
}

int main(int argc, char **argv)
{
	game::vr::menu_steering wheel;
	assert(wheel.update(false, true, false, 128) == 0);
	assert(wheel.update(false, false, true, 128) == 255);
	assert(wheel.update(false, false, false, 128) == 128);
	assert(wheel.update(true, false, true, 128) == 129);
	assert(wheel.update(true, false, false, 128) == 129);
	assert(wheel.update(true, true, true, 128) == 129);
	assert(wheel.update(true, true, false, 128) == 128);
	for (int value = 127; value >= 0; value--)
		assert(wheel.update(true, true, false, 128) == value);
	assert(wheel.update(true, true, false, 128) == 0);
	assert(wheel.update(true, false, false, 128) == 0);
	for (int value = 1; value <= 255; value++)
		assert(wheel.update(true, false, true, 128) == value);
	assert(wheel.update(true, false, true, 128) == 255);
	assert(wheel.update(true, false, false, 128) == 255);
	assert(wheel.update(true, false, false, 100) == 100);
	assert(wheel.update(true, false, false, 128) == 128);
	assert(wheel.update(true, true, false, 100) == 99);
	assert(wheel.update(false, false, false, 128) == 128);
	assert(wheel.update(true, false, false, 128) == 128);
	assert(wheel.update(true, true, false, 128) == 127);
	if (argc > 1) keyboard_medium(argv[1]);

	const auto &vr = game::get(game::vr::ID);
	assert(vr.title.name == "vr");
	assert(game::find("vr") == game::vr::ID);
	assert(vr.display_name == "Virtua Racing");
	assert(vr.romset == "vr");
	assert(vr.recompiler_map == "games/vr/vr_map.txt");
	assert(vr.mode_ram == 0x503002 && vr.test_menu_mode == 18);   // 13 in attract, 18 in the test menu

	const auto &vr_io = game::get_io_profile(game::vr::ID);
	assert(vr_io.digital_controls[12] == "Shift Down");
	assert(vr_io.digital_controls[13] == "Shift Up");
	assert(vr_io.analog_controls[0] == "Wheel");
	assert(vr_io.analog_controls[1] == "Accelerator");
	assert(vr_io.analog_controls[2] == "Brake");
	assert(vr_io.analog_environment[0] == "VR_WHEEL");
	assert(vr_io.analog_environment[1] == "VR_ACCEL");
	assert(vr_io.analog_environment[2] == "VR_BRAKE");
	assert(vr_io.analog_neutral[0] == 0x80);
	assert(vr_io.analog_neutral[1] == 0x30);
	assert(vr_io.analog_neutral[2] == 0x30);
	assert(vr_io.host_axis_defaults[0] == game::host_axis::left_x);
	assert(vr_io.host_axis_defaults[1] == game::host_axis::right_trigger);
	assert(vr_io.host_axis_defaults[2] == game::host_axis::left_trigger);
	assert(vr_io.analog_kinds[0] == game::analog_kind::centered);
	assert(vr_io.analog_kinds[1] == game::analog_kind::pedal);
	assert(vr_io.analog_kinds[2] == game::analog_kind::pedal);

	const auto controls = player_controls::defaults(0, game::vr::CONTROLS);
	assert(controls.key[C_LEFT] == SDL_SCANCODE_LEFT);
	assert(controls.key[C_RIGHT] == SDL_SCANCODE_RIGHT);
	assert(controls.key[game::vr::C_ACCELERATOR] == SDL_SCANCODE_UP);
	assert(controls.key[game::vr::C_BRAKE] == SDL_SCANCODE_DOWN);
	assert(controls.key[C_UP] == SDL_SCANCODE_X);
	assert(controls.key[C_DOWN] == SDL_SCANCODE_Z);
	assert(controls.pad[C_UP] == SDL_GAMEPAD_BUTTON_SOUTH);
	assert(controls.pad[C_DOWN] == SDL_GAMEPAD_BUTTON_EAST);
	assert(controls.pad[C_COIN] == SDL_GAMEPAD_BUTTON_DPAD_UP);
	assert(controls.key[game::vr::C_ACCELERATOR] != controls.key[C_RIGHT]);
	assert(controls.pad[game::vr::C_CAMERA] == SDL_GAMEPAD_BUTTON_NORTH);
	assert(controls.key[game::vr::C_CAMERA] == SDL_SCANCODE_C);
	assert(controls.analog_axis[1] == game::host_axis::right_trigger);
	assert(game::vr::CONTROLS.visible(game::vr::C_ACCELERATOR));
	assert(std::string(game::vr::CONTROLS.key_name(game::vr::C_BRAKE)) == "brake");
	assert(game::vr::CONTROLS.visible(game::vr::C_CAMERA));
	assert(std::string(game::vr::CONTROLS.name(game::vr::C_CAMERA)) == "camera");
	assert(std::string(game::vr::CONTROLS.name(C_UP)) == "up");   // (pad_up, as before the shift's keys)
	assert(std::string(game::vr::CONTROLS.label(C_UP)) == "Shift Up");
	assert(std::string(game::vr::CONTROLS.key_name(C_UP)) == "shift_up");
	assert(game::vr::CONTROLS.start_bits[0] == game::vr::CONTROLS.start_bits[1]);
	assert(game::vr::CONTROLS.hold_start);
	assert(!control_profile{}.hold_start);
	auto remapped = controls;
	remapped.load({ { "p1_key_camera", "V" }, { "p1_pad_camera", "a" } }, 0, game::vr::CONTROLS);
	assert(remapped.key[game::vr::C_CAMERA] == SDL_SCANCODE_V);
	assert(remapped.pad[game::vr::C_CAMERA] == SDL_GAMEPAD_BUTTON_SOUTH);
	FILE *saved = tmpfile();
	assert(saved);
	remapped.save(saved, 0, game::vr::CONTROLS);
	rewind(saved);
	std::string text;
	for (int ch; (ch = fgetc(saved)) != EOF;) text += char(ch);
	fclose(saved);
	assert(text.find("p1_key_camera = V\n") != std::string::npos);
	assert(text.find("p1_pad_camera = a\n") != std::string::npos);
	// every pad action the page shows is one the game has
	for (int a : game::vr::CONTROLS.pad_actions) assert(game::vr::CONTROLS.visible(a));
}
