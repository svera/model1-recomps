#pragma once

#include "../game.h"
#include <array>
#include <map>
#include <optional>
#include <set>
#include <utility>
#include <vector>

namespace game::swa {

// A surface cell a widescreen picture adds: layer and band of the list it comes from (so it is
// drawn with that band's light), a coarse (layers 2 and 3) or fine cell, dx and dz from the centre
// cell (fine cells from twice the coarse centre, as the fine layers' lists are)
struct side_cell {
	int layer, band;
	bool coarse;
	int x, z;
	bool operator==(const side_cell &) const = default;
};

struct gameplay_state : game::gameplay_state
{
	bool lod = true;   // the arcade's distance-based models (the host's setting turns it off)
	bool full_distance = false;    // the Death Star's surface drawn past the arcade's last band (Draw distance Full)
	bool wide_cockpit = true;      // widescreen: the cockpit's art continued across the sides (Widescreen cockpit)
	uint64_t nearest = 0;          // detail results replaced by the nearest level's (LOD off)
	uint64_t destroyers_near = 0;  // Star Destroyers drawn with their near model (LOD off)
	bool fleet_stern = false;      // (LOD off) a fleet ship given the full hull: its stern to add
	uint64_t surface_cells = 0;    // surface cells added beyond the arcade's (Draw distance Full)
	uint64_t trench_cells = 0;     // the trench's cells added past the arcade's grid, seen from inside (Full)
	uint64_t bands_lit = 0;        // far bands' light raised (Draw distance Full)
	uint64_t fine_cells = 0;       // coarse surface cells drawn as their four fine cells (LOD off)
	uint64_t coarse_hidden = 0;    // coarse surface models the renderer left out for them (LOD off)
	uint64_t wrapped_copies = 0;   // asteroid-field objects drawn again a period away (Draw distance Full)
	uint64_t culled_copies = 0;    // asteroid-field objects the game culled, drawn where they repeat (Full)
	uint64_t cluster_copies = 0;   // of those, the small rocks' clusters (DB88C) drawn where they repeat (Full)
	bool z_wrap_prefetched = false;   // (Full) renderer Z copies prepared in attract sequence 3, held through its transition
	uint32_t destroyer_manager = 0;
	std::map<uint32_t, uint32_t> asteroid_objects;   // attract ROM placement (asteroid or rock cluster) -> initialized object
	std::set<uint32_t> live_clusters;                // of those, the rock clusters seen with their handler
	uint32_t wrap_matrix = 0;      // the display list's latest object matrix command
	bool destroyers_wrap = false;  // the manager wraps the Star Destroyers this frame (its 77C4 test)
	struct culled_object {
		std::array<float, 3> position;
		std::array<float, 9> rotation;   // in the world, scaled
		std::vector<render_object> models;
		bool cluster = false;            // a small rocks' cluster (DB88C), with its own periods
		uint32_t source = 0;             // what its copies repeat (copy_source)
	};
	// (Full) the display list being written: the asteroid field's copies (view matrix, model), the
	// culled objects not yet placed, the view offset (a drawn object's translation less the camera's
	// rotation of its position) and the stars' matrix command (halfword) they are drawn with
	struct wrap_copies {
		uint32_t buffer = 0, pointer = 0;
		std::vector<std::pair<std::array<float, 12>, render_object>> copies;
		std::vector<culled_object> culled;
		std::set<uint32_t> asteroids;   // objects already drawn or culled in this list
		std::optional<std::array<float, 3>> view_offset;
		uint32_t stars = 0;
		std::array<float, 12> stars_matrix{};
		bool dirty = false;
	} wrap_list;
	uint32_t destroyer_matrix = 0;   // the latest Star Destroyer's own matrix command (C1A1D)
	std::map<uint32_t, std::vector<render_object>> destroyer_models;   // each Star Destroyer's, as last drawn
	std::vector<render_object> last_destroyer_models;
	std::map<uint32_t, model_bounds> copied_bounds;   // (Full) each copied model's, from its vertices
	std::vector<uint32_t> coarse_models;   // the coarse map's models, sorted
	// (Full) the far layer's extra cells, per heading list; the light's direction this pass
	std::map<uint32_t, std::vector<std::pair<int, int>>> far_cells;
	std::array<float, 3> light{};
	bool light_known = false, far_attached = false;
	int view_word = -1;            // the TGP 0x31 (view extents) argument being written, else -1
	// Widescreen: the surface cells the other headings' lists add (side_cells), per list table,
	// heading and Full; the pass being drawn (its entry, next cell to attach, latest layer)
	struct surface_sides {
		std::vector<side_cell> cells;
		std::array<std::map<std::pair<int, int>, int>, 4> band;   // the heading's own cells' bands
		std::vector<std::pair<int, int>> far;                     // (Full) past the widened far layer
	};
	std::map<uint64_t, surface_sides> sides;
	const surface_sides *side_pass = nullptr;
	size_t side_next = 0;
	int side_layer = -1;
	uint64_t side_frame = 0;
	uint64_t side_cells_drawn = 0;   // surface cells added for the widescreen sides
};

using cell_bands = std::vector<std::vector<std::pair<int, int>>>;
// The cells the other headings' lists (nearest first) draw that the heading's own four layers'
// lists don't, each layer's within `reach` of the centre (in its cells), in list order (layer,
// band): fine cells only where no coarse cell stands for them, and of a coarse cell some of whose
// fine cells the heading draws, only its other fine cells.
std::vector<side_cell> side_cells(const std::array<cell_bands, 4> &own, const std::vector<std::array<cell_bands, 4>> &others,
	const std::array<float, 4> &reach);
// The headings either side whose lists cover a picture `ext` columns wider each side than the
// arcade's (the surface's lists are 32 headings apart): 0 for 4:3.
int side_headings(int ext);

gameplay_callbacks create_gameplay(model1 &machine);
void set_lod(model1 &machine, bool enabled);
void set_full_draw_distance(model1 &machine, bool full);
void set_wide_cockpit(model1 &machine, bool wide);
// Widescreen: continues the art at each edge of a layer's middle 496 columns out across its `ext`
// columns each side, every line along its own slope at the edge (pixels as gameplay layer_sides).
// False when there is nothing to continue.
bool continue_sides(uint32_t *pixels, int width, int height, int ext);
// The cells the far layer adds with Full, nearest first: past the reach of its heading's list
// (dx, dz from the camera's cell) out to `radius` cells, within the list's own direction.
std::vector<std::pair<int, int>> far_cells(const std::vector<std::pair<int, int>> &list, int radius,
	const std::set<std::pair<int, int>> *drawn = nullptr, float inner = 0);
// A model's detail result as the V60 reads it at `pc`: with LOD off, the nearest level's for every
// object the game draws; culled objects and other results unchanged.
uint32_t lod_result(bool enabled, uint32_t pc, uint32_t value);
// A Star Destroyer's detail byte as its drawing routine reads it: with LOD off, the near model's
// for every Star Destroyer the game draws.
uint8_t destroyer_detail(bool enabled, uint8_t value);
// Argument `index` of TGP 0x31 (the view test's extents: 2 distances, then right, left, top and
// bottom in screen pixels) as the TGP takes it: a full-width view's right and left (+-248) widened
// by `ext` pixels, so its 0x32 tests pass what the widescreen sides show. Others unchanged.
uint32_t view_extent(int index, uint32_t value, int ext);

// The test menu's GAME ASSIGNMENT: the three difficulties (0-4 for its 1/5 to 5/5) and Advertise
// sound (0 Off, 1 On), in the settings the game keeps in battery-backed RAM and the I/O board's EEPROM.
enum class game_assignment { player_difficulty, enemy_difficulty, timer_difficulty, advertise_sound };
// -1 while the game's settings aren't loaded (or are invalid)
int game_assignment_value(model1 &machine, game_assignment setting);
// Changes it as the test menu's EXIT does: in RAM, with the settings' CRC and backup copy, and in
// the EEPROM the next boot loads them from. False while the settings aren't loaded.
bool set_game_assignment(model1 &machine, game_assignment setting, int value);
// COIN ASSIGNMENT's coin/credit setting (its 5 bytes, first highest): -1 while the settings aren't
// loaded. Setting it (as set_game_assignment) is false when it is already so or can't be set.
int64_t coin_setting(model1 &machine);
bool set_coin_setting(model1 &machine, int64_t value);
// Its first 4 bytes, the table's entries: setting #1 (1 coin 1 credit) and #26 (FREE PLAY)
inline constexpr int64_t COIN_SETTING_1 = 0x01010100, COIN_SETTING_FREE_PLAY = 0x01010101;
inline bool is_free_play(int64_t coin) { return coin >= 0 && coin >> 8 == COIN_SETTING_FREE_PLAY; }
// `coin` with the table entry in place of its first 4 bytes (the last kept)
inline int64_t with_coin_entry(int64_t coin, int64_t entry) { return entry << 8 | (coin & 0xff); }
// The CRC SWA's settings carry (73780): CCITT's polynomial, from DEBD, over `size` bytes then 16 zero bits.
uint16_t settings_crc(const uint8_t *data, size_t size);

}
