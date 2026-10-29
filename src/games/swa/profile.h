#pragma once
#include "../game.h"

namespace game::swa {

inline constexpr id ID{ "swa" };
extern const profile PROFILE;
extern const io_profile IO_PROFILE;

// the player 1 input byte (IO_PROFILE), shared by both seats
enum : uint8_t {
	PILOT_LASER = 0x01, PILOT_TORPEDO = 0x02, GUNNER_LASER = 0x04, GUNNER_TORPEDO = 0x08, PILOT_VIEW = 0x10
};
inline constexpr uint8_t COIN = 0x01;    // the system byte (coin 1)
inline constexpr uint8_t START = 0x10;   // the system byte; Start 2 (0x20) isn't read

}
