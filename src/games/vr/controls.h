// Virtua Racing's controls for the desktop host (src/host/controls.h): the wheel and pedals are
// the I/O board's analog channels (keys, or a pad's axes), the gear shift two buttons.
#pragma once
#include "../../host/controls.h"
#include "profile.h"

namespace game::vr {

enum : uint8_t { SHIFT_DOWN = 0x10, SHIFT_UP = 0x20 };   // player 1's input byte (IO_PROFILE)

// its pedals in the shared button slots (src/host/controls.h): keys for the analog channels
enum : int { C_ACCELERATOR = C_BUTTON_1, C_BRAKE, C_CAMERA };
inline constexpr uint8_t CAMERA_CYCLE_INPUT = 0x40;   // Host-only action, consumed before I/O.

class menu_steering
{
public:
	uint8_t update(bool selecting, bool left, bool right, uint8_t analog)
	{
		if (!selecting) {
			active = false;
			return left != right ? (left ? 0 : 255) : analog;
		}
		if (left != right) {
			if (!active) position = analog;
			active = true;
			position = std::clamp(position + (right ? 1 : -1), 0, 255);
		} else if (analog != 0x80) {
			active = false;
		}
		return active ? uint8_t(position) : analog;
	}

private:
	bool active = false;
	int position = 0x80;
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
	c.analog_help = "For a pad, select and move a stick axis or squeeze a trigger. The keyboard also has separate steering, accelerator and brake keys. On course selection and initials entry, hold Left/Right to move gradually, release to keep the selection, and press Accelerator to confirm.";
	return c;
}();

}
