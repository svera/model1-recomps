#include "profile.h"
#include "gameplay.h"
#include "../script.h"

namespace game::swa {

std::unique_ptr<auto_play> make_auto_play(const std::string &scenario, uint32_t seed, bool checks);

namespace {

// The 3D is drawn at a steady 30 pictures a second in play, like VR's. Blending between them
// (Frame rate 60 or 120) pairs the parts by their place in the list alone, with no table of the
// ships' records or the camera yet; each carried on by its own move. It starts at 30 (Arcade).
const video_traits VIDEO{ .smooth_frames = true, .separate_parts = true, .default_fps = 30, .stretch_backdrops = false, .still_models = { { 0x1020000, 0x1050000 }, { 0x20000, 0x40000 } }, .camera_anchors = { 0x108126b, 0x1082ac8, 0x1083ebb, 0x1085420, 0x1085d19, 0x108661c, 0x116aebe, 0x116482e } };
const registration REGISTERED{ { &PROFILE, &IO_PROFILE, &VIDEO, nullptr, create_gameplay, make_auto_play } };

}
}
