#pragma once

#include "../game.h"

namespace game::wingwar {

// This TGP distance read feeds Wing War's model-tier table at 0x02fcba; its separate 0x1a
// view checks remain untouched.
inline constexpr uint32_t LOD_RESULT_READ = 0x02fc8e;

struct gameplay_state : game::gameplay_state
{
	bool lod = false;
};

gameplay_callbacks create_gameplay(model1 &machine);
void set_lod(model1 &machine, bool enabled);
uint32_t lod_result(bool enabled, uint32_t pc, uint32_t value);

}
