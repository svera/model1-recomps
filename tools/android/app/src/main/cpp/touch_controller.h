#pragma once

#include <SDL3/SDL_joystick.h>
#include <array>

void android_touch_set_state(bool enabled, unsigned buttons, std::array<float, 6> axes, bool vr_stick = false);
SDL_JoystickID android_touch_update();
void android_touch_ui_state(bool menu_open, bool pc_mode);
void android_lan_discovery(bool enabled);
void android_touch_shutdown();
