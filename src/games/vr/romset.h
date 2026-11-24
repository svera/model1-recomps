// Virtua Racing's MAME set ("vr"), for the shared Model 1 loader (src/machine/model1_romset.h).
#pragma once
#include "../../machine/model1_romset.h"
#include <zlib.h>

namespace game::vr {

extern const romset::definition ROMSET;

inline constexpr uint32_t EXTENDED_BOOT_4 = 0x8678f78d, EXTENDED_BOOT_5 = 0x4aecba14;

inline bool is_extended_program(const std::vector<uint8_t> &program)
{
	return program.size() >= 0x1000000 &&
		uint32_t(crc32(0, program.data() + 0xfc0000, 0x20000)) == EXTENDED_BOOT_4 &&
		uint32_t(crc32(0, program.data() + 0xfe0000, 0x20000)) == EXTENDED_BOOT_5;
}

} // namespace game::vr
