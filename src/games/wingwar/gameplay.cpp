#include "gameplay.h"
#include "../../machine/model1.h"
#include <bit>

namespace game::wingwar {

namespace {
constexpr uint32_t NEAREST_DETAIL_DISTANCE = std::bit_cast<uint32_t>(256.0f);
}

uint32_t lod_result(bool arcade_lod, uint32_t pc, uint32_t value)
{
	// The V60 converts this distance to 256, then shifts by eight to select model tier 1.
	return !arcade_lod && pc == LOD_RESULT_READ ? NEAREST_DETAIL_DISTANCE : value;
}

gameplay_callbacks create_gameplay(model1 &)
{
	gameplay_callbacks callbacks;
	auto state = std::make_shared<gameplay_state>();
	callbacks.state = state;
	callbacks.copro_result = [state](uint32_t pc, uint32_t value) {
		return lod_result(state->lod, pc, value);
	};
	return callbacks;
}

void set_lod(model1 &machine, bool enabled)
{
	std::static_pointer_cast<gameplay_state>(machine.game_state())->lod = enabled;
}

}
