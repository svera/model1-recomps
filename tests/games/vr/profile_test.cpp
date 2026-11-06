#include "../../../src/games/game.h"
#include "../../../src/games/vr/controls.h"
#include "../../../src/games/script.h"
#include "../../../src/host/regions.h"
#include "../../../src/machine/model1.h"
#include <string>
#include <cassert>

using screen = game::vr::menu_steering::screen;

static void steering_curve()
{
	using wheel_t = game::vr::menu_steering;
	wheel_t linear, curved;
	auto settled = [](uint8_t analog, float speed) {
		wheel_t wheel;
		uint8_t value = 128;
		for (int frame = 0; frame < 120; frame++)
			value = wheel.update(screen::none, false, false, analog, true, speed);
		return value;
	};
	assert(wheel_t::calibrated(settled(0, 0)) == -127);
	assert(settled(128, 0) == 128);
	assert(wheel_t::calibrated(settled(255, 0)) == 127);
	assert(wheel_t::calibrated(settled(64, 0)) == -32);
	assert(wheel_t::calibrated(settled(192, 0)) == 32);
	assert(wheel_t::calibrated(settled(166, 0)) == 11);   // 30% stick gives about 9% steering
	for (float speed : { 0.0f, 160.0f, 320.0f, 500.0f }) {
		int previous = 0;
		for (int analog = 0; analog <= 255; analog++) {
			const auto input = uint8_t(analog);
			const int output = settled(input, speed);
			assert(output >= previous);
			assert(std::abs(wheel_t::calibrated(output)) <= std::abs(wheel_t::calibrated(analog)));
			assert(linear.update(screen::none, false, false, input) == input);
			assert(linear.update(screen::none, true, false, input) == 0);
			assert(linear.update(screen::none, false, true, input) == 255);
			previous = output;
		}
	}
	for (int delta = 0; delta <= 127; delta++)
		assert(std::abs((128 - settled(uint8_t(128 - delta), 0)) -
			(settled(uint8_t(128 + delta), 0) - 128)) <= 1);
	assert(wheel_t::calibrated(settled(255, 80)) == 110);
	assert(wheel_t::calibrated(settled(255, 160)) == 91);
	assert(wheel_t::calibrated(settled(255, 320)) == 57);
	assert(wheel_t::calibrated(settled(0, 320)) == -57);
	assert(settled(255, 500) == settled(255, 320));
	int previous_lock = 255;
	for (int speed = 0; speed <= 400; speed++) {
		const int lock = settled(255, float(speed));
		assert(lock <= previous_lock);
		previous_lock = lock;
	}
	// Sudden full lock, reversal and release obey the speed-dependent rate, without overshoot.
	for (float speed : { 0.0f, 320.0f }) {
		wheel_t wheel;
		int previous = 0;
		const int max_step = int(std::ceil((speed == 0 ? 6.0f : 1.8f) * wheel_t::FRAME_SECONDS * 128));
		for (uint8_t target : { 255, 0, 128 }) {
			const int end = wheel_t::calibrated(settled(target, speed));
			for (int frame = 0; frame < 120; frame++) {
				const int value = wheel_t::calibrated(wheel.update(screen::none, false, false, target, true, speed));
				assert(std::abs(value - previous) <= max_step + 2);   // calibration and byte quantization
				assert(value >= std::min(previous, end) && value <= std::max(previous, end));
				previous = value;
			}
			assert(previous == end);
		}
	}
	wheel_t slow, fast;
	assert(slow.update(screen::none, false, false, 255, true, 0) == 141);
	assert(fast.update(screen::none, false, false, 255, true, 320) == 135);
	assert(fast.update(screen::none, false, false, 255, true, 320, 0) == 135);
	// Identical elapsed simulation time gives the same result at different sampling rates.
	for (int hz : { 30, 60, 120 }) {
		wheel_t wheel;
		uint8_t value = 128;
		for (int frame = 0; frame < hz / 5; frame++)
			value = wheel.update(screen::none, false, false, 255, true, 320, 1.0f / hz);
		assert(value == 160);
	}
	// Speed changes enforce the new range immediately, while players keep independent histories.
	for (int frame = 0; frame < 60; frame++) slow.update(screen::none, false, false, 255, true);
	assert(wheel_t::calibrated(slow.update(screen::none, false, false, 255, true, 320)) == 57);
	assert(fast.update(screen::none, false, false, 128, true, 320) == 128);
	assert(slow.update(screen::none, false, false, 0, false) == 0);
	assert(slow.update(screen::none, false, false, 128, true) == 128);
	// Both menu screens must see the uncurved axis, including their repeat thresholds.
	for (auto s : { screen::course, screen::initials })
		for (int analog = 0; analog <= 255; analog++)
			for (int frame = 0; frame < 20; frame++)
				assert(linear.update(s, false, false, uint8_t(analog)) ==
					curved.update(s, false, false, uint8_t(analog), true, 320));
	assert(curved.update(screen::none, false, false, 128, true, 320) == 128);

	const auto &profile = game::vr::CONTROLS;
	auto p1 = player_controls::defaults(0, profile);
	auto p2 = player_controls::defaults(1, profile);
	assert(profile.steering_curve && !control_profile{}.steering_curve);
	assert(p1.standard_steering && p2.standard_steering);
	p1.load({}, 0, profile);
	assert(p1.standard_steering);
	assert(!player_controls{}.standard_steering);
	const std::map<std::string, std::string> settings{
		{ "p1_steering_curve", "exponential" }, { "p2_steering_curve", "linear" }
	};
	p1.load(settings, 0, profile);
	p2.load(settings, 1, profile);
	assert(p1.standard_steering && !p2.standard_steering);
	p1.standard_steering = false;
	p1.load({ { "p1_steering_curve", "standard" } }, 0, profile);
	assert(p1.standard_steering);
	p1.load({ { "p1_steering_curve", "invalid" } }, 0, profile);
	assert(p1.standard_steering);
	FILE *saved = tmpfile();
	assert(saved);
	p1.save(saved, 0, profile);
	p2.save(saved, 1, profile);
	rewind(saved);
	std::map<std::string, std::string> restored;
	char line[256];
	while (fgets(line, sizeof(line), saved)) {
		std::string text(line);
		const auto separator = text.find(" = ");
		assert(separator != std::string::npos && text.back() == '\n');
		restored.emplace(text.substr(0, separator), text.substr(separator + 3, text.size() - separator - 4));
	}
	assert(!ferror(saved));
	fclose(saved);
	assert(restored.at("p1_steering_curve") == "standard");
	assert(restored.at("p2_steering_curve") == "arcade");
	p1 = player_controls::defaults(0, profile);
	p2.standard_steering = true;
	p1.load(restored, 0, profile);
	p2.load(restored, 1, profile);
	assert(p1.standard_steering && !p2.standard_steering);
	p1.load({ { "p1_steering_curve", "arcade" } }, 0, profile);
	assert(!p1.standard_steering);
	assert(player_controls::defaults(0, profile).standard_steering);
	printf("VR Standard steering: nonlinear response, speed scaling, slew limits, timing, menus and settings migration passed\n");
}

static void digital_steering()
{
	using wheel_t = game::vr::menu_steering;
	for (float speed : { 0.0f, 160.0f, 320.0f }) {
		wheel_t digital, analog;
		// Hold, reverse, release and opposing buttons match the same analog rate limiter.
		for (int direction : { 1, -1, 0, 1, 2 }) {
			const bool left = direction == -1 || direction == 2;
			const bool right = direction == 1 || direction == 2;
			const uint8_t target = direction == 1 ? 255 : direction == -1 ? 0 : 128;
			int previous = wheel_t::calibrated(analog.update(screen::none, false, false, 128, true, speed, 0));
			for (int frame = 0; frame < 60; frame++) {
				const auto output = digital.update(screen::none, left, right, 128, true, speed);
				assert(output == analog.update(screen::none, false, false, target, true, speed));
				const int value = wheel_t::calibrated(output);
				if (frame == 0 && direction == 1) assert(value > 0 && value < 127);
				if (frame == 0 && direction == 0) assert(value < 0);   // release is not instant
				assert(std::abs(value - previous) <= 16);
				previous = value;
			}
			if (direction == 0 || direction == 2) assert(previous == 0);
		}
	}
	wheel_t tapping;
	int previous_peak = 0;
	for (int pulse = 0; pulse < 4; pulse++) {
		int peak = 0;
		for (int frame = 0; frame < 3; frame++)
			peak = wheel_t::calibrated(tapping.update(screen::none, false, true, 128, true, 320));
		const int released = wheel_t::calibrated(tapping.update(screen::none, false, false, 128, true, 320));
		assert(peak > previous_peak && peak < 57);
		assert(released > 0 && released < peak);
		previous_peak = peak;
	}
	for (int frame = 0; frame < 60; frame++)
		assert(tapping.update(screen::none, false, false, 128, true, 320) >= 128);
	assert(tapping.update(screen::none, false, false, 128, true, 320) == 128);
	for (int hz : { 30, 60, 120 }) {
		wheel_t wheel;
		uint8_t value = 128;
		for (int frame = 0; frame < hz / 5; frame++)
			value = wheel.update(screen::none, false, true, 128, true, 320, 1.0f / hz);
		assert(value == 160);
	}
	wheel_t mixed, digital_only;
	for (int frame = 0; frame < 10; frame++)
		assert(mixed.update(screen::none, true, false, 255, true, 320) ==
			digital_only.update(screen::none, true, false, 128, true, 320));
	assert(mixed.update(screen::none, false, true, 128, false) == 255);
	assert(mixed.update(screen::none, false, false, 128, true, 320) == 128);
	for (auto s : { screen::course, screen::initials }) {
		wheel_t arcade, standard;
		for (int frame = 0; frame < 100; frame++) {
			const bool left = frame >= 50, right = frame < 40;
			assert(arcade.update(s, left, right, 128) ==
				standard.update(s, left, right, 128, true, 320));
		}
		assert(standard.update(screen::none, false, false, 128, true, 320) == 128);
	}
	printf("VR digital steering: ramp, release, reversal, tapping, timing and menu bypass passed\n");
}

// Taps on course selection: the game's own wheel reading is the host's formula, and each tap is one course.
static void pad_courses(const char *dir)
{
	model1 m(load_regions(dir, game::vr::ID));
	m.set_smooth(false);
	m.reset();
	game::vr::menu_steering wheel;
	auto play = game::make_auto_play(game::vr::ID, "intermediate", 12345, true);
	assert(play);
	int selecting_frames = 0, checks = 0;
	bool raced = false;
	for (int f = 1; f <= 4500; f++) {
		const auto input = play->frame(f);
		const bool selecting = m.work_ram()[0x1138] == 0x10 && m.camera() == 0x502150;
		const int s = selecting ? selecting_frames++ : -1;
		// right, right (Expert), then left (Medium), which the script confirms
		const bool right = s == 2 || s == 4, left = s == 14;
		const uint8_t value = wheel.update(selecting ? screen::course : screen::none, left, right, 0x80, true, 320);
		assert(m.set_analog_input(0, value));
		for (size_t c = 1; c < input.analog.size(); c++)
			if (input.analog[c] >= 0) assert(m.set_analog_input(c, uint8_t(input.analog[c])));
		for (int c = 0; c < 3; c++) m.m_inputs[c] = uint8_t(~input.pressed[c]);
		m.run_frame();
		if (s == 0 || s == 12 || s == 30) {
			assert(int32_t(m.read32(0x501060)) == game::vr::menu_steering::calibrated(value));
			assert(int(m.read32(0x501940) & 0xffff) == (s == 0 ? 0 : s == 12 ? 2 : 1));
			checks++;
		}
		const auto error = play->check(f, m);
		if (!error.empty()) fprintf(stderr, "%s\n", error.c_str());
		assert(error.empty());
		if (m.work_ram()[0x1138] == 0x14) raced = true;
	}
	assert(checks == 3 && raced);
	printf("VR pad: one tap per course on course selection, Medium confirmed into race\n");
}

// Initials typed with taps: qualifying's race, then SDK entered with the cursor (its own checks).
static void pad_initials(const char *dir)
{
	model1 m(load_regions(dir, game::vr::ID));
	m.set_smooth(false);
	m.reset();
	game::vr::menu_steering wheel;
	auto play = game::make_auto_play(game::vr::ID, "qualifying", 12345, true);
	assert(play);
	constexpr int target[3]{ 'S' - 'A', 'D' - 'A', 'K' - 'A' };
	int taps = 0;
	bool tapped = false;
	uint8_t last = 0x80;
	for (int f = 1; f <= play->min_frames(); f++) {
		auto input = play->frame(f, m);
		const bool initials = m.work_ram()[0x1138] == 0x1e && m.read32(0x5018fc) != 0;
		if (initials) {
			const int letter = game::vr::menu_steering::letter(last);
			const uint32_t confirmed = m.read32(0x501910);
			const int want = target[std::min(confirmed, 2u)];
			const bool tap = !tapped && letter != want;
			tapped = tap;
			taps += tap;
			input.analog[0] = last = wheel.update(screen::initials, tap && want < letter, tap && want > letter, 0x80, true, 320);
		}
		for (size_t c = 0; c < input.analog.size(); c++)
			if (input.analog[c] >= 0) assert(m.set_analog_input(c, uint8_t(input.analog[c])));
		for (int c = 0; c < 3; c++) m.m_inputs[c] = uint8_t(~input.pressed[c]);
		m.run_frame();
		const auto error = play->check(f, m);
		if (!error.empty()) fprintf(stderr, "%s\n", error.c_str());
		assert(error.empty());
	}
	const auto error = play->finish();
	if (!error.empty()) fprintf(stderr, "%s\n", error.c_str());
	assert(error.empty());
	assert(taps == 3 + 15 + 7);   // P (the stick's center) to S, D, K
	printf("VR pad: SDK entered one letter per tap and saved in the rankings\n");
}

int main(int argc, char **argv)
{
	steering_curve();
	digital_steering();
	using wheel_t = game::vr::menu_steering;
	game::vr::menu_steering wheel;
	// racing: the keys steer fully, the stick passes through
	assert(wheel.update(screen::none, true, false, 128) == 0);
	assert(wheel.update(screen::none, false, true, 128) == 255);
	for (int analog = 0; analog <= 255; analog++)
		assert(wheel.update(screen::none, false, false, uint8_t(analog)) == analog);
	// the ROM's mapping (FE6776, FC26C8, FC488A); SDK as the qualifying script enters it
	assert(wheel_t::calibrated(0x80) == 0 && wheel_t::calibrated(0x85) == 0 && wheel_t::calibrated(0x86) == 1);
	assert(wheel_t::calibrated(0) == -127 && wheel_t::calibrated(255) == 127);
	assert(wheel_t::letter(148) == 'S' - 'A' && wheel_t::letter(68) == 'D' - 'A' && wheel_t::letter(100) == 'K' - 'A');
	assert(wheel_t::course(0x80) == 0 && wheel_t::course(0xb0) == 1 && wheel_t::course(0xe0) == 2);
	for (auto s : { screen::course, screen::initials })
		for (int i = 0; i < wheel_t::choices(s); i++) {
			const uint8_t w = wheel_t::wheel_for(s, i);
			assert(wheel_t::choice(s, w) == i);
			// the middle of the choice (the center for the centered stick's): a step either way keeps it
			assert(w == 0x80 || wheel_t::choice(s, 0x80) != i);
			assert(wheel_t::choice(s, w - 1) == i && wheel_t::choice(s, w + 1) == i);
		}

	// courses: from Beginner (the centered stick's), one tap a course, no further than the last
	auto course = [&](bool left, bool right, uint8_t analog = 128) {
		return wheel_t::course(wheel.update(screen::course, left, right, analog));
	};
	assert(course(false, false) == 0);
	assert(course(false, true) == 1);
	assert(course(false, true) == 1);   // held: no repeat yet
	assert(course(false, false) == 1);  // released: kept
	assert(course(false, true) == 2);
	assert(course(false, false) == 2);
	assert(course(false, true) == 2);   // the last
	assert(course(false, false) == 2);
	assert(course(true, false) == 1);   // straight back: no waiting out a wheel position
	assert(course(false, false) == 1);
	assert(course(true, true) == 1);    // both: nothing
	// the stick: directions past a quarter, drift ignored
	for (int analog = 0x60; analog <= 0xa0; analog++) assert(course(false, false, uint8_t(analog)) == 1);
	assert(course(false, false, 0xa1) == 2);
	assert(course(false, false, 0x80) == 2);
	assert(course(false, false, 0x5f) == 1);
	assert(course(false, true, 0x00) == 2);   // a new direction is a new tap; the D-pad wins
	assert(course(false, false) == 2);

	// initials: a new screen starts at the centered stick's letter (P); holding repeats
	auto letter = [&](bool left, bool right) { return wheel_t::letter(wheel.update(screen::initials, left, right, 128)); };
	assert(wheel.update(screen::none, false, false, 128) == 128);
	assert(letter(false, false) == 'P' - 'A');
	assert(letter(false, true) == 'Q' - 'A');
	for (int f = 1; f < wheel_t::REPEAT_DELAY; f++) assert(letter(false, true) == 'Q' - 'A');
	assert(letter(false, true) == 'R' - 'A');
	for (int f = 1; f < wheel_t::REPEAT_EVERY; f++) assert(letter(false, true) == 'R' - 'A');
	assert(letter(false, true) == 'S' - 'A');
	assert(letter(false, false) == 'S' - 'A');
	for (int i = 0; i < 40; i++) letter(i % 2 == 0, false);   // tapped past the start
	assert(letter(false, false) == 0);
	for (int i = 0; i < 80; i++) letter(false, i % 2 == 0);   // and past the end
	assert(letter(false, false) == 28);
	// back on a course, its own choice again
	assert(course(false, false) == 0);
	if (argc > 1) {
		pad_courses(argv[1]);
		pad_initials(argv[1]);
	}

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
	assert(!game::vr::CONTROLS.show_player_2);
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
