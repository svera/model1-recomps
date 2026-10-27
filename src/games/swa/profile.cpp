#include "profile.h"

namespace game::swa {

const profile PROFILE{
	ID, "Star Wars Arcade", "swa", "games/swa/swa_map.txt",
	"run `make swa-regions` with your roms/swa.zip, or tools/romset.py --game swa",
	// No camera RAM (the boot isn't skipped). For the mode, 7003[R25] (R25 = 408000): 1 from the Test
	// press until the test menu's EXIT, every test included, else 0. (Its scene state 7009[R25] takes
	// the same values in attract as in the test menu.)
	std::nullopt, 0x40f003, 1
};

// As the ROM's input test (and MAME's swa ports): the seats are Pilot and Gunner, both on the player 1
// byte; Start 2 (0x20) is wired in MAME but the game never reads it. The analog ranges are the ones
// the factory calibration takes as +-100 (outside them the test shows INVALID VALUE): sticks 27-227
// centred on 127, the throttle 28 (fast) to 228 (slow) centred on 128. Stick X counts right to left.
const io_profile IO_PROFILE{
	{
		"Coin 1", "Coin 2", "Test", "Service", "Start", "", "", "",
		"Pilot Laser", "Pilot Torpedo", "Gunner Laser", "Gunner Torpedo",
		"Pilot VR Button", "", "", "",
		"", "", "", "", "", "", "", ""
	},
	{ "Pilot Stick X", "Pilot Stick Y", "Pilot Throttle", "", "Gunner Stick X", "Gunner Stick Y", "", "" },
	{ "SWA_STICK1X", "SWA_STICK1Y", "SWA_THROTTLE", "", "SWA_STICK2X", "SWA_STICK2Y", "", "" },
	{ 127, 127, 128, 0xff, 127, 127, 0xff, 0xff },
	{ host_axis::left_x, host_axis::left_y, host_axis::right_y, host_axis::none,
	  host_axis::left_x, host_axis::left_y, host_axis::none, host_axis::none },
	{ analog_kind::centered, analog_kind::centered, analog_kind::centered, analog_kind::centered,
	  analog_kind::centered, analog_kind::centered, analog_kind::centered, analog_kind::centered },
	{ { { 27, 227, true }, { 27, 227, false }, { 28, 228, false }, {},
		{ 27, 227, true }, { 27, 227, false }, {}, {} } },
	{ 0, 0, 0, 0, 1, 1, 0, 0 }
};

}
