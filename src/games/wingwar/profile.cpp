#include "profile.h"

namespace game::wingwar {

namespace {
// Constant storage keeps PROFILE ready before cross-file game registration.
constexpr std::array<std::array<uint32_t, 2>, 1> RAM_CODE{ { { 0x40d000, 0x40e000 } } };
}

const profile PROFILE{
	ID, "Wing War", "wingwar", "games/wingwar/wingwar_map.txt",
	"run `make wingwar-regions` with your roms/wingwar.zip, or tools/romset.py --game wingwar",
	std::nullopt, std::nullopt, 0, 8, RAM_CODE
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
// Scenery nodes carry ROM record pointers; moving model writers carry their RAM record in R24.
// Attract fly-bys move the camera up to 12 units a game frame; its shot changes jump 275 or more.
const video_traits VIDEO{ .smooth_frames = true, .separate_parts = true, .camera_cut_distance = 64, .default_fps = 30,
	.stretch_backdrops = false, .centered_scroll_layers = 0x33,
	.centered_scroll_y0 = 80, .centered_scroll_y1 = 304,
	.records = draw_records{ {
		{ 0x02ff79, 21, 0, -6, true }, { 0x02fe99, 21, 0, -6, true },
		{ 0x00394e, 24 }, { 0x003890, 24 }, { 0x002682, 24 }, { 0x002a0c, 24 }
	}, 0x400000, 0x410000, 0 } };

}
