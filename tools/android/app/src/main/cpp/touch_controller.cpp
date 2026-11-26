#include "touch_controller.h"

#include <SDL3/SDL.h>
#ifdef __ANDROID__
#include <SDL3/SDL_system.h>
#include <jni.h>
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <mutex>

namespace {
struct touch_state {
	bool enabled = false;
	unsigned buttons = 0;
	std::array<float, 6> axes{};
};
std::mutex state_mutex;
touch_state state;
using clock_type = std::chrono::steady_clock;
std::array<clock_type::time_point, SDL_GAMEPAD_BUTTON_COUNT> button_until{};
std::array<clock_type::time_point, 2> trigger_until{};
unsigned pending_buttons = 0, pending_triggers = 0;
SDL_JoystickID touch_id = 0;
SDL_Joystick *touch_joystick = nullptr;
touch_state applied;
bool have_applied = false;

void report(const char *operation)
{
	fprintf(stderr, "touch controls: %s: %s\n", operation, SDL_GetError());
}

void detach()
{
	if (touch_joystick) SDL_CloseJoystick(touch_joystick);
	touch_joystick = nullptr;
	if (touch_id && !SDL_DetachVirtualJoystick(touch_id)) report("detach");
	touch_id = 0;
	have_applied = false;
}
}

void android_touch_set_state(bool enabled, unsigned buttons, std::array<float, 6> axes, bool vr_stick)
{
	if (vr_stick) axes[SDL_GAMEPAD_AXIS_RIGHTY] = axes[SDL_GAMEPAD_AXIS_LEFTY];
	std::lock_guard<std::mutex> lock(state_mutex);
	if (!enabled) {
		button_until.fill({});
		trigger_until.fill({});
		pending_buttons = pending_triggers = 0;
	} else {
		for (int button = 0; button < SDL_GAMEPAD_BUTTON_COUNT; ++button)
			if ((buttons & (1u << button)) && !(state.buttons & (1u << button)))
				pending_buttons |= 1u << button;
		for (int trigger = 0; trigger < 2; ++trigger)
			if (axes[SDL_GAMEPAD_AXIS_LEFT_TRIGGER + trigger] >= 0.99f
					&& state.axes[SDL_GAMEPAD_AXIS_LEFT_TRIGGER + trigger] < 0.99f)
				pending_triggers |= 1u << trigger;
	}
	state = {enabled, buttons, axes};
}

#ifdef __ANDROID__
extern "C" JNIEXPORT void JNICALL
Java_recomp_vr1_android_TouchControls_nativeSetState(JNIEnv *, jclass, jboolean enabled,
	jint buttons, jfloat lx, jfloat ly, jfloat rx, jfloat ry, jfloat lt, jfloat rt, jboolean vr_stick)
{
	android_touch_set_state(bool(enabled), unsigned(buttons), {lx, ly, rx, ry, lt, rt}, bool(vr_stick));
}
#endif

SDL_JoystickID android_touch_update()
{
	touch_state current;
	{
		std::lock_guard<std::mutex> lock(state_mutex);
		current = state;
		if (current.enabled) {
			const auto now = clock_type::now();
			const auto until = now + std::chrono::milliseconds(34);
			for (int button = 0; button < SDL_GAMEPAD_BUTTON_COUNT; ++button) {
				if (pending_buttons & (1u << button)) button_until[button] = until;
				if (button_until[button] > now) current.buttons |= 1u << button;
			}
			for (int trigger = 0; trigger < 2; ++trigger) {
				if (pending_triggers & (1u << trigger)) trigger_until[trigger] = until;
				if (trigger_until[trigger] > now)
					current.axes[SDL_GAMEPAD_AXIS_LEFT_TRIGGER + trigger] = 1;
			}
			pending_buttons = pending_triggers = 0;
		}
	}
	if (!current.enabled) {
		if (touch_id) detach();
		return 0;
	}
	if (!touch_id) {
		SDL_VirtualJoystickDesc desc;
		SDL_INIT_INTERFACE(&desc);
		desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
		desc.name = "Touch controls";
		desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
		desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
		desc.axis_mask = (1u << SDL_GAMEPAD_AXIS_COUNT) - 1;
		desc.button_mask = (1u << SDL_GAMEPAD_BUTTON_COUNT) - 1;
		touch_id = SDL_AttachVirtualJoystick(&desc);
		if (!touch_id) { report("attach"); return 0; }
		touch_joystick = SDL_OpenJoystick(touch_id);
		if (!touch_joystick) { report("open"); detach(); return 0; }
	}
	bool ok = true;
	for (int axis = 0; axis < SDL_GAMEPAD_AXIS_COUNT; ++axis) {
		if (have_applied && current.axes[axis] == applied.axes[axis]) continue;
		const float v = axis >= SDL_GAMEPAD_AXIS_LEFT_TRIGGER
			? 2 * std::clamp(current.axes[axis], 0.0f, 1.0f) - 1
			: std::clamp(current.axes[axis], -1.0f, 1.0f);
		const Sint16 value = Sint16(v < 0 ? v * 32768 : v * 32767);
		if (!SDL_SetJoystickVirtualAxis(touch_joystick, axis, value)) { report("set axis"); ok = false; }
	}
	for (int button = 0; button < SDL_GAMEPAD_BUTTON_COUNT; ++button)
		if ((!have_applied || ((current.buttons ^ applied.buttons) & (1u << button)))
				&& !SDL_SetJoystickVirtualButton(touch_joystick, button, (current.buttons & (1u << button)) != 0)) {
			report("set button");
			ok = false;
		}
	if (ok) { applied = current; have_applied = true; }
	return touch_id;
}

#ifdef __ANDROID__
void android_touch_ui_state(bool menu_open, bool pc_mode)
{
	auto *env = static_cast<JNIEnv *>(SDL_GetAndroidJNIEnv());
	auto activity = static_cast<jobject>(SDL_GetAndroidActivity());
	if (!env || !activity) {
		fprintf(stderr, "touch controls: cannot access Android activity\n");
		return;
	}
	jclass cls = env->GetObjectClass(activity);
	jmethodID method = cls ? env->GetMethodID(cls, "setTouchUiState", "(ZZ)V") : nullptr;
	if (method) env->CallVoidMethod(activity, method, jboolean(menu_open), jboolean(pc_mode));
	if (env->ExceptionCheck()) {
		env->ExceptionDescribe();
		env->ExceptionClear();
	}
	if (cls) env->DeleteLocalRef(cls);
	env->DeleteLocalRef(activity);
}

void android_lan_discovery(bool enabled)
{
	auto *env = static_cast<JNIEnv *>(SDL_GetAndroidJNIEnv());
	auto activity = static_cast<jobject>(SDL_GetAndroidActivity());
	if (!env || !activity) {
		fprintf(stderr, "LAN: cannot access Android activity for discovery\n");
		return;
	}
	jclass cls = env->GetObjectClass(activity);
	jmethodID method = cls ? env->GetMethodID(cls, "setLanDiscovery", "(Z)V") : nullptr;
	if (method) env->CallVoidMethod(activity, method, jboolean(enabled));
	else fprintf(stderr, "LAN: cannot enable Android discovery\n");
	if (env->ExceptionCheck()) {
		env->ExceptionDescribe();
		env->ExceptionClear();
	}
	if (cls) env->DeleteLocalRef(cls);
	env->DeleteLocalRef(activity);
}
#endif

void android_touch_shutdown()
{
	detach();
}
