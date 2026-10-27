#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <istream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class model1;

namespace game {

// A game, by its short name ("vf", "vr"): the regions folder build/<name>, its assets/games/<name>.
// Each game's folder (src/games/<name>/) defines its own and registers its definition (below).
struct id
{
	std::string_view name;
	friend constexpr bool operator==(id, id) = default;
	explicit operator bool() const { return !name.empty(); }
};
enum class host_axis : int8_t { none = -1, left_x, left_y, right_x, right_y, left_trigger, right_trigger };
enum class analog_kind : uint8_t { centered, pedal };

struct profile
{
	id title;
	std::string_view display_name;
	std::string_view romset;
	std::string_view recompiler_map;
	std::string_view make_regions;   // how a development build gets the regions (src/host/regions.h)
	// RAM the shared code reads: the camera pointer the game last sent (an offset in work RAM,
	// 0x500000 on; 0 between scenes, and until the boot's over), and the address of the game's mode
	// byte (in work RAM or the battery-backed 0x400000 on), test_menu_mode in its test menu. Unset
	// until the game's RAM layout is known.
	std::optional<uint32_t> camera_ram;
	std::optional<uint32_t> mode_ram;
	uint8_t test_menu_mode;
	// The V60's average cycles per instruction (the cores charge MAME's estimate of 8 each).
	double cpu_cycles_per_instruction = 8;
};

struct io_profile
{
	// Active-low input bytes, ordered [system, player 1, player 2], with bit 0 first.
	std::array<std::string_view, 24> digital_controls;
	// ADC channels 0-7; the I/O board's secondary switch selects channels 4-7.
	std::array<std::string_view, 8> analog_controls;
	std::array<std::string_view, 8> analog_environment;   // raw port values (tools/model1run)
	std::array<uint8_t, 8> analog_neutral;                 // raw port value at rest
	std::array<host_axis, 8> host_axis_defaults;
	std::array<analog_kind, 8> analog_kinds;
	// The host reads a channel as 0 (stick left/forward, key `low`) to 255 (right/back, key `high`),
	// then maps it onto the cabinet's calibrated range, reversed where the hardware counts the other way.
	struct analog_range { uint8_t low = 0, high = 255; bool reverse = false; };
	std::array<analog_range, 8> analog_ranges{};
	std::array<uint8_t, 8> analog_player{};                // whose pad and keys drive the channel (0, 1)

	uint8_t analog_port(size_t channel, uint8_t host) const
	{
		const analog_range &r = analog_ranges[channel];
		const int value = r.low + (host * (r.high - r.low) + 127) / 255;
		return uint8_t(r.reverse ? r.low + r.high - value : value);
	}
	uint8_t analog_host(size_t channel, uint8_t port) const
	{
		const analog_range &r = analog_ranges[channel];
		const int span = r.high - r.low;
		if (span <= 0) return 0;
		int value = std::clamp(int(port), int(r.low), int(r.high));
		if (r.reverse) value = r.low + r.high - value;
		return uint8_t(((value - r.low) * 255 + span / 2) / span);
	}
};

struct tgp_hle_callbacks
{
	std::function<void()> fifo_push;
	std::function<void()> reset;
};

struct gameplay_state
{
	virtual ~gameplay_state() = default;
};

// Optional per-object effects, in model coordinates and BGR555 colour components.
// A negative polygon result omits it; nonnegative values are passed to colour.
struct object_effects
{
	bool hidden = false;
	int lightmode = -1;
	std::function<void(float &, float &, float &)> point;
	std::function<void(float &, float &, float &)> normal;
	std::function<int(int)> polygon;
	// Remap a texture word's material before palette lookup, retaining its lighting flags.
	std::function<uint16_t(uint16_t)> material;
	std::function<void(uint32_t, int, int &, int &, int &)> colour;
};

struct model_bounds
{
	std::array<float, 3> center, extent;
	float radius = 0;
};

struct render_object
{
	uint32_t texture, polygon, size;
	uint32_t owner = 0;
	std::optional<std::array<float, 12>> transform;
	float fade = 1;                                // its colours scaled toward black (1: as lit)
	std::optional<model_bounds> bounds;             // optional model-space bound for renderer-only copies
	// (smooth motion) nonzero: a copy with a transform that pairs with the nearest one with the same
	// key in the older list's paired slot, its transform blended with that one's
	uint64_t key = 0;
};

struct record_visibility
{
	uint32_t owner;
	std::array<float, 3> position;
	bool culled;
};

struct gameplay_callbacks
{
	std::shared_ptr<gameplay_state> state;
	std::function<void()> reset;
	std::function<void()> frame_begin;
	std::function<void()> frame_end;
	// An optional aligned work-RAM word to observe after the CPU writes it (mask: bytes written).
	uint32_t work_ram_watch = 0;
	std::function<void(uint16_t)> work_ram_written;
	// Optional adjustment of a completed TGP result as the V60 reads it.
	std::function<uint32_t(uint32_t, uint32_t)> copro_result;
	// Optional adjustment of a word the V60 writes to the TGP's input FIFO (PC, value).
	std::function<uint32_t(uint32_t, uint32_t)> copro_write;
	// Optional adjustment of 32-bit reads in an inclusive address range (PC, address, value).
	// A zero end observes only read32_watch.
	uint32_t read32_watch = 0;
	uint32_t read32_watch_end = 0;
	std::function<uint32_t(uint32_t, uint32_t, uint32_t)> read32_value;
	// Optional adjustment of the 8- and 16-bit reads by instructions in [read_pc_first, read_pc_last]
	// (PC, address, value). These include the instructions' own bytes from the program ROM.
	uint32_t read_pc_first = 0, read_pc_last = 0;
	std::function<uint16_t(uint32_t, uint32_t, uint16_t)> read_value;
	// Extra renderer-only objects attached to a matrix as the CPU writes it. The argument is the
	// display-list halfword written; the callback may set it to an earlier matrix command of the
	// same list, whose extras the result then replaces (an empty result removes them).
	std::function<std::vector<render_object>(uint32_t &)> matrix_objects;
	// Snapshot active records when a relevant matrix is written, before visibility advances.
	std::function<std::vector<game::record_visibility>(uint32_t)> record_visibility;
	std::function<void()> render_begin;
	std::function<void()> render_end;
	// Owner is the recorded RAM record, 1 for the static world, or 0 when unknown.
	std::function<object_effects(uint32_t, uint32_t, uint32_t, uint32_t, const float *)> object;
	// Diagnostic outlines instead of filled polygons; nullopt omits an object.
	std::function<std::optional<uint32_t>(uint32_t, uint32_t)> wireframe;
	std::function<std::optional<uint16_t>(uint32_t)> backdrop_tile;
	std::function<std::vector<uint32_t>(std::string_view)> screen_hashes;
	std::function<std::optional<std::string_view>(uint32_t)> screen_name;
	std::function<bool(uint32_t, bool)> wide_scene;
	// Widescreen: a tile layer (non-opaque) as drawn this game frame, `ext` columns each side of the
	// arcade's 496 (`width` = 496 + 2 * ext, pixels 0x00rrggbb, 0xff000000 for nothing). The game may
	// fill the sides, which are otherwise empty; true when it did.
	std::function<bool(int layer, uint32_t *pixels, int width, int height, int ext)> layer_sides;
};

// What the shared video code (src/machine) needs to know about a game's own data: none of it is
// the board's, each is where one game keeps something in its ROM or RAM.

// A stage sky's panorama record in the program ROM (see model1_sky.cpp). `top`: a one-row record
// the game streams over the panorama's top row (0: none).
struct sky_record
{
	std::string_view name;
	uint32_t rom;
	uint32_t top = 0;
};

// The RAM the game's sky-scroll routine reads (byte addresses in RAM B, see model1_interp.cpp):
// the camera's heading (1/32 pixel) and height, the last game frame's, and the byte whose bit 0
// marks the video frame between two game frames. tile_pair instead holds whole-pixel scrolls:
// bit 0 selects the current pair, otherwise their half-sum (with the current pair's flags).
struct sky_camera
{
	uint32_t heading, height, last_heading, last_height, between;
	enum class format { camera, tile_pair };
	format layout = format::camera;
	uint32_t control = 0;            // tile_pair: bit 2 disables the sky layer on direct frames
};

// A texture block the game leaves unset (all FFFF) that the cabinet shows in one palette pen,
// for the polygons in [poly_first, poly_last] only.
struct unset_texture_pen
{
	uint32_t poly_first, poly_last, texture;
	uint16_t pen;
};

// What the game itself says each part of its 3D is, read as it writes its display list (see
// blend_by_records): each instruction writing a matrix command and the register then holding the
// record of the thing drawn (records lie in [records_from, records_to); any other value: the still
// world), and where the camera is in RAM B as the list is written (12 floats: the rotation taking
// the world to the view, by columns, then the view's translation, like a list's matrix).
struct matrix_writer
{
	uint32_t pc;
	int record_register;
	uint32_t caller = 0;             // optional return address at SP; first matching writer wins
};

struct draw_records
{
	std::vector<matrix_writer> writers;
	uint32_t records_from, records_to;
	uint32_t camera;
};

struct hud_view
{
	float x, y;
	int edge;                       // -1: left, +1: right
};

struct hud_tile_range
{
	uint16_t first, last;
};

struct video_traits
{
	bool smooth_frames = false;      // the 3D blended between game frames (a 30 Hz game's)
	bool separate_parts = false;     // the parts are separate objects (ships, shots), not a fighter's limbs
	int default_fps = 120;           // the Frame rate setting's first-run choice (30: the arcade's, no blending)
	bool pin_hud_edges = false;      // move off-center, non-full-screen HUD graphics into widescreen sides
	// widescreen: the opaque tile layers (the backdrop) stretched across the width; else drawn 1:1,
	// the sides of each the colour of its edges where those are one colour, black where not
	bool stretch_backdrops = true;
	std::vector<uint32_t> foreground_objects;   // screen-space 3D overlays, sorted after world geometry
	std::vector<uint32_t> right_edge_objects;   // whole screen-space models anchored to the right edge
	std::vector<hud_view> edge_views;           // HUD projection translations, separate from the world camera
	std::vector<hud_tile_range> hud_tile_groups; // artwork kept separate from nearby centered prompts
	// polygon ROM addresses below this are the scenery a moving model's shadow falls on, which
	// above 1x has its shadows kept off the models (0: no such split, shadows drawn as the arcade)
	uint32_t scenery_polys_end = 0;
	std::vector<sky_record> skies;    // empty: no hi-res skies
	int sky_tile_map = 0, sky_first_row = 0;   // where the game streams the panoramas
	std::optional<sky_camera> sky_scroll;      // none: the sky's scroll as the tile chip has it
	std::vector<unset_texture_pen> unset_texture_pens;
	std::optional<draw_records> records;   // none: the parts matched from the lists alone
};

class auto_play;
struct macro;

// What a game gives the shared code. Each game's folder has one (src/games/<name>/game.cpp) and
// registers it when the program starts, so a binary has the games it links and nothing here
// names them.
struct definition
{
	const profile *info = nullptr;
	const io_profile *io = nullptr;
	const video_traits *video = nullptr;
	tgp_hle_callbacks (*tgp_hle)(model1 &) = nullptr;           // none: the TGP runs its own program
	gameplay_callbacks (*gameplay)(model1 &) = nullptr;
	// scripted play (src/games/script.h): nullptr for a scenario the game hasn't
	std::unique_ptr<auto_play> (*play)(const std::string &scenario, uint32_t seed, bool checks) = nullptr;
	bool (*macros)(const std::string &cmd, std::istream &args, macro &out) = nullptr;   // nullptr: none
};

// in a game's game.cpp: static const game::registration REGISTERED{ { &PROFILE, ... } };
struct registration
{
	explicit registration(const definition &d);
};

// the game's definition; an unknown game ends the program with a message
const definition &get_definition(id title);
// the games in this binary, by name
std::vector<id> registered();

tgp_hle_callbacks make_tgp_hle(id title, model1 &machine);
gameplay_callbacks make_gameplay(id title, model1 &machine);
const video_traits &get_video(id title);

const profile &get(id title);
const io_profile &get_io_profile(id title);
// by its short name ("vf", "vr"), among the games in this binary
std::optional<id> find(std::string_view short_name);

}
