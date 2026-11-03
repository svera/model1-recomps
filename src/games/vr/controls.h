// Virtua Racing's controls for the desktop host (src/host/controls.h): the wheel and pedals are
// the I/O board's analog channels (keys, or a pad's axes), the gear shift two buttons.
#pragma once
#include "../../host/controls.h"
#include "profile.h"
#include <cstdlib>

namespace game::vr {

enum : uint8_t { SHIFT_DOWN = 0x10, SHIFT_UP = 0x20 };   // player 1's input byte (IO_PROFILE)

// its pedals in the shared button slots (src/host/controls.h): keys for the analog channels
enum : int { C_ACCELERATOR = C_BUTTON_1, C_BRAKE, C_CAMERA };
inline constexpr uint8_t CAMERA_CYCLE_INPUT = 0x40;   // Host-only action, consumed before I/O.

// Course selection and initials entry read the wheel as a position. With a pad, Left/Right (D-pad,
// stick or keys) instead step a cursor one course or letter at a time, repeating while held, and the
// wheel is set to the middle of that choice.
class menu_steering
{
public:
	enum class screen { none, course, initials };

	// The game's wheel reading for an input value (FE6776: a dead zone of 5 around 0x80, full at 0x50)
	static int calibrated(int wheel)
	{
		const int d = wheel - 0x80;
		const int magnitude = std::clamp(std::abs(d) - 5, 0, 0x4b) * 0xff / 0x96;
		return d < 0 ? -magnitude : magnitude;
	}
	// FC26C8: Beginner up to 0x1c, Medium up to 0x58, Expert beyond
	static int course(int wheel)
	{
		const int c = calibrated(wheel);
		return c <= 0x1c ? 0 : c <= 0x58 ? 1 : 2;
	}
	// FC488A: 29 choices (A-Z, then symbols and end), 8 wheel units each
	static int letter(int wheel) { return std::clamp(((calibrated(wheel) + 0x80) >> 3) - 1, 0, 28); }
	static int choices(screen s) { return s == screen::course ? 3 : s == screen::initials ? 29 : 1; }
	static int choice(screen s, int wheel) { return s == screen::course ? course(wheel) : s == screen::initials ? letter(wheel) : 0; }
	// the input value at the middle of a choice (the center for the centered stick's)
	static uint8_t wheel_for(screen s, int index)
	{
		if (choice(s, 0x80) == index) return 0x80;
		int first = -1, last = -1;
		for (int w = 0; w < 256; w++)
			if (choice(s, w) == index) {
				if (first < 0) first = w;
				last = w;
			}
		if (first < 0) return 0x80;
		// the outer choices run to the end of the range: keep to their part near the others
		if (first == 0) first = std::max(first, last - 8);
		if (last == 255) last = std::min(last, first + 8);
		return uint8_t((first + last) / 2);
	}

	static constexpr int REPEAT_DELAY = 14, REPEAT_EVERY = 5;   // frames

	static uint8_t exponential(uint8_t analog)
	{
		const int delta = int(analog) - 128;
		const int range = delta < 0 ? 128 : 127;
		const int magnitude = (delta * delta + range / 2) / range;
		return uint8_t(128 + (delta < 0 ? -magnitude : magnitude));
	}

	uint8_t update(screen s, bool left, bool right, uint8_t analog, bool exponential_steering = false)
	{
		if (s == screen::none) {
			current = screen::none;
			return left != right ? (left ? 0 : 255) : exponential_steering ? exponential(analog) : analog;
		}
		if (s != current) {
			current = s;
			index = choice(s, 0x80);
			held = 0;
		}
		// The stick is directions here, not a position; its drift near the center is ignored.
		if (!left && !right) {
			left = analog < 0x60;
			right = analog > 0xa0;
		}
		const int direction = left == right ? 0 : right ? 1 : -1;
		if (direction != held_direction) {
			held_direction = direction;
			held = 0;
		}
		if (direction) {
			if (held == 0 || (held >= REPEAT_DELAY && (held - REPEAT_DELAY) % REPEAT_EVERY == 0))
				index = std::clamp(index + direction, 0, choices(s) - 1);
			held++;
		}
		return wheel_for(s, index);
	}

private:
	screen current = screen::none;
	int index = 0, held = 0, held_direction = 0;
};

inline const control_profile CONTROLS = [] {
	control_profile c;
	c.title = ID;
	c.show_player_2 = false;
	c.add(C_UP, "Shift Up");
	c.add(C_DOWN, "Shift Down");
	c.add(C_LEFT, "Steer Left");
	c.add(C_RIGHT, "Steer Right");
	c.add(C_ACCELERATOR, "Accelerator", "accelerator");
	c.add(C_BRAKE, "Brake", "brake");
	c.add(C_CAMERA, "Cycle camera", "camera");
	c.bits[C_CAMERA] = CAMERA_CYCLE_INPUT;
	for (int a : { C_START, C_COIN, C_MENU }) c.add(a);
	c.key_names[C_UP] = "shift_up";
	c.key_names[C_DOWN] = "shift_down";
	c.bits[C_UP] = SHIFT_UP;
	c.bits[C_DOWN] = SHIFT_DOWN;
	c.start_bits = { 0x10, 0x10 };   // one Start button
	c.hold_start = true;            // hold for manual transmission on course selection
	c.steering_curve = true;
	auto &k1 = c.keys[0], &k2 = c.keys[1];
	k1[C_UP] = SDL_SCANCODE_X; k1[C_DOWN] = SDL_SCANCODE_Z; k1[C_LEFT] = SDL_SCANCODE_LEFT; k1[C_RIGHT] = SDL_SCANCODE_RIGHT;
	k1[C_ACCELERATOR] = SDL_SCANCODE_UP; k1[C_BRAKE] = SDL_SCANCODE_DOWN; k1[C_START] = SDL_SCANCODE_1;
	k1[C_CAMERA] = SDL_SCANCODE_C;
	k2[C_UP] = SDL_SCANCODE_I; k2[C_DOWN] = SDL_SCANCODE_K; k2[C_LEFT] = SDL_SCANCODE_J; k2[C_RIGHT] = SDL_SCANCODE_L;
	k2[C_ACCELERATOR] = SDL_SCANCODE_U; k2[C_BRAKE] = SDL_SCANCODE_O; k2[C_START] = SDL_SCANCODE_2;
	c.pad[C_UP] = SDL_GAMEPAD_BUTTON_SOUTH;
	c.pad[C_DOWN] = SDL_GAMEPAD_BUTTON_EAST;
	c.pad[C_COIN] = SDL_GAMEPAD_BUTTON_DPAD_UP;
	c.pad[C_START] = SDL_GAMEPAD_BUTTON_START;
	c.pad[C_MENU] = SDL_GAMEPAD_BUTTON_BACK;
	c.pad[C_CAMERA] = SDL_GAMEPAD_BUTTON_NORTH;
	c.pad_actions = { C_UP, C_DOWN, C_CAMERA, C_START, C_COIN, C_MENU };   // (steering and pedals: Analog axes)
	// the shift was on Up and Down before it had keys of its own
	c.renamed_keys = { { C_UP, "up", SDL_SCANCODE_UP, SDL_SCANCODE_X }, { C_DOWN, "down", SDL_SCANCODE_DOWN, SDL_SCANCODE_Z } };
	c.analog_keys = { { 0, C_LEFT, C_RIGHT, true }, { 1, -1, C_ACCELERATOR, false }, { 2, -1, C_BRAKE, false } };
	c.pad_buttons_help = "Change the shift, camera, start, coin and menu buttons.";
	c.analog_help = "For a pad, select and move a stick axis or squeeze a trigger. The keyboard also has separate steering, accelerator and brake keys. On course selection and initials entry, tap the stick, D-pad or keys Left/Right to move one course or letter (hold to repeat), and press Accelerator (default: right trigger or Up key) to confirm.";
	return c;
}();

}
