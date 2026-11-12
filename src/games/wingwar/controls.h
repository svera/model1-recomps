// Wing War's flight controls: the stick axes and throttle are analog. The cabinet's four view
// buttons (across the system and player input bytes) are one Change view button, as in the other
// games: it presses the next view's button (view_cycle, gameplay.h).
#pragma once
#include "../../host/controls.h"
#include "gameplay.h"
#include "profile.h"

namespace game::wingwar {

enum : int { C_MACHINE_GUN = C_BUTTON_1, C_MISSILE, C_SMOKE, C_VIEW,
	C_THROTTLE_UP = C_BUTTON_8, C_THROTTLE_DOWN = C_BUTTON_9 };

inline const control_profile CONTROLS = [] {
	control_profile c;
	c.title = ID;
	c.show_player_2 = false;
	c.add(C_UP, "Pitch up");
	c.add(C_DOWN, "Pitch down");
	c.add(C_LEFT, "Yaw left");
	c.add(C_RIGHT, "Yaw right");
	c.add(C_MACHINE_GUN, "Machine gun", "machine_gun");
	c.add(C_MISSILE, "Missile");
	c.add(C_SMOKE, "Smoke");
	c.add(C_VIEW, "Change view", "view");
	c.add(C_THROTTLE_UP, "Throttle up", "throttle_up");
	c.add(C_THROTTLE_DOWN, "Throttle down", "throttle_down");
	c.add(C_START);
	c.add(C_COIN);
	c.add(C_MENU);

	c.bits[C_MACHINE_GUN] = 0x10;
	c.bits[C_MISSILE] = 0x20;
	c.bits[C_SMOKE] = 0x40;
	c.bits[C_VIEW] = VIEW_CYCLE_INPUT;
	c.start_bits = { 0x10, 0x10 };

	auto &k = c.keys[0];
	k[C_UP] = SDL_SCANCODE_UP;
	k[C_DOWN] = SDL_SCANCODE_DOWN;
	k[C_LEFT] = SDL_SCANCODE_LEFT;
	k[C_RIGHT] = SDL_SCANCODE_RIGHT;
	k[C_MACHINE_GUN] = SDL_SCANCODE_Z;
	k[C_MISSILE] = SDL_SCANCODE_X;
	k[C_SMOKE] = SDL_SCANCODE_V;
	k[C_VIEW] = SDL_SCANCODE_C;
	k[C_THROTTLE_UP] = SDL_SCANCODE_W;
	k[C_THROTTLE_DOWN] = SDL_SCANCODE_S;
	k[C_START] = SDL_SCANCODE_1;
	k[C_COIN] = SDL_SCANCODE_5;

	c.pad[C_MACHINE_GUN] = SDL_GAMEPAD_BUTTON_SOUTH;
	c.pad[C_MISSILE] = SDL_GAMEPAD_BUTTON_EAST;
	c.pad[C_SMOKE] = SDL_GAMEPAD_BUTTON_WEST;
	c.pad[C_VIEW] = SDL_GAMEPAD_BUTTON_NORTH;
	c.pad[C_START] = SDL_GAMEPAD_BUTTON_START;
	c.pad[C_COIN] = SDL_GAMEPAD_BUTTON_DPAD_UP;
	c.pad[C_MENU] = SDL_GAMEPAD_BUTTON_BACK;
	c.pad_actions = { C_MACHINE_GUN, C_MISSILE, C_SMOKE, C_VIEW, C_START, C_COIN, C_MENU };
	// Smoke was on C before Change view took it
	c.key_names[C_SMOKE] = "smoke";
	c.renamed_keys = { { C_SMOKE, "button_3", SDL_SCANCODE_C, SDL_SCANCODE_V } };
	c.analog_keys = { { 0, C_LEFT, C_RIGHT, true }, { 1, C_UP, C_DOWN, true },
		{ 2, C_THROTTLE_DOWN, C_THROTTLE_UP, false } };
	c.pad_buttons_help = "Change the weapon, view, start, coin and menu buttons.";
	c.analog_help = "The left stick controls flight: horizontal for yaw and vertical for pitch. The right trigger controls throttle. "
		"Keyboard arrows steer the stick, W/S change throttle, and the d-pad also steers.";
	return c;
}();

}
