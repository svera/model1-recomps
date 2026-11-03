#include "../src/host/gamepads.h"
#include <cassert>

static SDL_JoystickID attach_pad(Uint16 product)
{
	SDL_VirtualJoystickDesc desc;
	SDL_INIT_INTERFACE(&desc);
	desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
	desc.vendor_id = 0x1209;
	desc.product_id = product;
	desc.name = "Hotplug regression pad";
	desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
	desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
	desc.axis_mask = (1u << SDL_GAMEPAD_AXIS_COUNT) - 1;
	desc.button_mask = (1u << SDL_GAMEPAD_BUTTON_COUNT) - 1;
	const auto id = SDL_AttachVirtualJoystick(&desc);
	assert(id && SDL_IsGamepad(id));
	return id;
}

int main()
{
	SDL_SetHint(SDL_HINT_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT, "0x1209/0x0001,0x1209/0x0002");
	SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
	assert(SDL_Init(SDL_INIT_GAMEPAD));
	SDL_Gamepad *pads[2]{};
	player_controls ctl[2];
	assert(!refresh_gamepads(pads, ctl, false));
	assert(!pads[0] && !pads[1]);

	const auto first = attach_pad(1);
	// The periodic reconciliation must work even without a queued connection event.
	SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);
	assert(refresh_gamepads(pads, ctl, false));
	assert(SDL_GetGamepadID(pads[0]) == first && !pads[1]);
	auto *kept = pads[0];
	for (int i = 0; i < 3; i++) {
		assert(!refresh_gamepads(pads, ctl, false));
		assert(pads[0] == kept);
	}
	const auto second = attach_pad(2);
	assert(refresh_gamepads(pads, ctl, false));
	assert(pads[0] == kept && SDL_GetGamepadID(pads[1]) == second);
	const std::array<SDL_JoystickID, 2> claimed{second, first};
	assert(refresh_gamepads(pads, ctl, false, claimed));
	assert(SDL_GetGamepadID(pads[0]) == second && pads[1] == kept);
	assert(!refresh_gamepads(pads, ctl, false, claimed));
	assert(SDL_GetGamepadID(pads[0]) == second && pads[1] == kept);
	assert(refresh_gamepads(pads, ctl, false));
	auto *joystick = SDL_GetGamepadJoystick(pads[0]);
	assert(SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_LEFTX, 24000));
	assert(SDL_SetJoystickVirtualButton(joystick, SDL_GAMEPAD_BUTTON_SOUTH, true));
	SDL_UpdateJoysticks();
	assert(SDL_GetGamepadAxis(pads[0], SDL_GAMEPAD_AXIS_LEFTX) == 24000);
	assert(SDL_GetGamepadButton(pads[0], SDL_GAMEPAD_BUTTON_SOUTH));

	assert(refresh_gamepads(pads, ctl, true));
	assert(SDL_GetGamepadID(pads[0]) == second && pads[1] == kept);
	assert(!refresh_gamepads(pads, ctl, true));
	assert(SDL_GetGamepadID(pads[0]) == second && pads[1] == kept);
	ctl[1].pad_choice = gamepad_guid(first);
	assert(!refresh_gamepads(pads, ctl, false));
	assert(SDL_GetGamepadID(pads[0]) == second && SDL_GetGamepadID(pads[1]) == first);
	ctl[0].pad_choice = "none";
	assert(refresh_gamepads(pads, ctl, false));
	assert(!pads[0] && SDL_GetGamepadID(pads[1]) == first);
	assert(SDL_DetachVirtualJoystick(first));
	assert(refresh_gamepads(pads, ctl, false));
	assert(!pads[0] && SDL_GetGamepadID(pads[1]) == second);

	const auto reconnected = attach_pad(1);
	assert(reconnected != first);
	SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);
	assert(refresh_gamepads(pads, ctl, false));
	assert(!pads[0] && SDL_GetGamepadID(pads[1]) == reconnected);
	assert(!SDL_GetGamepadButton(pads[1], SDL_GAMEPAD_BUTTON_SOUTH));
	assert(SDL_DetachVirtualJoystick(second));
	assert(SDL_DetachVirtualJoystick(reconnected));
	assert(refresh_gamepads(pads, ctl, false));
	assert(!pads[0] && !pads[1]);
	SDL_Quit();
	printf("Gamepad hotplug: late connection, missed events, live input, stable handles, assignments, swaps and reconnection passed\n");
}
