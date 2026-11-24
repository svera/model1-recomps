#include "romset.h"
#include "profile.h"
#include <cstring>

namespace game::vr {

namespace {

using namespace romset;

// MAME's src/mame/sega/model1.cpp, ROM_START(vr), past the CPU board's ROMs (as tools/romset.py).
// VR also has the TGP's program and its track data.
const rom ROMS[] = {
	{ "epr-14882.14", 0x547d75ad, MAINCPU, 0x200000, BYTE16 },
	{ "epr-14883.15", 0x6bfad8b1, MAINCPU, 0x200001, BYTE16 },
	{ "epr-14878a.4", 0x6d69e695, MAINCPU, 0xfc0000, RAW },
	{ "epr-14879a.5", 0xd45af9dd, MAINCPU, 0xfe0000, RAW },
	{ "mpr-14880.6", 0xadc7c208, MAINCPU, 0x1000000, BYTE16 },
	{ "mpr-14881.7", 0xe5ab89df, MAINCPU, 0x1000001, BYTE16 },
	{ "mpr-14884.8", 0x6cf9c026, MAINCPU, 0x1100000, BYTE16 },
	{ "mpr-14885.9", 0xf65c9262, MAINCPU, 0x1100001, BYTE16 },
	{ "mpr-14886.10", 0x92868734, MAINCPU, 0x1200000, BYTE16 },
	{ "mpr-14887.11", 0x10c7c636, MAINCPU, 0x1200001, BYTE16 },
	{ "mpr-14888.12", 0x04bfdc5b, MAINCPU, 0x1300000, BYTE16 },
	{ "mpr-14889.13", 0xc49f0486, MAINCPU, 0x1300001, BYTE16 },
	{ "315-5573.bin", 0x3335a19b, TGP_PROGRAM, 0x0, RAW },
	{ "epr-14870a.7", 0x919d9b75, SNDCPU, 0x0, WORDSWAP },
	{ "mpr-14873.32", 0xb1965190, PCM1, 0x0, RAW },
	{ "mpr-14876.4", 0xba6b2327, PCM2, 0x0, RAW },
	{ "mpr-14890.26", 0xdcbe006b, POLYGONS, 0x000000, WORD32 },
	{ "mpr-14891.27", 0x25832b38, POLYGONS, 0x000002, WORD32 },
	{ "mpr-14892.28", 0x5136f3ba, POLYGONS, 0x400000, WORD32 },
	{ "mpr-14893.29", 0x1c531ada, POLYGONS, 0x400002, WORD32 },
	{ "mpr-14894.30", 0x830a71bc, POLYGONS, 0x800000, WORD32 },
	{ "mpr-14895.31", 0xaf027ac5, POLYGONS, 0x800002, WORD32 },
	{ "mpr-14896.32", 0x382091dc, POLYGONS, 0xc00000, WORD32 },
	{ "mpr-14897.33", 0x74873195, POLYGONS, 0xc00002, WORD32 },
	{ "mpr-14898.39", 0x61da2bb6, COPRO_DATA, 0x000000, BYTE32 },
	{ "mpr-14899.40", 0x2cd58bee, COPRO_DATA, 0x000001, BYTE32 },
	{ "mpr-14900.41", 0xaa7c017d, COPRO_DATA, 0x000002, BYTE32 },
	{ "mpr-14901.42", 0x175b7a9a, COPRO_DATA, 0x000003, BYTE32 },
};

constexpr std::array<region_def, N_REGIONS> regions()
{
	std::array<region_def, N_REGIONS> r{};
	for (int i = 0; i < N_REGIONS; i++) r[i] = COMMON_REGIONS[i];
	r[COPRO_DATA] = { 0x200000, 0 };
	r[TGP_PROGRAM] = { 0x2000, 0 };
	return r;
}

// v0.3's boot ROM identifies the patched program. Course data varies with the builder's
// tuning options, so its ROMs are checked by name, size and zip CRC instead of a fixed CRC.
const auto EXTENDED_ROMS = [] {
	auto roms = std::to_array(ROMS);
	roms[2].crc = EXTENDED_BOOT_4;
	roms[3].crc = EXTENDED_BOOT_5;
	for (size_t i = 0; i < roms.size(); i++) {
		if ((roms[i].region == MAINCPU && i != 2 && i != 3) ||
			roms[i].region == POLYGONS || roms[i].region == COPRO_DATA) {
			roms[i].variable = true;
			roms[i].size = roms[i].region == POLYGONS ? 0x200000 : 0x80000;
		}
	}
	return roms;
}();

bool validate_extended(const model1::regions &regions, std::string &error)
{
	constexpr size_t load = 0x278d70 + 10 * 24;
	constexpr const char *names[] = {
		"BIG FOREST", "BAY BRIDGE", "ACROPOLIS", "ALPINE", "AMAZON FALLS",
		"DIABLO CANYON", "METROPOLIS", "PACIFIC COAST", "SURFERS OVAL", "SURFERS ROAD"
	};
	if (regions.maincpu.size() < load + 10 * 64) {
		error = "Virtua Racing Extended: missing course directory.";
		return false;
	}
	for (size_t i = 0; i < std::size(names); i++) {
		const auto *record = regions.maincpu.data() + load + i * 64;
		const size_t length = std::strlen(names[i]);
		if (record[0] < 1 || record[0] > 3 || record[1] > 9 || record[2] > 2 ||
			std::memcmp(record + 28, names[i], length) || record[28 + length] != 0) {
			error = "Virtua Racing Extended: invalid course directory.";
			return false;
		}
	}
	return true;
}

const romset::definition EXTENDED = {
	ID, "vr.zip", "vr", "Virtua Racing Extended v0.3", regions(), EXTENDED_ROMS,
	EXTENDED_BOOT_4, nullptr, true, false, nullptr, "extended", validate_extended
};

} // namespace

const romset::definition ROMSET = {
	ID, "vr.zip", "vr", "Virtua Racing", regions(), ROMS,
	0x6d69e695,   // epr-14878a.4, the main program
	nullptr, true, false, &EXTENDED
};

static const romset::registration REGISTERED{ ROMSET };

} // namespace game::vr
