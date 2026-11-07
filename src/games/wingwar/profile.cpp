#include "profile.h"

namespace game::wingwar {

const profile PROFILE{
	ID, "Wing War", "wingwar", "games/wingwar/wingwar_map.txt",
	"run `make wingwar-regions` with your roms/wingwar.zip, or tools/romset.py --game wingwar",
	std::nullopt, std::nullopt, 0
};

const io_profile IO_PROFILE{
	{
		"Coin 1", "", "Test", "Service", "Start", "View 1", "View 2", "View 3",
		"View 4", "", "", "", "Machine Gun", "Missile", "Smoke", "",
		"", "", "", "", "", "", "", ""
	},
	{ "Stick X", "Stick Y", "Throttle", "", "", "", "", "" },
	{ "WINGWAR_STICKX", "WINGWAR_STICKY", "WINGWAR_THROTTLE", "", "", "", "", "" },
	{ 0x80, 0x80, 0x01, 0xff, 0xff, 0xff, 0xff, 0xff },
	{ host_axis::left_x, host_axis::left_y, host_axis::right_trigger,
	  host_axis::none, host_axis::none, host_axis::none, host_axis::none, host_axis::none },
	{ analog_kind::centered, analog_kind::centered, analog_kind::pedal, analog_kind::centered,
	  analog_kind::centered, analog_kind::centered, analog_kind::centered, analog_kind::centered },
	{ { { 0, 255, true }, { 0, 255, false }, { 1, 255, false }, {}, {}, {}, {}, {} } },
	{ 0, 0, 0, 0, 0, 0, 0, 0 },
	false
};

// The line-scrolled aircraft panel shares maps 0 and 2 with the stationary top/bottom artwork.
const video_traits VIDEO{ .smooth_frames = true, .separate_parts = true, .default_fps = 30,
	.stretch_backdrops = false, .centered_scroll_layers = 0x33,
	.centered_scroll_y0 = 80, .centered_scroll_y1 = 304 };

}
