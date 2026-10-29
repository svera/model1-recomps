#include "romset.h"
#include "profile.h"

namespace game::swa {
namespace {

using namespace romset;

// MAME 0.280's ROM_START(swa). Repeated entries reproduce ROM_RELOAD, including byte swapping.
const rom ROMS[] = {
	{ "epr-16669.14", 0x52e5e7e4, MAINCPU, 0x200000, BYTE16 },
	{ "epr-16670.15", 0x1e7ecabd, MAINCPU, 0x200001, BYTE16 },
	{ "epr-16668.5", 0x9e112425, MAINCPU, 0xf80000, RAW },
	{ "epr-16668.5", 0x9e112425, MAINCPU, 0x000000, RAW },
	{ "epr-16668.5", 0x9e112425, MAINCPU, 0x080000, RAW },
	// MAME marks the available TGP program BAD_DUMP.
	{ "315-5711.bin", 0xc5ddb8fc, TGP_PROGRAM, 0x0, RAW },
	{ "epr-16470.7", 0x7da18cf7, SNDCPU, 0x00000, WORDSWAP },
	{ "epr-16470.7", 0x7da18cf7, SNDCPU, 0x80000, WORDSWAP },
	{ "mpr-16486.32", 0x7df50533, PCM1, 0x000000, RAW },
	{ "mpr-16487.33", 0x31b28dfa, PCM1, 0x200000, RAW },
	{ "mpr-16484.4", 0x9d4c334d, PCM2, 0x000000, RAW },
	{ "mpr-16485.5", 0x95aadcad, PCM2, 0x200000, RAW },
	{ "epr-16471.2", 0xf4ee84a4, MPEGCPU, 0x000000, RAW },
	{ "mpr-16514.57", 0x3175b0be, MPEG, 0x000000, RAW },
	{ "mpr-16515.58", 0x3114d748, MPEG, 0x200000, RAW },
	{ "mpr-16476.26", 0xd48609ae, POLYGONS, 0x000000, WORD32 },
	{ "mpr-16477.27", 0xb979b082, POLYGONS, 0x000002, WORD32 },
	{ "mpr-16478.28", 0x80c780f7, POLYGONS, 0x400000, WORD32 },
	{ "mpr-16479.29", 0xe43183b3, POLYGONS, 0x400002, WORD32 },
	{ "mpr-16480.30", 0x3185547a, POLYGONS, 0x800000, WORD32 },
	{ "mpr-16481.31", 0xce8d76fe, POLYGONS, 0x800002, WORD32 },
	{ "mpr-16472.39", 0x5a0d7553, COPRO_DATA, 0x000000, BYTE32 },
	{ "mpr-16473.40", 0x876c5399, COPRO_DATA, 0x000001, BYTE32 },
	{ "mpr-16474.41", 0x5864a26f, COPRO_DATA, 0x000002, BYTE32 },
	{ "mpr-16475.42", 0xb9266be9, COPRO_DATA, 0x000003, BYTE32 },
};

constexpr std::array<region_def, N_REGIONS> regions()
{
	std::array<region_def, N_REGIONS> r{};
	for (int i = 0; i < N_REGIONS; i++) r[i] = COMMON_REGIONS[i];
	r[COPRO_DATA] = { 0x200000, 0 };
	r[TGP_PROGRAM] = { 0x2000, 0 };
	r[POLYGONS] = { 0xc00000, 0 };
	r[MPEGCPU] = { 0x20000, 0 };
	r[MPEG] = { 0x800000, 0 };
	return r;
}

}

const romset::definition ROMSET{
	ID, "swa.zip", "swa", "Star Wars Arcade (US)", regions(), ROMS, 0x9e112425
};
static const romset::registration REGISTERED{ ROMSET };

}
