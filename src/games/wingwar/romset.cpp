#include "romset.h"
#include "profile.h"

namespace game::wingwar {
namespace {

using namespace romset;

// MAME's src/mame/sega/model1.cpp, ROM_START(wingwar), past the CPU board's ROMs.
const rom ROMS[] = {
	{ "epr-16729.14", 0x7edec2cc, MAINCPU, 0x200000, BYTE16 },
	{ "epr-16730.15", 0xbab24dee, MAINCPU, 0x200001, BYTE16 },
	{ "epr-16953.4", 0xc821a920, MAINCPU, 0xfc0000, RAW },
	{ "epr-16953.4", 0xc821a920, MAINCPU, 0x000000, RAW },
	{ "epr-16952.5", 0x03a3ecc5, MAINCPU, 0xfe0000, RAW },
	{ "epr-16952.5", 0x03a3ecc5, MAINCPU, 0x020000, RAW },
	{ "mpr-16738.6", 0x51518ffa, MAINCPU, 0x1000000, BYTE16 },
	{ "mpr-16737.7", 0x37b1379c, MAINCPU, 0x1000001, BYTE16 },
	{ "mpr-16736.8", 0x10b6a025, MAINCPU, 0x1100000, BYTE16 },
	{ "mpr-16735.9", 0xc82fd198, MAINCPU, 0x1100001, BYTE16 },
	{ "mpr-16734.10", 0xf76371c1, MAINCPU, 0x1200000, BYTE16 },
	{ "mpr-16733.11", 0xe105847b, MAINCPU, 0x1200001, BYTE16 },
	{ "315-5711.bin", 0x6a21f304, TGP_PROGRAM, 0x0000, RAW },
	{ "epr-17126.7", 0x50178e40, SNDCPU, 0x000000, WORDSWAP },
	{ "epr-16752.8", 0x6541c48f, SNDCPU, 0x020000, WORDSWAP },
	{ "epr-16752.8", 0x6541c48f, SNDCPU, 0x080000, WORDSWAP },
	{ "mpr-16753.32", 0x324a8333, PCM1, 0x000000, RAW },
	{ "mpr-16754.33", 0x144f3cf5, PCM1, 0x200000, RAW },
	{ "mpr-16755.4", 0x4baaf878, PCM2, 0x000000, RAW },
	{ "mpr-16756.5", 0xd9c40672, PCM2, 0x200000, RAW },
	{ "mpr-16743.26", 0xa710d33c, POLYGONS, 0x000000, WORD32 },
	{ "mpr-16744.27", 0xde796e1f, POLYGONS, 0x000002, WORD32 },
	{ "mpr-16745.28", 0x905b689c, POLYGONS, 0x400000, WORD32 },
	{ "mpr-16746.29", 0x163b312e, POLYGONS, 0x400002, WORD32 },
	{ "mpr-16747.30", 0x7353bb12, POLYGONS, 0x800000, WORD32 },
	{ "mpr-16748.31", 0x8ce98d3a, POLYGONS, 0x800002, WORD32 },
	{ "mpr-16749.32", 0x0e36dc1a, POLYGONS, 0xc00000, WORD32 },
	{ "mpr-16750.33", 0xe4f0b98d, POLYGONS, 0xc00002, WORD32 },
	{ "mpr-16741.39", 0x84b2ffd8, COPRO_DATA, 0x000000, BYTE32 },
	{ "mpr-16742.40", 0xe9cc12bb, COPRO_DATA, 0x000001, BYTE32 },
	{ "mpr-16739.41", 0x6c73e98f, COPRO_DATA, 0x000002, BYTE32 },
	{ "mpr-16740.42", 0x44b31007, COPRO_DATA, 0x000003, BYTE32 },
};

constexpr std::array<region_def, N_REGIONS> regions()
{
	std::array<region_def, N_REGIONS> r{};
	for (int i = 0; i < N_REGIONS; i++) r[i] = COMMON_REGIONS[i];
	r[COPRO_DATA] = { 0x200000, 0 };
	r[TGP_PROGRAM] = { 0x2000, 0 };
	return r;
}

}

const romset::definition ROMSET{
	ID, "wingwar.zip", "wingwar", "Wing War", regions(), ROMS, 0xc821a920, nullptr, false, true
};
static const romset::registration REGISTERED{ ROMSET };

}
