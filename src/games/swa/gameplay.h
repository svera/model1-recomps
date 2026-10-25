#pragma once

#include "../game.h"

namespace game::swa {

struct gameplay_state : game::gameplay_state
{
	bool lod = true;   // the arcade's distance-based models (the host's setting turns it off)
	uint64_t nearest = 0;          // detail results replaced by the nearest level's (LOD off)
	uint64_t destroyers_near = 0;  // Star Destroyers drawn with their near model (LOD off)
};

gameplay_callbacks create_gameplay(model1 &machine);
void set_lod(model1 &machine, bool enabled);
// A model's detail result as the V60 reads it at `pc`: with LOD off, the nearest level's for every
// object the game draws; culled objects and other results unchanged.
uint32_t lod_result(bool enabled, uint32_t pc, uint32_t value);
// A Star Destroyer's detail byte as its drawing routine reads it: with LOD off, the near model's
// for every Star Destroyer the game draws.
uint8_t destroyer_detail(bool enabled, uint8_t value);

}
