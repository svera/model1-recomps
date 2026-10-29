// Star Wars Arcade's controls for the desktop host (src/host/controls.h). The ROM's input test
// names the seats Pilot (player 1) and Gunner (player 2): each has a flight stick with Laser and
// Torpedo; the Pilot also has the throttle and the VR (view) button. Both seats' buttons share the
// I/O board's player 1 byte, and there is one Start button (the Pilot's).
#pragma once
#include "../../host/controls.h"
#include "profile.h"

namespace game::swa {

enum : int { C_LASER = C_BUTTON_1, C_TORPEDO, C_VIEW, C_THROTTLE_UP, C_THROTTLE_DOWN };

inline const control_profile CONTROLS = [] {
	control_profile c;
	c.title = ID;
	c.add(C_UP, "Stick forward (dive)");
	c.add(C_DOWN, "Stick back (climb)");
	c.add(C_LEFT, "Stick left");
	c.add(C_RIGHT, "Stick right");
	c.add(C_LASER, "Laser", "laser");
	c.add(C_TORPEDO, "Torpedo", "torpedo");
	c.add(C_VIEW, "View (VR button)", "view");
	c.add(C_THROTTLE_UP, "Throttle faster", "throttle_up");
	c.add(C_THROTTLE_DOWN, "Throttle slower", "throttle_down");
	for (int a : { C_START, C_COIN, C_MENU }) c.add(a);
	c.button_ports = { 1, 1 };
	c.bits[C_LASER] = PILOT_LASER;
	c.bits[C_TORPEDO] = PILOT_TORPEDO;
	c.bits[C_VIEW] = PILOT_VIEW;
	std::array<uint8_t, C_COUNT> gunner{};
	gunner[C_LASER] = GUNNER_LASER;
	gunner[C_TORPEDO] = GUNNER_TORPEDO;
	c.p2_bits = gunner;
	for (int a : { C_VIEW, C_THROTTLE_UP, C_THROTTLE_DOWN }) c.p1_only[a] = true;
	c.start_bits = { START, START };
	auto &k1 = c.keys[0], &k2 = c.keys[1];
	k1[C_UP] = SDL_SCANCODE_UP; k1[C_DOWN] = SDL_SCANCODE_DOWN; k1[C_LEFT] = SDL_SCANCODE_LEFT; k1[C_RIGHT] = SDL_SCANCODE_RIGHT;
	k1[C_LASER] = SDL_SCANCODE_Z; k1[C_TORPEDO] = SDL_SCANCODE_X; k1[C_VIEW] = SDL_SCANCODE_C;
	k1[C_THROTTLE_UP] = SDL_SCANCODE_W; k1[C_THROTTLE_DOWN] = SDL_SCANCODE_S; k1[C_START] = SDL_SCANCODE_1;
	k2[C_UP] = SDL_SCANCODE_I; k2[C_DOWN] = SDL_SCANCODE_K; k2[C_LEFT] = SDL_SCANCODE_J; k2[C_RIGHT] = SDL_SCANCODE_L;
	k2[C_LASER] = SDL_SCANCODE_U; k2[C_TORPEDO] = SDL_SCANCODE_O; k2[C_START] = SDL_SCANCODE_2;
	c.pad[C_LASER] = PAD_RT;
	c.pad[C_TORPEDO] = PAD_LT;
	c.pad[C_VIEW] = SDL_GAMEPAD_BUTTON_NORTH;
	c.pad[C_START] = SDL_GAMEPAD_BUTTON_START;
	c.pad[C_COIN] = SDL_GAMEPAD_BUTTON_LEFT_SHOULDER;
	c.pad[C_MENU] = SDL_GAMEPAD_BUTTON_BACK;
	c.pad_actions = { C_LASER, C_TORPEDO, C_VIEW, C_START, C_COIN, C_MENU };   // (stick and throttle: Analog axes)
	// the keys give a channel's full travel: forward/left and faster are its low end
	c.analog_keys = {
		{ 0, C_LEFT, C_RIGHT, true }, { 1, C_UP, C_DOWN, true }, { 2, C_THROTTLE_UP, C_THROTTLE_DOWN, false },
		{ 4, C_LEFT, C_RIGHT, true }, { 5, C_UP, C_DOWN, true } };
	c.pad_buttons_help = "Change the laser, torpedo, view, start, coin and menu buttons. The Gunner has no view button.";
	c.analog_help = "Select, then move a stick axis. The flight stick is the left stick (pushing forward dives, as the "
					"cabinet's), the Pilot's throttle the right stick's Y (up: faster). Values are scaled to the "
					"cabinet's factory calibration; Invert flight Y (Game) swaps forward and back.";
	return c;
}();

}
