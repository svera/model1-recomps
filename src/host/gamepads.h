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
	const std::array<SDL_JoystickID, 2> &preferred = {})
{
	int count = 0;
	SDL_JoystickID *ids = SDL_GetGamepads(&count);
	if (!ids) {
		fprintf(stderr, "Couldn't enumerate gamepads: %s\n", SDL_GetError());
		return false;
	}
	std::array<SDL_JoystickID, 2> before{};
	for (int p = 0; p < 2; p++)
		if (pads[p]) before[p] = SDL_GetGamepadID(pads[p]);
	std::array<SDL_Gamepad *, 2> next{};
	std::vector<bool> used(count, false);
	for (int p = 0; p < 2; p++)
		for (int i = 0; preferred[p] && i < count; i++) {
			if (used[i] || ids[i] != preferred[p]) continue;
			for (int old = 0; old < 2; old++)
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
		for (int p = 0; p < 2; p++) {
			const std::string &choice = ctl[p].pad_choice;
			if (next[p] || choice == "none") continue;
			const bool chosen = choice != "auto";
			if ((pass == 0 && !chosen) || (pass == 1 && chosen)) continue;
			for (int i = 0; i < count && !next[p]; i++) {
				if (used[i] || (pass == 0 && gamepad_guid(ids[i]) != choice)) continue;
				used[i] = true;
				for (int old = 0; old < 2; old++)
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
	for (auto *pad : { pads[0], pads[1] })
		if (pad && pad != next[0] && pad != next[1]) SDL_CloseGamepad(pad);
	bool changed = false;
	for (int p = 0; p < 2; p++) {
		pads[p] = next[p];
		changed |= before[p] != (pads[p] ? SDL_GetGamepadID(pads[p]) : 0);
	}
	return changed;
}
