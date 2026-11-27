#pragma once
#include "controls.h"
#include <array>
#include <cstdio>
#include <vector>

inline std::string gamepad_guid(SDL_JoystickID id)
{
	char guid[64];
	SDL_GUIDToString(SDL_GetGamepadGUIDForID(id), guid, sizeof guid);
	return guid;
}

// Reconcile rather than reopening every pad: a Bluetooth connection can appear in stages.
inline bool refresh_gamepads(SDL_Gamepad *pads[2], const player_controls ctl[2], bool swapped,
	const std::array<SDL_JoystickID, 2> &preferred = {});

template<size_t N>
inline bool refresh_gamepad_slots(SDL_Gamepad **pads, const player_controls *ctl, bool swapped,
	const std::array<SDL_JoystickID, N> &preferred)
{
	int count = 0;
	SDL_JoystickID *ids = SDL_GetGamepads(&count);
	if (!ids) {
		fprintf(stderr, "Couldn't enumerate gamepads: %s\n", SDL_GetError());
		return false;
	}
	std::array<SDL_JoystickID, N> before{};
	for (size_t p = 0; p < N; p++)
		if (pads[p]) before[p] = SDL_GetGamepadID(pads[p]);
	std::array<SDL_Gamepad *, N> next{};
	std::vector<bool> used(count, false);
	for (size_t p = 0; p < N; p++)
		for (int i = 0; preferred[p] && i < count; i++) {
			if (used[i] || ids[i] != preferred[p]) continue;
			for (size_t old = 0; old < N; old++)
				if (before[old] == ids[i]) next[p] = pads[old];
			if (!next[p]) {
				next[p] = SDL_OpenGamepad(ids[i]);
				if (!next[p])
					fprintf(stderr, "Couldn't open gamepad %u: %s (will retry)\n",
						unsigned(ids[i]), SDL_GetError());
			}
			used[i] = next[p] != nullptr;
			break;
		}
	// Explicit choices first, then automatic assignments, then missing choices' fallbacks.
	for (int pass = 0; pass < 3; pass++)
		for (size_t p = 0; p < N; p++) {
			const std::string &choice = ctl[p].pad_choice;
			if (next[p] || choice == "none") continue;
			const bool chosen = choice != "auto";
			if ((pass == 0 && !chosen) || (pass == 1 && chosen)) continue;
			for (int i = 0; i < count && !next[p]; i++) {
				if (used[i] || (pass == 0 && gamepad_guid(ids[i]) != choice)) continue;
				used[i] = true;
				for (size_t old = 0; old < N; old++)
					if (before[old] == ids[i]) next[p] = pads[old];
				if (!next[p]) {
					next[p] = SDL_OpenGamepad(ids[i]);
					if (!next[p])
						fprintf(stderr, "Couldn't open gamepad %u: %s (will retry)\n",
							unsigned(ids[i]), SDL_GetError());
				}
			}
		}
	SDL_free(ids);
	if (swapped && next[0] && next[1]) std::swap(next[0], next[1]);
	for (size_t p = 0; p < N; p++)
		if (pads[p] && std::find(next.begin(), next.end(), pads[p]) == next.end()) SDL_CloseGamepad(pads[p]);
	bool changed = false;
	for (size_t p = 0; p < N; p++) {
		pads[p] = next[p];
		changed |= before[p] != (pads[p] ? SDL_GetGamepadID(pads[p]) : 0);
	}
	return changed;
}

inline bool refresh_gamepads(SDL_Gamepad *pads[2], const player_controls ctl[2], bool swapped,
	const std::array<SDL_JoystickID, 2> &preferred)
{
	return refresh_gamepad_slots(pads, ctl, swapped, preferred);
}
