#pragma once

#include <SDL3/SDL_joystick.h>
#include <array>

void android_touch_set_state(bool enabled, unsigned buttons, const std::array<float, 6> &axes);
SDL_JoystickID android_touch_update();
void android_touch_ui_state(bool menu_open, bool pc_mode);
void android_touch_shutdown();
