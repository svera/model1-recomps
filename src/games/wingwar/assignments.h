#pragma once
#include <array>
#include <cstdint>
#include <cstddef>
#include <vector>

class model1;

namespace game::wingwar {

struct assignment
{
	const char *name;
	uint8_t offset, count;
	int first, step;
};

inline constexpr std::array<assignment, 7> ASSIGNMENTS{{
	{"Game difficulty", 0x17, 8, 1, 1},
	{"Expert mode game time", 0x1d, 17, 60, 15},
	{"Dog fight mode life", 0x19, 13, 16, 4},
	{"Expert mode life", 0x1a, 13, 16, 4},
	{"Dog fight mode life (VS)", 0x1b, 13, 16, 4},
	{"Expert mode life (VS)", 0x1c, 13, 16, 4},
	{"Advertise sound", 0x18, 2, 0, 1},
}};

int assignment_value(model1 &machine, size_t index);
bool set_assignments(model1 &machine, const std::array<int, 7> &values);
inline constexpr int FREE_PLAY = 25;
int64_t coin_setting(model1 &machine);
bool valid_coin_setting(int64_t value);
bool set_coin_setting(model1 &machine, int64_t value);
int64_t free_play_coin_setting(int64_t value);
// Returns 0 for standalone, 1 or 2 for a linked cabinet, -1 for no record, or -2 for an invalid role.
int link_role(model1 &machine);
bool set_link_role(model1 &machine, int role);
// The settings the ROM writes on blank saves, with difficulty 1 (its own is 3), as the EEPROM a
// cold boot reads them from; empty if the ROM has no such record.
std::vector<uint8_t> new_eeprom(model1 &machine);

}
