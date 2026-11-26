#include "../tools/android/app/src/main/cpp/touch_controller.h"
#include "../src/games/vr/controls.h"

#include <SDL3/SDL.h>
#include <cassert>
#include <cstdio>

int main()
{
	assert(SDL_Init(SDL_INIT_GAMEPAD));
	android_touch_set_state(true, 0, {});
	SDL_JoystickID id = android_touch_update();
	assert(id);
	SDL_Gamepad *pad = SDL_OpenGamepad(id);
	assert(pad);
	SDL_UpdateJoysticks();
	assert(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) == 0);
	assert(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) == 0);

	android_touch_set_state(true, (1u << SDL_GAMEPAD_BUTTON_START) | (1u << SDL_GAMEPAD_BUTTON_WEST),
		{0.5f, -1.0f, 0, 0.25f, 1, 0.5f});
	assert(android_touch_update() == id);
	SDL_UpdateJoysticks();
	assert(SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_START));
	assert(SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_WEST));
	assert(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX) > 16000);
	assert(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY) == -32768);
	assert(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTY) > 8000);
	assert(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) == 32767);
	assert(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > 16000);

	android_touch_set_state(true, 0, {});
	android_touch_update();
	SDL_UpdateJoysticks();
	assert(SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_START));
	SDL_Delay(40);
	android_touch_update();
	SDL_UpdateJoysticks();
	assert(!SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_START));
	assert(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX) == 0);
	assert(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) == 0);

	for (float y : { -1.0f, 0.0f, 0.48f, 0.5f, 1.0f, 0.0f }) {
		android_touch_set_state(true, 0, {0.5f, y, 0, 0, 0, 0}, true);
		assert(android_touch_update() == id);
		SDL_UpdateJoysticks();
		assert(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX) > 16000);
		assert(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY) == SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTY));
		uint8_t inputs[3]{};
		game::vr::CONTROLS.right_stick_input(inputs, 0, SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTY));
		assert(bool(inputs[1] & game::vr::CAMERA_REAR_INPUT) == (y >= 0.5f));
	}
	android_touch_set_state(true, 0, {0, 0.75f, 0, -0.5f, 0, 0});
	android_touch_update();
	SDL_UpdateJoysticks();
	assert(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY) > 0);
	assert(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTY) < 0);

	android_touch_set_state(true, 1u << SDL_GAMEPAD_BUTTON_SOUTH, {0, 0, 0, 0, 0, 1});
	android_touch_set_state(true, 0, {});
	SDL_Delay(40);
	android_touch_update();
	SDL_UpdateJoysticks();
	assert(SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_SOUTH));
	assert(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) == 32767);
	SDL_Delay(40);
	android_touch_update();
	SDL_UpdateJoysticks();
	assert(!SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_SOUTH));
	assert(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) == 0);

	android_touch_set_state(true, 1u << SDL_GAMEPAD_BUTTON_START, {1, 0, 0, 0, 0, 1});
	android_touch_update();
	SDL_UpdateJoysticks();
	assert(SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_START));
	android_touch_set_state(false, 0, {});
	assert(android_touch_update() == 0);
	assert(!SDL_GamepadConnected(pad));
	SDL_CloseGamepad(pad);
	android_touch_set_state(true, 0, {});
	id = android_touch_update();
	pad = SDL_OpenGamepad(id);
	assert(pad);
	SDL_UpdateJoysticks();
	assert(!SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_START));
	assert(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) == 0);
	SDL_CloseGamepad(pad);
	android_touch_shutdown();
	SDL_Quit();
	puts("Android touch controller tests passed");
}
