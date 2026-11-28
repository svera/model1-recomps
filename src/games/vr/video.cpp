// Virtua Racing's data the shared video code works from (game::video_traits).
#include "gameplay.h"

namespace game::vr {

constexpr game::matrix_writer animated_scenery_writer(uint32_t caller)
{
	return { 0xff8500, 25, caller, 0, false, false };
}

// The 3D blended between its 30 game frames a second, each part paired by what the game says it
// is (a car's wheels change model as they turn and the trackside reuses its models along the
// track, so pairing parts by place in the list put tyres between wheels). Its routine at FF8500
// writes a matrix command for a car's part (body, livery, wheels) with the car's record in R19,
// FF85D8 the pit crew's with theirs in R25. The animated-scenery callers of FF8500 also use R25:
// R19 still holds the last car checked for a collision, not the palm being drawn. Car records
// lie at 400D00 on, 0x280 apart; non-RAM owners are the still world (the track, R19 pointing in ROM; the SEGA logo too, which is drawn
// without the camera: blend_by_records tells it by its place). The camera it drew with is at 501B00. No scenery split for the shadow fix, no hi-res skies.
const video_traits VIDEO{ .smooth_frames = true,
	.pin_hud_edges = true,
	.foreground_objects = { 0x1031c7 },   // the screen-space SEGA logo, also in close-up car views
	.right_edge_objects = { 0x1031c7 },
	.edge_views = { { -232, 134, -1 }, { 149, 12, 1 } },   // position pointer; circuit map and its cars
	.edge_viewports = { { 405, 59, 1 } },   // Live Monitor's commentator window
	.hud_tile_groups = { { 0x1180, 0x11bc } },   // position gauge, beside the large GO! tiles
	.wide_tile_slides = { { 0x07, 1, 480 }, { 0x1c, 1, 480 } },   // linked-play notice and results
	// FFE3C6/FFE444 average these whole-pixel scrolls on even video frames; odd frames
	// use the current pair directly. Keep the half-pixels lost by the tile-register writes.
	.sky_scroll = sky_camera{ 0x501408, 0x50140a, 0x501418, 0x50141a, 0x500501, sky_camera::format::tile_pair, 0x501420 },
	.records = draw_records{ {
		animated_scenery_writer(0xffab97), animated_scenery_writer(0xffb72c),
		animated_scenery_writer(0xffb254),
		animated_scenery_writer(0xffc0a1),
		animated_scenery_writer(0xffd19c),
		animated_scenery_writer(0xffcec9),
		animated_scenery_writer(0xffb81c), animated_scenery_writer(0xffb859),
		animated_scenery_writer(0xffc235), animated_scenery_writer(0xffc251),
		animated_scenery_writer(0xffd809), animated_scenery_writer(0xffd85a),
		animated_scenery_writer(0xffd9e8), animated_scenery_writer(0xffda21), animated_scenery_writer(0xffda67),
		animated_scenery_writer(0xffdb49), animated_scenery_writer(0xffdb9a), animated_scenery_writer(0xffdbd5),
		animated_scenery_writer(0xffddd4), animated_scenery_writer(0xffddeb),
		animated_scenery_writer(0xffde6e), animated_scenery_writer(0xffdedc), animated_scenery_writer(0xffdf42),
		{ 0xff8500, 19 }, { 0xff85d8, 25, 0, 0, false, false }
	}, 0x400000, 0x410000, 0x501b00 } };

}
