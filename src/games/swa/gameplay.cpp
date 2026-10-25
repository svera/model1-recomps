// Star Wars Arcade's model detail. Three drawing routines (the ships and the larger scenery) ask
// the TGP (command 0x22) for an object's screen-size measure, drop the object above 0x6FFF, and
// otherwise pick one of its five models from the byte table at 23FE6E indexed by the measure / 32:
// level 0 below 0x180, 1 from 0x180, 2 from 0x2A0, 3 from 0x460, 4 from 0x700. The model entry
// is the type's list plus level * 8. Nothing else reads the measure, and the choice is not kept in
// RAM, so a smaller measure changes only the model in the display list.
//
// Star Destroyers choose by rank instead: 0DBFF6 sorts them by distance and stores each one's level
// in 3A[object] from the byte table at DC0C2 (the nearest -1: the near model with its separate
// parts; the next two 0, the rest 2; 7F: not drawn). Ranks up to 2 also join a list at 6D80 that the
// game uses, so the stored byte stays; only the drawing routine's read of it, at 0DC336, changes.
// The routine then uses level 2 for every Star Destroyer while the game's mode byte 7009[R25] is 2
// (the attract mode's scenes, including the start of its X-wing demonstration); its read at 0DC341
// reads another mode. The routine leaves the level in R17 for DCE13, which runs the near Star
// Destroyer's turret timers (and their shots) only for -1. So for one drawn near only by this
// setting, the turrets' enable mask read at 0DCE68 reads all set, the path the game takes for a
// disabled turret: no RAM written.
//
// The Star Destroyers of the fleet 0D6540 draws (the attract mode's, and 0EEC62's) pick their
// model from the table at D6E12 by two TGP measures and store it in the object (2[R20]: the model,
// 6[R20]: its data) for C1869 to draw. Only C1869's reads of those fields change, from the far
// model to the near one, so the object keeps what the game stored.
#include "gameplay.h"
#include "../../machine/model1.h"

namespace game::swa {

namespace {
// the `in.w [FP], R0` reading each routine's measure
constexpr uint32_t LOD_READS[] = { 0x0cdfd8, 0x0d1d38, 0x0e7fd3 };
constexpr uint32_t CULLED_ABOVE = 0x6fff;
constexpr uint32_t DESTROYER_DETAIL_READ = 0x0dc336;   // movz.bw 3A[R20], R17
constexpr uint32_t ALL_FAR_READ = 0x0dc341;            // cmp.b #2, 7009[R25]
constexpr uint32_t TURRET_MASK_READ = 0x0dce68;        // and.h 44[R20], R0
constexpr uint32_t MODE = 0x7009;                      // in the game's variables (R25)
constexpr uint8_t MODE_ALL_FAR = 2;
constexpr int VARIABLES_REG = 25;
constexpr uint32_t FLEET_DATA_READ = 0x0c18aa;    // mov.w 6[R20], [R11+]
constexpr uint32_t FLEET_MODEL_READ = 0x0c18af;   // mov.w [2[R20]], [R11+]
constexpr uint32_t FLEET_MODEL = 2, FLEET_DATA = 6;   // in the drawn object
constexpr uint32_t FLEET_FAR_MODEL = 0x23c01a, FLEET_FAR_DATA = 0x53698;
constexpr uint32_t FLEET_NEAR_MODEL = 0x23c016, FLEET_NEAR_DATA = 0x5360f;
constexpr uint32_t OBJECTS_FIRST = 0x400000, OBJECTS_LAST = 0x40fffc;   // RAM A
constexpr uint32_t DETAIL = 0x3a, TURRET_MASK = 0x44;  // in a Star Destroyer's object
constexpr uint8_t DESTROYER_HIDDEN = 0x7f, DESTROYER_NEAR = 0xff;
constexpr int OBJECT_REG = 20;   // R20: the Star Destroyer's object
}

uint32_t lod_result(bool enabled, uint32_t pc, uint32_t value)
{
	if (enabled || value > CULLED_ABOVE) return value;
	for (uint32_t read : LOD_READS)
		if (pc == read) return 0;
	return value;
}

uint8_t destroyer_detail(bool enabled, uint8_t value)
{
	return enabled || value == DESTROYER_HIDDEN ? value : DESTROYER_NEAR;
}

void set_lod(model1 &machine, bool enabled)
{
	std::static_pointer_cast<gameplay_state>(machine.game_state())->lod = enabled;
}

gameplay_callbacks create_gameplay(model1 &machine)
{
	gameplay_callbacks callbacks;
	auto state = std::make_shared<gameplay_state>();
	callbacks.state = state;
	callbacks.copro_result = [state](uint32_t pc, uint32_t value) {
		const uint32_t result = lod_result(state->lod, pc, value);
		if (result != value) state->nearest++;
		return result;
	};
	callbacks.read_pc_first = DESTROYER_DETAIL_READ;
	callbacks.read_pc_last = TURRET_MASK_READ;
	// only the operand itself: an interrupt taken at these instructions reads its vector with the same PC
	callbacks.read_value = [&machine, state](uint32_t pc, uint32_t address, uint16_t value) -> uint16_t {
		if (state->lod) return value;
		const uint32_t object = machine.cpu().reg(OBJECT_REG);
		if (pc == DESTROYER_DETAIL_READ && address == object + DETAIL) {
			const uint8_t result = destroyer_detail(false, uint8_t(value));
			if (result != value) state->destroyers_near++;
			return result;
		}
		const uint32_t mode = machine.cpu().reg(VARIABLES_REG) + MODE;
		if (pc == ALL_FAR_READ && address == mode && value == MODE_ALL_FAR) return 0;
		if (pc == TURRET_MASK_READ && address == object + TURRET_MASK &&
			(machine.read8(object + DETAIL) != DESTROYER_NEAR || machine.read8(mode) == MODE_ALL_FAR)) return 0xffff;
		return value;
	};
	callbacks.read32_watch = OBJECTS_FIRST;
	callbacks.read32_watch_end = OBJECTS_LAST;
	callbacks.read32_value = [&machine, state](uint32_t pc, uint32_t address, uint32_t value) -> uint32_t {
		if (state->lod || (pc != FLEET_MODEL_READ && pc != FLEET_DATA_READ)) return value;
		const uint32_t object = machine.cpu().reg(OBJECT_REG);
		if (pc == FLEET_MODEL_READ && address == object + FLEET_MODEL && value == FLEET_FAR_MODEL) {
			state->destroyers_near++;
			return FLEET_NEAR_MODEL;
		}
		if (pc == FLEET_DATA_READ && address == object + FLEET_DATA && value == FLEET_FAR_DATA) return FLEET_NEAR_DATA;
		return value;
	};
	return callbacks;
}

}
