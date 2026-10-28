// Star Wars Arcade's model detail. Three drawing routines (the ships and the larger scenery) ask
// the TGP (command 0x22) for an object's screen-size measure, drop the object above 0x6FFF, and
// otherwise pick one of its five models from the byte table at 23FE6E indexed by the measure / 32:
// level 0 below 0x180, 1 from 0x180, 2 from 0x2A0, 3 from 0x460, 4 from 0x700. The model entry
// is the type's list plus level * 8. Nothing else reads the measure, and the choice is not kept in
// RAM, so a smaller measure changes only the model in the display list.
//
// Star Destroyers choose by rank instead: 0DBFF6 sorts them by distance and stores each one's level
// in 3A[object] from the byte table at DC0C2 (the nearest -1: the near model with its separate
// parts; the next two 0, the rest 2; 7F: not drawn). Ranks up to 2 also join a list at 6D80 that the
// game uses, so the stored byte stays; only the drawing routine's read of it, at 0DC336, changes.
// The routine then uses level 2 for every Star Destroyer while the game's mode byte 7009[R25] is 2
// (the attract mode's scenes, including the start of its X-wing demonstration); its read at 0DC341
// reads another mode. The routine leaves the level in R17 for DCE13, which runs the near Star
// Destroyer's turret timers (and their shots) only for -1. So for one drawn near only by this
// setting, the turrets' enable mask read at 0DCE68 reads all set, the path the game takes for a
// disabled turret: no RAM written.
//
// The Star Destroyers of the fleet 0D6540 draws (the attract mode's, and 0EEC62's) pick their
// model from the table at D6E12 by two TGP measures and store it in the object (2[R20]: the model,
// 6[R20]: its data) for C1869 to draw. Only C1869's reads of those fields change, from either entry
// to the hull of the one ship D6540 draws in full, so the object keeps what the game stored; that
// ship's stern is added to the matrix command as an extra.
//
// Draw distance. The Death Star's surface (EAA2D) is four layers of square cells around the camera:
// two from 32 x 32 maps, then two of cells twice the size from a 16 x 16 map (B40AE), each map
// repeating. Each layer draws a fixed list of cells (dx, dz from the camera's cell, in bands, nearest
// first) chosen by the heading 1A[R20] and the height; the farthest layer's lists end about 9 of its
// 4,096-unit cells out, where the surface ends against space. After each band the light vector
// is scaled by the layer's table at 99BE0 + 0x40 * layer, which falls to 0 in the far bands: the
// distant buildings take only the ambient light, the same flat grey as the ground. A cell is a
// map entry (a halfword offset to its record): type 0 (a model: texture, polygon) or type 3 (an
// angle about Y, then the model), drawn with the camera's matrix translated to (x << R6, 0, z << R6).
// Full adds the cells past the far layer's list, within its direction, to the matrix of one of its
// type-0 cells, in the renderer only, and keeps every band's light at least 0.45 (the far layer's
// fifth band). Neither changes what the V60 runs or writes outside the display list.
// The lists span only the 4:3 view; in a wide frame the cells the other headings' lists draw that
// this heading's don't (side_cells) are added the same way, band by band, with bounds so the
// renderer culls those outside the wide view. Full takes one heading more each side (at 4:3 too):
// one list's far ring leaves the horizon's corners empty, the gaps jumping as the heading steps.
//
// The coarse layers are the surface's own distant detail: each coarse cell's model is a simpler
// version of the 2 x 2 fine cells it covers (fine cells 2x, 2x + 1 by 2z, 2z + 1; its centre is half
// a fine cell past theirs, both maps' models centred on their cell). With LOD off the renderer draws
// those four fine models at each coarse cell's matrix instead of its own.
//
// The asteroid field repeats every 131,072 units (X, and Z while 77C4 bit 15 is set). The game
// keeps each asteroid and Star Destroyer in the period around the camera and culls those out of
// view (TGP 0x32), so one leaving behind reappears ahead in a single frame, and a Star Destroyer
// (far bigger than its test's radius) appears at once at a screen edge. Full draws every object
// within 2 periods, where it is and where it repeats, fading in from black beyond 1.5: drawn
// objects' models copied from their matrix, culled ones from their own fields (see
// wrapped_copies), in the renderer only. The small rocks' clusters (DB88C) are a denser field of
// their own: each repeats every 32,768 units in X and 65,536 in Z (while the mode is below 4), so
// the game's one copy pops in 16,384 units to a side; Full draws their repeats within 65,536 units,
// fading in beyond 49,152. The game also keeps each within 4,096 units of the camera's height
// (8,192 apart), moving it from above to below at once; copies stay at the game's height (stacked
// ones would fill the sky with columns) but one within 2,048 of that edge is also drawn where it
// moves to, fading in, so that the move shows no change.
#include "gameplay.h"
#include "../../machine/model1.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <optional>

namespace game::swa {

namespace {
// the `in.w [FP], R0` reading each routine's measure
constexpr uint32_t LOD_READS[] = { 0x0cdfd8, 0x0d1d38, 0x0e7fd3 };
constexpr uint32_t CULLED_ABOVE = 0x6fff;
constexpr uint32_t DESTROYER_DETAIL_READ = 0x0dc336;   // movz.bw 3A[R20], R17
constexpr uint32_t ALL_FAR_READ = 0x0dc341;            // cmp.b #2, 7009[R25]
constexpr uint32_t TURRET_MASK_READ = 0x0dce68;        // and.h 44[R20], R0
constexpr uint32_t MODE = 0x7009;                      // in the game's variables (R25)
constexpr uint8_t MODE_ALL_FAR = 2;
// An occasional attract loop's asteroid demo ends on the music credits: the 3D squeezed into a
// panel on the left, the song list scrolling on the right of the same tile maps the game also
// draws opaque (which widescreen stretches, so the text showed twice). This flag (variables
// 0x7783) is set from the credits until the next demo starts.
constexpr uint8_t MUSIC_CREDITS_SEQUENCE = 3, MUSIC_CREDITS_SUBSEQUENCE = 3;
constexpr uint32_t MUSIC_CREDITS_FLAG = 0x7783;
// The cockpit: tile layer 3, map 1's tiles (its only use), drawn from these characters; it touches
// both edges of the screen with the canopy's struts and the side consoles.
constexpr int COCKPIT_LAYER = 3, COCKPIT_MAP = 1;
constexpr uint16_t COCKPIT_FIRST = 0x2000, COCKPIT_END = 0x2300;
constexpr int COCKPIT_EDGE_TILES = 4;    // at least this many down each edge
constexpr uint8_t Z_WRAP_PREFETCH_SEQUENCE = 3, Z_WRAP_PREFETCH_SUBSEQUENCE = 2;
constexpr uint32_t ATTRACT_DEMO = 0x7770;   // C3828 alternates demos 1 (asteroid field) and 2 (Death Star)
constexpr uint8_t ASTEROID_FIELD_DEMO = 1;
constexpr int VARIABLES_REG = 25;
constexpr uint32_t FLEET_DATA_READ = 0x0c18aa;    // mov.w 6[R20], [R11+]
constexpr uint32_t FLEET_MODEL_READ = 0x0c18af;   // mov.w [2[R20]], [R11+]
constexpr uint32_t FLEET_MODEL = 2, FLEET_DATA = 6;   // in the drawn object
constexpr uint32_t FLEET_FAR_MODEL = 0x23c01a, FLEET_FAR_DATA = 0x53698;
constexpr uint32_t FLEET_NEAR_MODEL = 0x23c016, FLEET_NEAR_DATA = 0x5360f;
// the one ship D6540 draws in full (D6630): its hull, and its stern drawn first
constexpr uint32_t FLEET_HULL_MODEL = 0x23c01e, FLEET_HULL_DATA = 0x536ad;
constexpr uint32_t FLEET_STERN_MODEL = 0x23c022, FLEET_STERN_DATA = 0x53777;
constexpr uint32_t FLEET_MATRIX_WRITE = 0x0c18c1;   // C1869's mov.w #B, -44[R11]
constexpr uint32_t OBJECTS_FIRST = 0x400000, OBJECTS_LAST = 0x40fffc;   // RAM A
constexpr uint32_t DETAIL = 0x3a, TURRET_MASK = 0x44;  // in a Star Destroyer's object
constexpr uint8_t DESTROYER_HIDDEN = 0x7f, DESTROYER_NEAR = 0xff;
constexpr int OBJECT_REG = 20;   // R20: the Star Destroyer's object
// the surface (EAA2D)
constexpr uint32_t SURFACE_MATRIX_WRITE = 0x0eadae;   // mov.w #B, [R11+]: a cell's matrix command
constexpr uint32_t SURFACE_LIGHT_READS[] = { 0x0eae2d, 0x0eae31, 0x0eae35 };   // a band's light vector
constexpr uint32_t BAND_LIGHTS = 0x99be0, COARSE_BAND_LIGHTS = 0x99c60, FAR_BAND_LIGHTS = 0x99ca0, BAND_LIGHTS_END = 0x99ce0;
// the fine layers' map (both fine layers draw the same models), and the coarse layers'
constexpr uint32_t FINE_MAP = 0x0b0962, COARSE_MAP = 0x0b40ae;
constexpr uint32_t FINE_MASK = 0x1f, COARSE_MASK = 0xf;
constexpr int FINE_ROW_SHIFT = 5, COARSE_ROW_SHIFT = 4;
constexpr uint32_t POLYGON_ADDRESS = 0x7fffff;   // as the renderer takes a model's address
constexpr float FULL_LIGHT = 0.45f;
constexpr int FULL_RADIUS = 18;      // cells of the far layer: twice the arcade's
constexpr int MASK_REG = 4, SHIFT_REG = 6, ROW_SHIFT_REG = 7, MAP_REG = 10, CENTER_X_REG = 13, CENTER_Z_REG = 14;
constexpr uint32_t LAYER_LIGHTS_SIZE = 0x40;   // each layer's band lights in the tables at 99BE0
constexpr int COARSE_SHIFT = 12;     // the coarse layers' cells: 4,096 units, the fine ones' half
// a surface pass's layers in later frames than its first only when the V60 runs late
constexpr uint64_t SIDE_PASS_FRAMES = 4;
constexpr int LAYER_REG = 18;        // the layer's list table, + 4
constexpr int LIGHTS_REG = 28;       // the band's light scale in its layer's table
constexpr uint32_t HEADING = 0x1a;   // in the surface's object (R20)
constexpr uint16_t CELL_MODEL = 0, CELL_TURNED = 3;
// the asteroid field's repeating objects
constexpr uint32_t MATRIX_WRITES[] = { 0x0c18c1, 0x0c191b, 0x0c1995, 0x0c1a12, 0x0c1ae6 };   // mov.w #B, -44[R11]
constexpr uint32_t BARE_MATRIX_WRITE = 0x0c1a63;                  // mov.w #B, -34[R11]: no object yet
constexpr uint32_t OBJECT_APPENDS[] = { 0x0c19b4, 0x0c1a88 };    // mov.w #1, -10[R11]: one more object
constexpr uint32_t MATRIX_SIZE = 0x34, OBJECT_SIZE = 0x10, MATRIX_FLOATS = 4;
constexpr uint32_t OBJECT_COMMAND = 1;
constexpr uint32_t DESTROYER_MANAGER_INIT = 0x0dbe87;   // mov.h [R4+], 30[R20]
constexpr uint32_t DESTROYER_MANAGER_HANDLER = 0x0dbedc;
constexpr uint32_t ATTRACT_DESTROYERS = 0x0df2ec;   // DBE82's mask, count, then handler/initializer/XYZ records
constexpr uint32_t ATTRACT_ASTEROIDS = 0x0df38a, ATTRACT_ASTEROIDS_END = 0x0df7b2;
constexpr uint32_t ASTEROID_RECORD_SIZE = 28;
constexpr uint32_t ASTEROID_INIT_READS[] = { 0x0db1dd, 0x0db46d };   // last angular velocity, 26 bytes into the placement
constexpr uint32_t WRAP_FLAG_READ = 0x0dbee0;   // and.h 77C4[R25], R0 (R0: the manager's 30[R20])
constexpr uint32_t WRAP_FLAGS = 0x77c4, CAMERA_X = 0x7724, CAMERA_Z = 0x7728, CAMERA_ROTATION = 0x7570;
constexpr uint32_t CAMERA_Y = 0x7704;   // the eye's, a float (7700-7708)
// 77C4: bit 15 folds Z into the camera's period too. C3B05 sets 1 before 8001 (C3C51/C38C4);
// prime the renderer's Z copies during sequence 3/subsequence 2 so ships are already present.
constexpr uint16_t WRAP_Z = 0x8000, WRAP_Z_NEXT = 0x0001;
constexpr uint8_t DESTROYERS_WRAP_BELOW = 4;    // the mode 7009 below which DBEDC wraps them
constexpr uint32_t HANDLER = 0x24, POSITION_X = 0xc, POSITION_Y = 0x10, POSITION_Z = 0x14;   // in an object
constexpr uint32_t ASTEROID_HANDLERS[] = { 0x0db200, 0x0db502 };
constexpr uint32_t DESTROYER_HANDLER = 0x0dd612;
// DB88C: a cluster of small rocks, placed from its halfwords' differences from the camera's
constexpr uint32_t CLUSTER_HANDLER = 0x0db88c;
constexpr uint32_t CLUSTER_CULL_READ = 0x0db927;   // its TGP 0x32 view test (radius 2560)
constexpr uint32_t CLUSTER_ROLL = 0x1c;
constexpr float CLUSTER_SCALE = 2.5f;              // 40200000
constexpr float CLUSTER_PERIOD_X = 32768, CLUSTER_PERIOD_Y = 8192, CLUSTER_PERIOD_Z = 65536;
constexpr float CLUSTER_HANDOVER = 2048;   // the copy the game moves it to fades in over this much height
constexpr uint8_t CLUSTERS_WRAP_Z_BELOW = 4;       // the mode 7009 below which DB88C places Z
constexpr float CLUSTER_REACH = 65536;
constexpr uint32_t CLUSTER_X = 0x30, CLUSTER_Y = 0x32;   // DB833: the placement's X and Y (and Z at 34) low halfwords
// DBE82's cluster placements after the asteroids': handler, initializer (DB833), then X, Y, Z
constexpr uint32_t ATTRACT_CLUSTERS = 0x0df7b2, ATTRACT_CLUSTERS_END = 0x0df816, CLUSTER_RECORD_SIZE = 20;
constexpr uint32_t CLUSTER_INIT_READ = 0x0db85b;   // mov.w [R4+], R0: Z, 16 bytes into the placement
constexpr uint32_t CLUSTER_MODEL = 0x23beba, CLUSTER_DATA = 0x5222b;
constexpr uint32_t ASTEROID_MATRIX_WRITE = 0x0c18c1;   // C1869's
constexpr uint32_t STARS_MODEL = 0x1f46a4;            // drawn with the camera's rotation alone
constexpr uint32_t ASTEROID_MODEL = 2, ASTEROID_DATA = 6, ASTEROID_SCALE = 0x38, ASTEROID_YAW = 0x40, ASTEROID_ROLL = 0x42;
constexpr float DESTROYER_SCALE = 2.48f;   // 401EB852, DC382; Star Destroyers keep the camera's rotation here
// the `in.w [FP], R0` reading TGP 0x32's view test (negative: out of view): asteroids' (R20), the manager's per Star Destroyer (R18)
constexpr uint32_t ASTEROID_CULL_READS[] = { 0x0db278, 0x0db3c6, 0x0db574, 0x0db672 };
constexpr uint32_t DESTROYER_CULL_READ = 0x0dbf96;
constexpr int MANAGED_REG = 18;
constexpr uint32_t LIST_POINTER = 0x7068, LIST_BUFFER = 0xffff0000;
// copies are drawn within WRAP_REACH of the camera, fading in from black beyond WRAP_FADE
constexpr float WRAP_PERIOD = 131072, WRAP_REACH = 262144, WRAP_FADE = 196608;
// TGP 0x31: the view test's extents, as 0D7BC0, C2ADA and CADDD send them (50, 50, 248, -248, 192,
// -192: the 3D view's half width, the screen's) before 0x32's tests; 0D7B85's narrow (+-20) view
// is a test of its own and stays.
constexpr uint32_t VIEW_EXTENTS = 0x31;
constexpr int VIEW_RIGHT = 2, VIEW_LEFT = 3, VIEW_ARGUMENTS = 6;
constexpr float VIEW_HALF_WIDTH = 248;
constexpr float VIEW_SLOPE = 1.24f;      // the half-width's slope: 248 pixels at the zoom of 200
constexpr int SURFACE_HEADINGS = 32;     // the surface's lists per layer, 11.25 degrees apart
constexpr uint32_t SURFACE_TABLE_SIZE = 4 * SURFACE_HEADINGS;   // one layer's; the four follow each other
constexpr int LIST_REG = 11;
}

// the far layer's cells past `list`'s reach, out to `radius` across its spread; given `drawn`, also
// those from `inner` out that no list draws, where merged lists reach unevenly
std::vector<std::pair<int, int>> far_cells(const std::vector<std::pair<int, int>> &list, int radius,
	const std::set<std::pair<int, int>> *drawn, float inner)
{
	float reach = 0, sx = 0, sz = 0;
	for (auto [x, z] : list) {
		const float r = std::hypot(float(x), float(z));
		reach = std::max(reach, r);
		if (r >= 2) sx += x / r, sz += z / r;
	}
	std::vector<std::pair<int, int>> cells;
	if (sx == 0 && sz == 0) return cells;
	const float facing = std::atan2(sz, sx);
	auto off = [&](int x, int z) { return std::fabs(std::remainder(std::atan2(float(z), float(x)) - facing, 6.2831853f)); };
	float spread = 0;
	for (auto [x, z] : list)
		if (std::hypot(float(x), float(z)) >= 2) spread = std::max(spread, off(x, z));
	for (int z = -radius; z <= radius; z++)
		for (int x = -radius; x <= radius; x++) {
			const float r = std::hypot(float(x), float(z));
			const bool past = r > reach || (drawn && r > inner && !drawn->count({ x, z }));
			if (past && r <= radius && off(x, z) <= spread + std::asin(std::min(1.0f, 0.7f / r))) cells.emplace_back(x, z);
		}
	std::stable_sort(cells.begin(), cells.end(), [](auto a, auto b) { return a.first * a.first + a.second * a.second < b.first * b.first + b.second * b.second; });
	return cells;
}

std::vector<side_cell> side_cells(const std::array<cell_bands, 4> &own, const std::vector<std::array<cell_bands, 4>> &others,
	const std::array<float, 4> &reach)
{
	using cell = std::pair<int, int>;
	std::set<cell> own_fine, own_coarse;
	for (int layer = 0; layer < 4; layer++)
		for (const auto &band : own[layer])
			for (const cell c : band) (layer < 2 ? own_fine : own_coarse).insert(c);
	// each neighbour cell the heading lacks, with the layer and band it first comes in
	std::map<cell, std::pair<int, int>> fine, coarse;
	for (const auto &lists : others)
		for (int layer = 0; layer < 4; layer++)
			for (int band = 0; band < int(lists[layer].size()); band++)
				for (const cell c : lists[layer][band]) {
					auto &to = layer < 2 ? fine : coarse;
					if (std::hypot(float(c.first), float(c.second)) <= reach[layer] && !(layer < 2 ? own_fine : own_coarse).count(c))
						to.emplace(c, std::pair{ layer, band });
				}
	const auto coarse_of = [](cell f) { return cell{ f.first >> 1, f.second >> 1 }; };
	std::set<cell> covered = own_coarse;   // coarse cells drawn whole
	std::vector<side_cell> cells;
	std::set<cell> split;                  // fine cells drawn for a coarse cell the heading half draws
	for (const auto &[c, at] : coarse) {
		bool partial = false;
		for (int j = 0; j < 2; j++)
			for (int i = 0; i < 2; i++) partial |= own_fine.count({ 2 * c.first + i, 2 * c.second + j }) > 0;
		if (!partial) {
			covered.insert(c);
			cells.push_back({ at.first, at.second, true, c.first, c.second });
			continue;
		}
		for (int j = 0; j < 2; j++)
			for (int i = 0; i < 2; i++) {
				const cell f{ 2 * c.first + i, 2 * c.second + j };
				if (!own_fine.count(f) && split.insert(f).second) cells.push_back({ at.first, at.second, false, f.first, f.second });
			}
	}
	for (const auto &[f, at] : fine)
		if (!covered.count(coarse_of(f)) && !split.count(f)) cells.push_back({ at.first, at.second, false, f.first, f.second });
	std::stable_sort(cells.begin(), cells.end(), [](const side_cell &a, const side_cell &b) { return std::pair{ a.layer, a.band } < std::pair{ b.layer, b.band }; });
	return cells;
}

int side_headings(int ext)
{
	if (ext <= 0) return 0;
	// the arcade's half-width at its zoom (the view cone's 1.24), and its widescreen one
	const float extra = std::atan(VIEW_SLOPE * (VIEW_HALF_WIDTH + float(ext)) / VIEW_HALF_WIDTH) - std::atan(VIEW_SLOPE);
	return int(std::ceil(extra / (6.2831853f / SURFACE_HEADINGS)));
}

uint32_t lod_result(bool enabled, uint32_t pc, uint32_t value)
{
	if (enabled || value > CULLED_ABOVE) return value;
	for (uint32_t read : LOD_READS)
		if (pc == read) return 0;
	return value;
}

uint8_t destroyer_detail(bool enabled, uint8_t value)
{
	return enabled || value == DESTROYER_HIDDEN ? value : DESTROYER_NEAR;
}

uint32_t view_extent(int index, uint32_t value, int ext)
{
	if (ext <= 0 || (index != VIEW_RIGHT && index != VIEW_LEFT)) return value;
	const float x = std::bit_cast<float>(value);
	if (std::fabs(x) != VIEW_HALF_WIDTH) return value;
	return std::bit_cast<uint32_t>(std::copysign(VIEW_HALF_WIDTH + float(ext), x));
}

void set_lod(model1 &machine, bool enabled)
{
	std::static_pointer_cast<gameplay_state>(machine.game_state())->lod = enabled;
}

void set_full_draw_distance(model1 &machine, bool full)
{
	auto state = std::static_pointer_cast<gameplay_state>(machine.game_state());
	state->full_distance = full;
	if (!full) {
		state->z_wrap_prefetched = false;
		state->wrap_list = {};
	}
}

void set_wide_cockpit(model1 &machine, bool wide)
{
	std::static_pointer_cast<gameplay_state>(machine.game_state())->wide_cockpit = wide;
}

namespace {
// the cockpit is on show: map 1 enabled, with cockpit characters down both edges of the screen
bool cockpit_shown(const model1 &machine)
{
	const uint16_t hscroll = machine.tile_word(0x5000 + COCKPIT_MAP), vscroll = machine.tile_word(0x5004 + COCKPIT_MAP);
	if (vscroll & 0x8000) return false;
	int left = 0, right = 0;
	for (int y = 4; y < 384; y += 8) {
		const int h = -(hscroll & 0x8000 ? machine.tile_word(0x4000 + 0x200 * COCKPIT_MAP + y) : hscroll);
		const int my = (y + vscroll) & 0x1ff;
		for (int x : { 0, 495 }) {
			const int mx = (x + h) & 0x1ff;
			const uint16_t code = machine.tile_word(0x1000 * COCKPIT_MAP + (my / 8) * 64 + mx / 8) & 0x3fff;
			if (code >= COCKPIT_FIRST && code < COCKPIT_END) (x ? right : left)++;
		}
	}
	return left >= COCKPIT_EDGE_TILES && right >= COCKPIT_EDGE_TILES;
}
}

bool continue_sides(uint32_t *pixels, int width, int height, int ext)
{
	constexpr uint32_t NOTHING = 0xff000000;
	constexpr int SCREEN = 496, REACH = 12, MAX_SHIFT = 18, WINDOW = 2;
	if (ext <= 0 || width != SCREEN + 2 * ext || height <= 0) return false;
	bool any = false;
	std::vector<float> slope(static_cast<size_t>(height)), y_at(static_cast<size_t>(height));
	std::vector<uint32_t> edge(static_cast<size_t>(height)), column(static_cast<size_t>(height));
	for (int dir : { -1, +1 }) {
		// column j in from this side's edge (0: the edge)
		const int x_edge = dir < 0 ? ext : ext + SCREEN - 1;
		auto at = [&](int j, int y) { return pixels[size_t(y) * width + x_edge - dir * j]; };
		for (int y = 0; y < height; y++) edge[y] = at(0, y);
		// Each row's slope where a line crosses the edge: the vertical shift REACH columns in that
		// matches the edge around the row best; rows without a clear line (an even colour) take
		// the slope interpolated between the nearest rows with one.
		std::vector<std::pair<int, float>> known;
		for (int y = 0; y < height; y++) {
			bool varied = false;
			for (int k = -WINDOW; k <= WINDOW; k++)
				if (y + k >= 0 && y + k < height && edge[y + k] != edge[y]) varied = true;
			if (!varied) continue;
			int best = INT32_MAX, sum = 0, ties = 0;
			for (int t = -MAX_SHIFT; t <= MAX_SHIFT; t++) {
				int cost = 0;
				for (int k = -WINDOW; k <= WINDOW; k++) {
					const int a = y + k, b = a + t;
					if (a < 0 || a >= height) continue;
					cost += b < 0 || b >= height || at(REACH, b) != edge[a];
				}
				if (cost < best) best = cost, sum = t, ties = 1;
				else if (cost == best) sum += t, ties++;
			}
			if (best <= 1 && ties <= 2) known.emplace_back(y, float(sum) / float(ties * REACH));
		}
		if (known.empty()) continue;
		for (size_t i = 0, k = 0; i < size_t(height); i++) {
			const int y = int(i);
			while (k + 1 < known.size() && known[k + 1].first <= y) k++;
			if (y <= known.front().first) slope[i] = known.front().second;
			else if (k + 1 >= known.size()) slope[i] = known.back().second;
			else {
				const auto [y0, s0] = known[k];
				const auto [y1, s1] = known[k + 1];
				slope[i] = s0 + (s1 - s0) * float(y - y0) / float(y1 - y0);
			}
		}
		// d columns out, the edge's row r is at r - slope * d: each row painted down to where the next is
		for (int d = 1; d <= ext; d++) {
			std::fill(column.begin(), column.end(), NOTHING);
			for (int r = 0; r < height; r++) y_at[r] = float(r) - slope[r] * float(d);
			for (int r = 0; r < height; r++) {
				const float a = y_at[r], b = r + 1 < height ? y_at[r + 1] : y_at[r] + 1;
				const int lo = int(std::floor(std::min(a, b) + 0.5f)), hi = int(std::floor(std::max(a, b) + 0.5f));
				for (int y = std::max(lo, 0); y < std::min(std::max(hi, lo + 1), height); y++) column[y] = edge[r];
			}
			const int x = x_edge + dir * d;
			for (int y = 0; y < height; y++) pixels[size_t(y) * width + x] = column[y];
		}
		any = true;
	}
	return any;
}

namespace {
// (Full) a band's light vector component as the V60 reads it: at least FULL_LIGHT of the light's
// direction, which the first bands (scale 1) give
uint32_t band_light(model1 &machine, gameplay_state &state, int component, uint32_t value)
{
	const uint32_t entry = machine.cpu().reg(LIGHTS_REG) - 4;
	if (entry < BAND_LIGHTS || entry >= BAND_LIGHTS_END) return value;
	const float scale = std::bit_cast<float>(machine.read32(entry));
	if (scale >= FULL_LIGHT) {
		state.light[component] = std::bit_cast<float>(value) / scale;
		if (component == 2) state.light_known = true;
		return value;
	}
	if (!state.light_known) return value;
	if (component == 2) state.bands_lit++;
	return std::bit_cast<uint32_t>(state.light[component] * FULL_LIGHT);
}

struct cell_model { uint32_t texture, polygon; float angle; };

// a map cell's model: type 0, or type 3 turned by its angle; nothing for another type
std::optional<cell_model> map_cell(model1 &machine, uint32_t map, uint32_t mask, int row_shift, int x, int z)
{
	const uint32_t entry = map + 2 * (((uint32_t(z) & mask) << row_shift) + (uint32_t(x) & mask));
	uint32_t at = entry + machine.read16(entry);
	const uint16_t type = machine.read16(at);
	float angle = 0;
	if (type == CELL_TURNED) {
		angle = machine.read16(at + 2) * (6.2831853f / 65536);
		at += 2;
	} else if (type != CELL_MODEL)
		return std::nullopt;
	return cell_model{ machine.read32(at + 2), machine.read32(at + 6), angle };
}

model_bounds bounds_for(model1 &machine, gameplay_state &state, uint32_t polygon);

// a model at `offset` from a parent cell's centre turned by `parent`, in that cell's matrix
render_object placed(const cell_model &model, float parent, float x, float z)
{
	const float pc = std::cos(parent), ps = std::sin(parent);
	const float c = std::cos(model.angle - parent), s = std::sin(model.angle - parent);
	return { model.texture, model.polygon, 0, 0,
		std::array<float, 12>{ c, 0, -s, 0, 1, 0, s, 0, c, pc * x - ps * z, 0, ps * x + pc * z } };
}

// a coarse cell (x, z) at `offset` from the parent cell: its own model, or with LOD off the four
// fine models it stands for
void add_coarse_cell(model1 &machine, gameplay_state &state, std::vector<render_object> &objects, const cell_model &coarse,
	int x, int z, int shift, float parent, float offset_x, float offset_z)
{
	if (state.lod) {
		objects.push_back(placed(coarse, parent, offset_x, offset_z));
		return;
	}
	const float fine = float(1 << (shift - 1)), half = fine / 2;
	for (int j = 0; j < 2; j++)
		for (int i = 0; i < 2; i++)
			if (const auto model = map_cell(machine, FINE_MAP, FINE_MASK, FINE_ROW_SHIFT, 2 * x + i, 2 * z + j))
				objects.push_back(placed(*model, parent, offset_x - half + fine * i, offset_z - half + fine * j));
	state.fine_cells++;
}

// at a coarse layer's cell matrix: with LOD off its fine cells, and (Full) the cells past the far
// layer's list, attached to its first type-0 cell; in a wide frame (or Full) the cells the
// neighbouring headings show (side_cells), each band's attached to the first type-0 cell drawn in
// it or after
std::vector<render_object> surface_cells(model1 &machine, gameplay_state &state)
{
	auto reg = [&](int r) { return machine.cpu().reg(r); };
	const uint32_t lights = reg(LIGHTS_REG);
	if (lights < BAND_LIGHTS || lights >= BAND_LIGHTS_END) return {};
	const int layer = int((lights - BAND_LIGHTS) / LAYER_LIGHTS_SIZE);
	const int shift = int(reg(SHIFT_REG)), row_shift = int(reg(ROW_SHIFT_REG));
	const uint32_t mask = reg(MASK_REG), map = reg(MAP_REG);
	const int px = int32_t(reg(0)) >> shift, pz = int32_t(reg(1)) >> shift;
	const int cx = int32_t(reg(CENTER_X_REG)), cz = int32_t(reg(CENTER_Z_REG));
	const auto own = map_cell(machine, map, mask, row_shift, px, pz);
	std::vector<render_object> objects;
	const auto heading_list = [&](uint32_t table, int heading) {
		return machine.read32(table + 4 * uint32_t(heading & (SURFACE_HEADINGS - 1)));
	};
	const auto read_list = [&](uint32_t at) {
		cell_bands bands(machine.read8(at++));
		for (auto &band : bands)
			for (int n = machine.read8(at++); n > 0; n--, at += 2) band.emplace_back(int8_t(machine.read8(at)), int8_t(machine.read8(at + 1)));
		return bands;
	};
	// (Full) one heading more each side: the far ring past one list's spread leaves the horizon's
	// corners empty, and the gaps jump as the heading steps
	const int steps = side_headings(machine.wide_ext()) + (state.full_distance ? 1 : 0);
	const bool neighbours = steps > 0;
	const uint32_t table = machine.read32(reg(LAYER_REG) - 4);
	const int heading = machine.read16(reg(OBJECT_REG) + HEADING) & (SURFACE_HEADINGS - 1);
	if (neighbours && (state.side_layer < 0 || layer < state.side_layer || machine.frame_number() > state.side_frame + SIDE_PASS_FRAMES)) {
		// a new pass: its four layers' tables follow each other
		const uint32_t first = table - SURFACE_TABLE_SIZE * uint32_t(layer);
		const uint64_t key = uint64_t(first) << 16 | uint64_t(heading) << 8 | uint64_t(steps) << 1 | uint64_t(state.full_distance);
		auto entry = state.sides.find(key);
		if (entry == state.sides.end()) {
			const auto lists = [&](int h) {
				std::array<cell_bands, 4> l;
				for (int i = 0; i < 4; i++) l[i] = read_list(heading_list(first + SURFACE_TABLE_SIZE * uint32_t(i), h));
				return l;
			};
			gameplay_state::surface_sides sides;
			const auto mine = lists(heading);
			std::vector<std::array<cell_bands, 4>> others;   // nearest first
			for (int d = 1; d <= SURFACE_HEADINGS / 2; d++) {
				others.push_back(lists(heading + d));
				if (d < SURFACE_HEADINGS / 2) others.push_back(lists(heading - d));
			}
			// each layer out to its own list's reach; (Full) the far layer to the reach of those
			// covering the picture's width, past which far_cells continues
			std::array<float, 4> reach{};
			const auto extend = [&](int i, const cell_bands &bands) {
				for (const auto &band : bands)
					for (auto [x, z] : band) reach[i] = std::max(reach[i], std::hypot(float(x), float(z)));
			};
			for (int i = 0; i < 4; i++) extend(i, mine[i]);
			std::vector<std::pair<int, int>> far;
			const auto list_reach = [](const cell_bands &bands) {
				float r = 0;
				for (const auto &band : bands)
					for (auto [x, z] : band) r = std::max(r, std::hypot(float(x), float(z)));
				return r;
			};
			float inner = list_reach(mine[3]);
			for (const auto &band : mine[3]) far.insert(far.end(), band.begin(), band.end());
			for (int n = 0; n < 2 * steps && n < int(others.size()); n++) {
				if (state.full_distance) extend(3, others[n][3]);
				inner = std::min(inner, list_reach(others[n][3]));
				for (const auto &band : others[n][3]) far.insert(far.end(), band.begin(), band.end());
			}
			sides.cells = side_cells(mine, others, reach);
			// the coarse cells the lists and side cells draw: the far ring fills the rest
			std::set<std::pair<int, int>> drawn;
			for (int i = 2; i < 4; i++) {
				for (const auto &band : mine[i]) drawn.insert(band.begin(), band.end());
				for (const auto &lists : others)
					for (const auto &band : lists[i])
						for (auto c : band)
							if (std::hypot(float(c.first), float(c.second)) <= reach[i]) drawn.insert(c);
			}
			for (int i = 0; i < 4; i++)
				for (int b = 0; b < int(mine[i].size()); b++)
					for (auto c : mine[i][b]) sides.band[i].emplace(c, b);
			sides.far = far_cells(far, FULL_RADIUS, &drawn, inner);
			entry = state.sides.emplace(key, std::move(sides)).first;
		}
		state.side_pass = &entry->second;
		state.side_next = 0;
		state.side_frame = machine.frame_number();
	}
	if (!neighbours) state.side_pass = nullptr;
	state.side_layer = layer;
	if (const auto *sides = state.side_pass; sides && own && own->angle == 0) {
		const auto band = sides->band[layer].find({ px - cx, pz - cz });
		if (band != sides->band[layer].end()) {
			// the centre cell in each kind's units: the fine layers' is twice the coarse layers'
			const int coarse_x = layer < 2 ? cx >> 1 : cx, coarse_z = layer < 2 ? cz >> 1 : cz;
			for (; state.side_next < sides->cells.size(); state.side_next++) {
				const side_cell &c = sides->cells[state.side_next];
				// the far layer's all come at its first cell: the game may draw none in a later band
				if (layer < 3 && std::pair{ c.layer, c.band } > std::pair{ layer, band->second }) break;
				const int cell_shift = c.coarse ? COARSE_SHIFT : COARSE_SHIFT - 1;
				const int x = (c.coarse ? coarse_x : 2 * coarse_x) + c.x, z = (c.coarse ? coarse_z : 2 * coarse_z) + c.z;
				const float ox = float(x * (1 << cell_shift) - px * (1 << shift)), oz = float(z * (1 << cell_shift) - pz * (1 << shift));
				const size_t from = objects.size();
				if (c.coarse) {
					if (const auto model = map_cell(machine, COARSE_MAP, COARSE_MASK, COARSE_ROW_SHIFT, x, z))
						add_coarse_cell(machine, state, objects, *model, x, z, COARSE_SHIFT, 0, ox, oz);
				} else if (const auto model = map_cell(machine, FINE_MAP, FINE_MASK, FINE_ROW_SHIFT, x, z))
					objects.push_back(placed(*model, 0, ox, oz));
				// most are out of view: the renderer leaves those out by their bounds
				for (size_t i = from; i < objects.size(); i++) objects[i].bounds = bounds_for(machine, state, objects[i].polygon);
				state.side_cells_drawn++;
			}
		}
	}
	if (lights < COARSE_BAND_LIGHTS) return objects;
	if (lights < FAR_BAND_LIGHTS) state.far_attached = false;
	if (!state.lod && own) add_coarse_cell(machine, state, objects, *own, px, pz, shift, own->angle, 0, 0);
	if (!state.full_distance || lights < FAR_BAND_LIGHTS || state.far_attached || !own || own->angle != 0) return objects;
	state.far_attached = true;
	const std::vector<std::pair<int, int>> *far = state.side_pass ? &state.side_pass->far : nullptr;
	if (!far) {
		const uint32_t list = heading_list(table, heading);
		auto cached = state.far_cells.find(list);
		if (cached == state.far_cells.end()) {
			std::vector<std::pair<int, int>> cells;
			for (const auto &band : read_list(list)) cells.insert(cells.end(), band.begin(), band.end());
			cached = state.far_cells.emplace(list, far_cells(cells, FULL_RADIUS)).first;
		}
		far = &cached->second;
	}
	for (auto [dx, dz] : *far) {
		const int x = cx + dx, z = cz + dz;
		if (const auto model = map_cell(machine, map, mask, row_shift, x, z)) {
			add_coarse_cell(machine, state, objects, *model, x, z, shift, 0, float((x - px) << shift), float((z - pz) << shift));
			state.surface_cells++;
		}
	}
	return objects;
}

// the coarse layers' own models, hidden with LOD off where their fine cells are drawn instead
bool coarse_model(model1 &machine, gameplay_state &state, uint32_t polygon)
{
	if (state.coarse_models.empty()) {
		for (int z = 0; z <= int(COARSE_MASK); z++)
			for (int x = 0; x <= int(COARSE_MASK); x++)
				if (const auto model = map_cell(machine, COARSE_MAP, COARSE_MASK, COARSE_ROW_SHIFT, x, z))
					state.coarse_models.push_back(model->polygon & POLYGON_ADDRESS);
		std::sort(state.coarse_models.begin(), state.coarse_models.end());
	}
	return std::binary_search(state.coarse_models.begin(), state.coarse_models.end(), polygon);
}

// m[3c + r] is row r, column c, as in a display-list matrix
using mat3 = std::array<float, 9>;
mat3 multiply(const mat3 &a, const mat3 &b)
{
	mat3 m{};
	for (int c = 0; c < 3; c++)
		for (int r = 0; r < 3; r++) m[3 * c + r] = a[r] * b[3 * c] + a[3 + r] * b[3 * c + 1] + a[6 + r] * b[3 * c + 2];
	return m;
}

std::optional<mat3> inverse(const mat3 &m)
{
	const float det = m[0] * (m[4] * m[8] - m[7] * m[5]) - m[3] * (m[1] * m[8] - m[7] * m[2]) + m[6] * (m[1] * m[5] - m[4] * m[2]);
	if (!std::isfinite(det) || std::fabs(det) < 1e-12f) return std::nullopt;
	return mat3{ (m[4] * m[8] - m[5] * m[7]) / det, (m[2] * m[7] - m[1] * m[8]) / det, (m[1] * m[5] - m[2] * m[4]) / det,
		(m[5] * m[6] - m[3] * m[8]) / det, (m[0] * m[8] - m[2] * m[6]) / det, (m[2] * m[3] - m[0] * m[5]) / det,
		(m[3] * m[7] - m[4] * m[6]) / det, (m[1] * m[6] - m[0] * m[7]) / det, (m[0] * m[4] - m[1] * m[3]) / det };
}

std::array<float, 3> apply(const mat3 &m, std::array<float, 3> v)
{
	return { m[0] * v[0] + m[3] * v[1] + m[6] * v[2], m[1] * v[0] + m[4] * v[1] + m[7] * v[2], m[2] * v[0] + m[5] * v[1] + m[8] * v[2] };
}

float read_float(model1 &machine, uint32_t at) { return std::bit_cast<float>(machine.read32(at)); }

std::array<float, 3> position(model1 &machine, uint32_t object)
{
	return { read_float(machine, object + POSITION_X), read_float(machine, object + POSITION_Y), read_float(machine, object + POSITION_Z) };
}

// a repeating field: its period on each axis (0: none), how far from the camera copies are drawn and
// (handover, 0: none) the period in Y within which the game keeps an object around the camera's height
struct wrap_field {
	std::array<float, 3> period;
	float reach;
	float handover = 0;
};

// a place an object repeats at, and its copy's fade
struct wrap_offset {
	std::array<float, 3> d;
	float fade = 1;
};

wrap_field asteroid_field(model1 &machine, const gameplay_state &state)
{
	const uint32_t vars = machine.cpu().reg(VARIABLES_REG);
	const bool wrap_z = state.z_wrap_prefetched || (machine.read16(vars + WRAP_FLAGS) & (WRAP_Z | WRAP_Z_NEXT));
	return { { WRAP_PERIOD, 0, wrap_z ? WRAP_PERIOD : 0 }, WRAP_REACH };
}

wrap_field cluster_field(model1 &machine)
{
	const bool wrap_z = machine.read8(machine.cpu().reg(VARIABLES_REG) + MODE) < CLUSTERS_WRAP_Z_BELOW;
	return { { CLUSTER_PERIOD_X, 0, wrap_z ? CLUSTER_PERIOD_Z : 0 }, CLUSTER_REACH, CLUSTER_PERIOD_Y };
}

// the world's periodic offsets of an object at `at` within the field's reach of the camera (or `center`); its own place too with `own`.
// With a handover, an object within CLUSTER_HANDOVER of the edge of its band around the camera's height is also
// drawn where the game moves it at the edge, fading in, so that neither copy appears at once.
std::vector<wrap_offset> wrap_offsets(model1 &machine, const std::array<float, 3> &at, bool own, const wrap_field &field,
	const std::optional<std::array<float, 3>> &center = std::nullopt)
{
	const uint32_t vars = machine.cpu().reg(VARIABLES_REG);
	const std::array<float, 3> camera = center ? *center : std::array<float, 3>{ float(int32_t(machine.read32(vars + CAMERA_X))),
		read_float(machine, vars + CAMERA_Y), float(int32_t(machine.read32(vars + CAMERA_Z))) };
	const std::array<float, 3> o{ at[0] - camera[0], at[1] - camera[1], at[2] - camera[2] };
	// (before the game folds them, objects can be periods away from the camera)
	std::array<int, 3> first{}, last{};
	for (int a = 0; a < 3; a++)
		if (field.period[a]) {
			first[a] = int(std::ceil((-field.reach - o[a]) / field.period[a]));
			last[a] = int(std::floor((field.reach - o[a]) / field.period[a]));
		}
	std::vector<std::pair<float, float>> heights{ { 0.0f, 1.0f } };   // (Y offset, fade)
	if (field.handover) {
		const float into = std::fabs(o[1]) - (field.handover / 2 - CLUSTER_HANDOVER);
		if (into > 0) heights.push_back({ o[1] > 0 ? -field.handover : field.handover, std::min(1.0f, into / CLUSTER_HANDOVER) });
	}
	std::vector<wrap_offset> offsets;
	for (const auto &[dy, fade] : heights)
		for (int k = first[2]; k <= last[2]; k++)
			for (int j = first[1]; j <= last[1]; j++)
				for (int i = first[0]; i <= last[0]; i++) {
					const std::array<float, 3> d{ i * field.period[0], j * field.period[1] + dy, k * field.period[2] };
					if ((own || i || j || k || dy) && std::hypot(o[0] + d[0], o[1] + d[1], o[2] + d[2]) <= field.reach)
						offsets.push_back({ d, fade });
				}
	return offsets;
}

mat3 camera_rotation(model1 &machine)
{
	mat3 k;
	const uint32_t vars = machine.cpu().reg(VARIABLES_REG);
	for (int i = 0; i < 9; i++) k[i] = read_float(machine, vars + CAMERA_ROTATION + 4 * i);
	return k;
}

// The intact ship's ROM records, before any ship has run its drawing routine.
std::vector<render_object> intact_destroyer(model1 &machine, bool lod, uint8_t animation)
{
	std::vector<render_object> models;
	const auto add = [&](uint32_t record) {
		models.push_back({ machine.read32(record + 4), machine.read32(record), 0, 0, std::nullopt });
	};
	if (lod) {
		for (uint32_t table : { 0x9a63cu, 0x9aa7cu, 0x9aa9cu, 0x9b0fcu }) add(table + 2 * 4);
		add(0xdc8c0 + (animation & 2) * 4);
		for (uint32_t table : { 0x9b5dcu, 0x9acccu }) add(table + 2 * 4);
	} else {
		for (uint32_t record : {
			0x9b584u, 0x9acf4u, 0x9b09cu, 0x9a65cu, 0x9a764u, 0x9a86cu, 0x9a974u,
			0x9b374u, 0x9aabcu, 0x9b37cu, 0x9abc4u,
			0x9b0a4u, 0x9b0acu, 0x9b0ccu, 0x9b0dcu, 0x9b0d4u }) add(record);
		add(0xdc8c0 + (animation & 2) * 4);
		for (uint32_t record : {
			0x9b59cu, 0x9b5b4u, 0x9b5bcu, 0x9b5d4u, 0x9b5c4u, 0x9b5ccu,
			0x9ad04u, 0x9ad24u, 0x9ad0cu, 0x9ad1cu, 0x9ad3cu, 0x9ad44u, 0x9ad2cu }) add(record);
	}
	return models;
}

// the object at R20 repeats with the world: an asteroid, or a Star Destroyer while the manager wraps them
bool repeats(model1 &machine, gameplay_state &state, uint32_t object)
{
	const uint32_t handler = machine.read32(object + HANDLER);
	if (std::find(std::begin(ASTEROID_HANDLERS), std::end(ASTEROID_HANDLERS), handler) != std::end(ASTEROID_HANDLERS) ||
		handler == CLUSTER_HANDLER) return true;
	return handler == DESTROYER_HANDLER && (state.destroyers_wrap || state.z_wrap_prefetched) &&
		machine.read8(machine.cpu().reg(VARIABLES_REG) + MODE) < DESTROYERS_WRAP_BELOW;
}

// the models drawn with a matrix command, from its first object up to the list's end
std::vector<render_object> matrix_models(model1 &machine, uint32_t matrix, uint32_t end)
{
	std::vector<render_object> objects;
	for (uint32_t at = matrix + MATRIX_SIZE; at + OBJECT_SIZE <= end && machine.read32(at) == OBJECT_COMMAND; at += OBJECT_SIZE)
		objects.push_back({ machine.read32(at + 4), machine.read32(at + 8), machine.read32(at + 12), 0, std::nullopt });
	return objects;
}

// a new display list started: the asteroid field's copies so far belong to the last one
void begin_list(gameplay_state &state, uint32_t pointer)
{
	auto &list = state.wrap_list;
	if ((pointer & LIST_BUFFER) == list.buffer && pointer >= list.pointer) {
		list.pointer = pointer;
		return;
	}
	const auto view_offset = list.view_offset;
	list = {};
	list.buffer = pointer & LIST_BUFFER;
	list.pointer = pointer;
	list.view_offset = view_offset;   // (the last list's until one of this list's objects gives it)
}

// Tight component bounds, plus the origin-centred radius used when constructing copies.
model_bounds bounds_for(model1 &machine, gameplay_state &state, uint32_t polygon)
{
	polygon &= 0xffffff;
	if (auto it = state.copied_bounds.find(polygon); it != state.copied_bounds.end()) return it->second;
	const bool ram = polygon & 0x800000;
	const float *data = machine.polygon_data(ram);
	const size_t words = ram ? 0x400000 : machine.polygon_rom().size() / sizeof(float);
	size_t at = polygon & 0x7fffff;
	float radius = 0;
	std::array<float, 3> lo{ INFINITY, INFINITY, INFINITY }, hi{ -INFINITY, -INFINITY, -INFINITY };
	auto reach = [&](size_t v) {
		radius = std::max(radius, std::sqrt(data[v] * data[v] + data[v + 1] * data[v + 1] + data[v + 2] * data[v + 2]));
		for (int i = 0; i < 3; i++) {
			lo[i] = std::min(lo[i], data[v + i]);
			hi[i] = std::max(hi[i], data[v + i]);
		}
	};
	const bool vertices = at + 6 <= words;
	if (vertices) {
		reach(at);
		reach(at + 3);
		for (at += 6; at + 10 <= words; at += 10) {
			uint32_t flags;
			std::memcpy(&flags, data + at, 4);
			if (!(flags & 3)) break;
			reach(at + 4);
			reach(at + 7);
		}
	}
	model_bounds bounds{};
	bounds.radius = radius;
	if (vertices)
		for (int i = 0; i < 3; i++) {
			bounds.center[i] = (hi[i] + lo[i]) * 0.5f;
			bounds.extent[i] = (hi[i] - lo[i]) * 0.5f;
		}
	return state.copied_bounds[polygon] = bounds;
}

// What a copy repeats, the same from its ROM placement on (smooth motion pairs a copy with the
// older list's of the same source and model): an attract asteroid's or cluster's placement, a Star
// Destroyer's (the manager's in placement order), else the object.
uint32_t copy_source(model1 &machine, const gameplay_state &state, uint32_t object)
{
	for (const auto &[record, placed] : state.asteroid_objects)
		if (placed == object) return record;
	if (state.destroyer_manager && machine.read32(state.destroyer_manager + HANDLER) == DESTROYER_MANAGER_HANDLER)
		for (uint32_t i = 0, count = std::min<uint32_t>(machine.read16(state.destroyer_manager + 0x32), machine.read16(ATTRACT_DESTROYERS + 2)); i < count; i++)
			if ((OBJECTS_FIRST | machine.read16(state.destroyer_manager + 0x36 + 2 * i)) == object) return ATTRACT_DESTROYERS + 4 + i * 20;
	return object;
}

// a model drawn with view matrix `view` (as the display list's: rotation, then translation), unless
// it is beyond `reach` or wide of the view (generously: wider pictures, smooth motion's turns)
bool add_copy(model1 &machine, gameplay_state &state, const std::array<float, 12> &view, const render_object &model, float reach, uint32_t source)
{
	const float x = view[9], y = view[10], z = view[11];
	const float distance = std::sqrt(x * x + y * y + z * z);
	if (distance >= reach) return false;
	float scale = 0;
	for (int c = 0; c < 3; c++)
		scale = std::max(scale, std::sqrt(view[c * 3] * view[c * 3] + view[c * 3 + 1] * view[c * 3 + 1] + view[c * 3 + 2] * view[c * 3 + 2]));
	const auto bounds = bounds_for(machine, state, model.polygon);
	const float radius = scale * bounds.radius;
	const float wide = 1.5f * (VIEW_HALF_WIDTH + float(machine.wide_ext())) / VIEW_HALF_WIDTH;
	if (z < -radius || std::fabs(x) - wide * z > radius * std::sqrt(1 + wide * wide) ||
		std::fabs(y) - z > radius * std::sqrt(2.0f)) return false;
	render_object faded = model;
	faded.bounds = bounds;
	faded.fade *= std::min(1.0f, (reach - distance) / (reach * (1 - WRAP_FADE / WRAP_REACH)));
	faded.key = uint64_t(source) << 24 | (model.polygon & 0xffffff);
	state.wrap_list.copies.push_back({ view, faded });
	state.wrap_list.dirty = true;
	return true;
}

// copies of the models at a view matrix moved by the world's periodic offsets (camera rotation k)
void add_copies(model1 &machine, gameplay_state &state, const std::array<float, 12> &view, const std::vector<render_object> &models, const mat3 &k,
	const std::vector<wrap_offset> &offsets, uint32_t source, float reach = WRAP_REACH)
{
	for (const auto &offset : offsets) {
		const auto d = apply(k, offset.d);
		auto moved = view;
		for (int r = 0; r < 3; r++) moved[9 + r] += d[r];
		for (auto model : models) {
			model.fade *= offset.fade;
			if (add_copy(machine, state, moved, model, reach, source)) state.wrapped_copies++;
		}
	}
}

// The cockpit is visible before DBE82 creates the fleet. Start with its ROM placements,
// then use the initialized objects until the normal drawing path supplies its own copies.
void prefetch_destroyers(model1 &machine, gameplay_state &state)
{
	if (!state.z_wrap_prefetched || state.destroyers_wrap) return;
	const uint32_t vars = machine.cpu().reg(VARIABLES_REG);
	const mat3 k = camera_rotation(machine);
	const auto eye = apply(k, { read_float(machine, vars + 0x7700), read_float(machine, vars + 0x7704), read_float(machine, vars + 0x7708) });
	const auto draw = [&](const std::array<float, 3> &here, uint8_t animation, uint32_t source) {
		const auto at = apply(k, here);
		std::array<float, 12> view;
		for (int j = 0; j < 9; j++) view[j] = k[j] * DESTROYER_SCALE;
		for (int j = 0; j < 3; j++) view[9 + j] = at[j] - eye[j];
		add_copies(machine, state, view, intact_destroyer(machine, state.lod, animation), k, wrap_offsets(machine, here, true, asteroid_field(machine, state)), source);
	};
	if (state.destroyer_manager && machine.read32(state.destroyer_manager + HANDLER) == DESTROYER_MANAGER_HANDLER) {
		for (uint32_t i = 0, count = machine.read16(state.destroyer_manager + 0x32); i < count; i++) {
			const uint32_t object = OBJECTS_FIRST | machine.read16(state.destroyer_manager + 0x36 + 2 * i);
			draw(position(machine, object), machine.read8(object + 0x3b), copy_source(machine, state, object));
		}
	} else {
		for (uint32_t i = 0, count = machine.read16(ATTRACT_DESTROYERS + 2); i < count; i++) {
			const uint32_t record = ATTRACT_DESTROYERS + 4 + i * 20;
			draw({ float(int32_t(machine.read32(record + 8))),
				float(int32_t(machine.read32(record + 12)) + int32_t(machine.read32(vars + 0x773a))),
				float(int32_t(machine.read32(record + 16))) }, 0, record);
		}
	}
}

mat3 asteroid_rotation(float scale, uint16_t yaw, uint16_t roll)
{
	// TGP 0x15 (Y), then 0x16 (Z); both turn clockwise.
	const float y = -yaw * (6.2831853f / 65536), z = -roll * (6.2831853f / 65536);
	const float cy = std::cos(y), sy = std::sin(y), cz = std::cos(z), sz = std::sin(z);
	mat3 rotation = multiply({ cy, 0, -sy, 0, 1, 0, sy, 0, cy }, { cz, sz, 0, -sz, cz, 0, 0, 0, 1 });
	for (float &v : rotation) v *= scale;
	return rotation;
}

void prefetch_asteroids(model1 &machine, gameplay_state &state)
{
	if (!state.z_wrap_prefetched) return;
	const uint32_t vars = machine.cpu().reg(VARIABLES_REG);
	const mat3 k = camera_rotation(machine);
	const auto eye = apply(k, { read_float(machine, vars + 0x7700), read_float(machine, vars + 0x7704), read_float(machine, vars + 0x7708) });
	for (uint32_t record = ATTRACT_ASTEROIDS; record < ATTRACT_ASTEROIDS_END; record += ASTEROID_RECORD_SIZE) {
		const auto found = state.asteroid_objects.find(record);
		const uint32_t object = found == state.asteroid_objects.end() ? 0 : found->second;
		if (object && state.wrap_list.asteroids.contains(object)) continue;
		// Once initialized, only the live object can decide whether it still exists.
		if (object && machine.read32(object + HANDLER) != machine.read32(record)) continue;
		const bool large = machine.read32(record) == ASTEROID_HANDLERS[0];
		const auto here = object ? position(machine, object) : std::array<float, 3>{
			float(int32_t(machine.read32(record + 8))), float(int32_t(machine.read32(record + 12))),
			float(int32_t(machine.read32(record + 16))) };
		const mat3 rotation = multiply(k, asteroid_rotation(object ? read_float(machine, object + ASTEROID_SCALE) : read_float(machine, record + 20),
			object ? machine.read16(object + ASTEROID_YAW) : 0, object ? machine.read16(object + ASTEROID_ROLL) : 0));
		const auto at = apply(k, here);
		std::array<float, 12> view;
		std::copy(rotation.begin(), rotation.end(), view.begin());
		for (int j = 0; j < 3; j++) view[9 + j] = at[j] - eye[j];
		const render_object model{ object ? machine.read32(object + ASTEROID_DATA) : large ? 0x51962u : 0x51961u,
			machine.read32(object ? machine.read32(object + ASTEROID_MODEL) : large ? 0x23ba7au : 0x23ba76u), 0, 0, std::nullopt };
		add_copies(machine, state, view, { model }, k, wrap_offsets(machine, here, true, asteroid_field(machine, state)), record);
	}
}

// where DB88C places a cluster with halfwords x and y (X's also its Z) around the camera at `eye`
// (7724/7728 are the eye's, but the list that creates the clusters has others there)
std::array<float, 3> cluster_position(model1 &machine, const std::array<float, 3> &eye, uint16_t x, uint16_t y, float z)
{
	const uint32_t vars = machine.cpu().reg(VARIABLES_REG);
	const int32_t camera_x = int32_t(eye[0]), camera_y = int32_t(std::lround(eye[1])), camera_z = int32_t(eye[2]);
	const auto wrap = [](uint16_t to, int32_t from, int bits) {
		return int32_t(int16_t(uint16_t((to - uint16_t(from)) << (16 - bits))) >> (16 - bits));
	};
	if (machine.read8(vars + MODE) < CLUSTERS_WRAP_Z_BELOW) z = float(camera_z + wrap(x, camera_z, 16));
	return { float(camera_x + wrap(x, camera_x, 15)), float(camera_y + wrap(y, camera_y, 13)), z };
}

// The cockpit is visible before DBE82 creates the rock clusters, and DB88C first places and draws
// them with the fleet. Until then they are drawn where it will place them, from their ROM placements
// and then their objects (with no roll before DB833 picks it).
void prefetch_clusters(model1 &machine, gameplay_state &state)
{
	if (!state.z_wrap_prefetched) return;
	const mat3 k = camera_rotation(machine);
	const uint32_t vars = machine.cpu().reg(VARIABLES_REG);
	const std::array<float, 3> camera{ read_float(machine, vars + 0x7700), read_float(machine, vars + 0x7704), read_float(machine, vars + 0x7708) };
	const auto eye = apply(k, camera);
	for (uint32_t record = ATTRACT_CLUSTERS; record < ATTRACT_CLUSTERS_END; record += CLUSTER_RECORD_SIZE) {
		const auto found = state.asteroid_objects.find(record);
		const uint32_t object = found == state.asteroid_objects.end() ? 0 : found->second;
		// (DBE82 sets the handler after DB833: until then it is not yet a cluster, rather than gone)
		const bool live = object && machine.read32(object + HANDLER) == CLUSTER_HANDLER;
		if (live) state.live_clusters.insert(object);
		if (object && (state.wrap_list.asteroids.contains(object) || (!live && state.live_clusters.contains(object)))) continue;
		const auto here = object ? cluster_position(machine, camera, machine.read16(object + CLUSTER_X), machine.read16(object + CLUSTER_Y), read_float(machine, object + POSITION_Z))
			: cluster_position(machine, camera, uint16_t(machine.read32(record + 8)), uint16_t(machine.read32(record + 12)), float(int32_t(machine.read32(record + 16))));
		const mat3 rotation = multiply(k, asteroid_rotation(CLUSTER_SCALE, 0, object ? machine.read16(object + CLUSTER_ROLL) : 0));
		const auto at = apply(k, here);
		std::array<float, 12> view;
		std::copy(rotation.begin(), rotation.end(), view.begin());
		for (int j = 0; j < 3; j++) view[9 + j] = at[j] - eye[j];
		const render_object model{ object ? machine.read32(object + ASTEROID_DATA) : CLUSTER_DATA,
			machine.read32(object ? machine.read32(object + ASTEROID_MODEL) : CLUSTER_MODEL), 0, 0, std::nullopt };
		const uint64_t before = state.wrapped_copies;
		const auto field = cluster_field(machine);
		add_copies(machine, state, view, { model }, k, wrap_offsets(machine, here, true, field, camera), record, field.reach);
		state.cluster_copies += state.wrapped_copies - before;
	}
}

// the culled objects so far, where they are and where they repeat
void place_culled(model1 &machine, gameplay_state &state)
{
	auto &list = state.wrap_list;
	if (list.culled.empty()) return;
	const mat3 k = camera_rotation(machine);
	// (with nothing drawn yet, as the field starts out of view: the camera's rotation of the eye's place)
	std::array<float, 3> view_offset;
	if (list.view_offset) view_offset = *list.view_offset;
	else {
		const uint32_t vars = machine.cpu().reg(VARIABLES_REG);
		const auto eye = apply(k, { read_float(machine, vars + 0x7700), read_float(machine, vars + 0x7704), read_float(machine, vars + 0x7708) });
		view_offset = { -eye[0], -eye[1], -eye[2] };
	}
	for (const auto &object : list.culled) {
		const mat3 rotation = multiply(k, object.rotation);
		const auto field = object.cluster ? cluster_field(machine) : asteroid_field(machine, state);
		for (const auto &offset : wrap_offsets(machine, object.position, true, field)) {
			std::array<float, 3> world;
			for (int i = 0; i < 3; i++) world[i] = object.position[i] + offset.d[i];
			const auto at = apply(k, world);
			std::array<float, 12> view;
			std::copy(rotation.begin(), rotation.end(), view.begin());
			for (int r = 0; r < 3; r++) view[9 + r] = at[r] + view_offset[r];
			for (auto model : object.models) {
				model.fade *= offset.fade;
				if (!add_copy(machine, state, view, model, field.reach, object.source)) continue;
				state.culled_copies++;
				if (object.cluster) state.cluster_copies++;
			}
		}
	}
	list.culled.clear();
}

// every copy so far, relative to the stars' matrix, and `word` that matrix command's
std::vector<render_object> star_extras(model1 &machine, gameplay_state &state, uint32_t &word)
{
	auto &list = state.wrap_list;
	place_culled(machine, state);
	if (!list.stars || !list.dirty) return {};
	list.dirty = false;
	mat3 parent;
	std::copy_n(list.stars_matrix.begin(), 9, parent.begin());
	const auto inv = inverse(parent);
	if (!inv) return {};
	std::vector<render_object> extras;
	extras.reserve(list.copies.size());
	for (const auto &[view, model] : list.copies) {
		mat3 rotation;
		std::copy_n(view.begin(), 9, rotation.begin());
		const mat3 local = multiply(*inv, rotation);
		const auto l = apply(*inv, { view[9] - list.stars_matrix[9], view[10] - list.stars_matrix[10], view[11] - list.stars_matrix[11] });
		render_object o = model;
		o.transform = std::array<float, 12>{ local[0], local[1], local[2], local[3], local[4], local[5], local[6], local[7], local[8], l[0], l[1], l[2] };
		extras.push_back(o);
	}
	word = list.stars;
	return extras;
}

// (Full) the asteroid field's world repeats every WRAP_PERIOD, but the game keeps one copy of each
// object, in the period around the camera, and draws only those its view test passes. Each
// repeating object's models the game draws are copied to where they repeat within WRAP_REACH, and
// the objects it culled are drawn where they are and where they repeat. All are drawn with the
// stars' matrix (the camera's rotation alone, the same every frame's list), so smooth motion's
// blend between two lists moves them with the camera rather than with a tumbling asteroid.
// `word` is the written halfword; it becomes the stars' matrix command's when the copies change.
std::vector<render_object> wrapped_copies(model1 &machine, gameplay_state &state, uint32_t &word)
{
	const uint32_t pc = machine.cpu().pc(), list = machine.cpu().reg(LIST_REG);
	auto key = [](uint32_t address) { return (address >> 1) & 0x7fff; };
	const bool bare = pc == BARE_MATRIX_WRITE;
	const bool full = std::find(std::begin(MATRIX_WRITES), std::end(MATRIX_WRITES), pc) != std::end(MATRIX_WRITES);
	const bool append = std::find(std::begin(OBJECT_APPENDS), std::end(OBJECT_APPENDS), pc) != std::end(OBJECT_APPENDS);
	uint32_t matrix = 0, added = 0;
	if (bare || full) {
		matrix = list - MATRIX_SIZE - (full ? OBJECT_SIZE : 0);
		if (word != key(matrix) + 1) matrix = 0;
		else {
			begin_list(state, matrix);
			state.wrap_matrix = matrix;
			if (full) added = matrix + MATRIX_SIZE;
		}
	} else if (append && word == key(list - OBJECT_SIZE) + 1) {
		matrix = state.wrap_matrix;
		added = list - OBJECT_SIZE;
	}
	auto &wl = state.wrap_list;
	if (!matrix) return wl.stars && wl.buffer == (list & LIST_BUFFER) ? star_extras(machine, state, word) : std::vector<render_object>{};
	const auto read_matrix = [&] {
		std::array<float, 12> view;
		for (int i = 0; i < 12; i++) view[i] = read_float(machine, matrix + MATRIX_FLOATS + 4 * i);
		return view;
	};
	if (full && (machine.read32(added + 8) & POLYGON_ADDRESS) == STARS_MODEL) {
		wl.stars = key(matrix);
		wl.stars_matrix = read_matrix();
		prefetch_destroyers(machine, state);
		prefetch_asteroids(machine, state);
		prefetch_clusters(machine, state);
		wl.dirty = true;
		return star_extras(machine, state, word);
	}
	const uint32_t object = machine.cpu().reg(OBJECT_REG);
	if (!repeats(machine, state, object)) return {};
	const bool destroyer = machine.read32(object + HANDLER) == DESTROYER_HANDLER;
	if (!destroyer) wl.asteroids.insert(object);
	const auto here = position(machine, object);
	const mat3 k = camera_rotation(machine);
	const auto view = read_matrix();
	// an object's own matrix (C1869's, C1A1D's) places it at its position: the frame's view offset
	if (pc == ASTEROID_MATRIX_WRITE || (destroyer && bare)) {
		const auto at = apply(k, here);
		wl.view_offset = std::array<float, 3>{ view[9] - at[0], view[10] - at[1], view[11] - at[2] };
	}
	if (destroyer && bare) {
		state.destroyer_matrix = matrix;
		state.destroyer_models[object].clear();
	}
	if (added) {
		const render_object model{ machine.read32(added + 4), machine.read32(added + 8), machine.read32(added + 12), 0, std::nullopt };
		if (destroyer && matrix == state.destroyer_matrix) {
			state.destroyer_models[object].push_back(model);
			state.last_destroyer_models = state.destroyer_models[object];
		}
		const bool cluster = machine.read32(object + HANDLER) == CLUSTER_HANDLER;
		const auto field = cluster ? cluster_field(machine) : asteroid_field(machine, state);
		const uint64_t before = state.wrapped_copies;
		add_copies(machine, state, view, { model }, k, wrap_offsets(machine, here, false, field), copy_source(machine, state, object), field.reach);
		if (cluster) state.cluster_copies += state.wrapped_copies - before;
	}
	return wl.stars ? star_extras(machine, state, word) : std::vector<render_object>{};
}

// (Full) a repeating object the game culled: its rotation and models, to draw where it repeats in view
void record_culled(model1 &machine, gameplay_state &state, uint32_t object, bool destroyer)
{
	gameplay_state::culled_object culled;
	culled.position = position(machine, object);
	culled.source = copy_source(machine, state, object);
	if (destroyer) {
		const auto own = state.destroyer_models.find(object);
		culled.models = own != state.destroyer_models.end() ? own->second :
			state.last_destroyer_models.empty() ? intact_destroyer(machine, state.lod, machine.read8(object + 0x3b)) : state.last_destroyer_models;
		culled.rotation = { DESTROYER_SCALE, 0, 0, 0, DESTROYER_SCALE, 0, 0, 0, DESTROYER_SCALE };
	} else {
		culled.models.push_back({ machine.read32(object + ASTEROID_DATA), machine.read32(machine.read32(object + ASTEROID_MODEL)), 0, 0, std::nullopt });
		culled.cluster = machine.read32(object + HANDLER) == CLUSTER_HANDLER;
		// DB88C: TGP 0x16 (its roll), then 0x13 (its scale)
		culled.rotation = culled.cluster ? asteroid_rotation(CLUSTER_SCALE, 0, machine.read16(object + CLUSTER_ROLL)) :
			asteroid_rotation(read_float(machine, object + ASTEROID_SCALE), machine.read16(object + ASTEROID_YAW), machine.read16(object + ASTEROID_ROLL));
	}
	if (culled.models.empty()) return;
	begin_list(state, machine.read32(machine.cpu().reg(VARIABLES_REG) + LIST_POINTER));
	if (!destroyer) state.wrap_list.asteroids.insert(object);
	state.wrap_list.culled.push_back(std::move(culled));
}
}

gameplay_callbacks create_gameplay(model1 &machine)
{
	gameplay_callbacks callbacks;
	auto state = std::make_shared<gameplay_state>();
	callbacks.state = state;
	callbacks.wide_scene = [&machine](uint32_t, bool) {
		const auto &ram = machine.nvram();
		const uint8_t sequence = ram[0xf008], subsequence = ram[0xf009];
		return !(sequence == MUSIC_CREDITS_SEQUENCE && subsequence == MUSIC_CREDITS_SUBSEQUENCE &&
				 ram[0x8000 + MUSIC_CREDITS_FLAG] != 0);
	};
	callbacks.layer_sides = [&machine, state](int layer, uint32_t *pixels, int width, int height, int ext) {
		return layer == COCKPIT_LAYER && state->wide_cockpit && cockpit_shown(machine) && continue_sides(pixels, width, height, ext);
	};
	callbacks.reset = [state] {
		state->view_word = -1;
		state->side_pass = nullptr;
		state->side_layer = -1;
		state->z_wrap_prefetched = false;
		state->destroyers_wrap = false;
		state->destroyer_manager = 0;
		state->asteroid_objects.clear();
		state->live_clusters.clear();
		state->destroyer_models.clear();
		state->last_destroyer_models.clear();
		state->destroyer_matrix = state->wrap_matrix = 0;
		state->wrap_list = {};
	};
	callbacks.frame_begin = [&machine, state] {
		const uint8_t sequence = machine.read8(0x40f008), subsequence = machine.read8(0x40f009);
		const bool asteroid_field = sequence == Z_WRAP_PREFETCH_SEQUENCE &&
			machine.read8(machine.cpu().reg(VARIABLES_REG) + ATTRACT_DEMO) == ASTEROID_FIELD_DEMO;
		if (state->full_distance && asteroid_field && subsequence == Z_WRAP_PREFETCH_SUBSEQUENCE) state->z_wrap_prefetched = true;
		else if (!state->full_distance || !asteroid_field) state->z_wrap_prefetched = false;
		if (!asteroid_field) {
			state->asteroid_objects.clear();
			state->live_clusters.clear();
		}
	};
	callbacks.copro_write = [&machine, state](uint32_t, uint32_t value) {
		// (no extent is 0x31 as a float, so a data word that happens to be 0x31 only restarts the count)
		if (value == VIEW_EXTENTS)
			state->view_word = 0;
		else if (state->view_word >= 0) {
			value = view_extent(state->view_word, value, machine.wide_ext());
			if (++state->view_word == VIEW_ARGUMENTS) state->view_word = -1;
		}
		return value;
	};
	callbacks.copro_result = [&machine, state](uint32_t pc, uint32_t value) {
		if (state->full_distance && int32_t(value) < 0) {
			if (std::find(std::begin(ASTEROID_CULL_READS), std::end(ASTEROID_CULL_READS), pc) != std::end(ASTEROID_CULL_READS) ||
				pc == CLUSTER_CULL_READ)
				record_culled(machine, *state, machine.cpu().reg(OBJECT_REG), false);
			else if (pc == DESTROYER_CULL_READ && (state->destroyers_wrap || state->z_wrap_prefetched) &&
				machine.read8(machine.cpu().reg(VARIABLES_REG) + MODE) < DESTROYERS_WRAP_BELOW)
				record_culled(machine, *state, machine.cpu().reg(MANAGED_REG), true);
		}
		if (state->full_distance)
			for (int i = 0; i < 3; i++)
				if (pc == SURFACE_LIGHT_READS[i]) return band_light(machine, *state, i, value);
		const uint32_t result = lod_result(state->lod, pc, value);
		if (result != value) state->nearest++;
		return result;
	};
	callbacks.matrix_objects = [&machine, state](uint32_t &word) -> std::vector<render_object> {
		if (state->lod && !state->full_distance && !machine.wide_ext()) return {};
		if (state->fleet_stern && machine.cpu().pc() == FLEET_MATRIX_WRITE) {
			const uint32_t matrix = (machine.cpu().reg(LIST_REG) - MATRIX_SIZE - OBJECT_SIZE) >> 1 & 0x7fff;
			if (word == matrix + 1) {
				state->fleet_stern = false;
				word = matrix;
				return { { FLEET_STERN_DATA, machine.read32(FLEET_STERN_MODEL), 0, 0, std::nullopt } };
			}
		}
		if (machine.cpu().pc() == SURFACE_MATRIX_WRITE) return surface_cells(machine, *state);
		return state->full_distance ? wrapped_copies(machine, *state, word) : std::vector<render_object>{};
	};
	callbacks.object = [&machine, state](uint32_t, uint32_t, uint32_t polygon, uint32_t, const float *) {
		object_effects effects;
		effects.hidden = !state->lod && coarse_model(machine, *state, polygon);
		if (effects.hidden) state->coarse_hidden++;
		return effects;
	};
	callbacks.read_pc_first = ASTEROID_INIT_READS[0];
	callbacks.read_pc_last = TURRET_MASK_READ;
	// only the operand itself: an interrupt taken at these instructions reads its vector with the same PC
	callbacks.read_value = [&machine, state](uint32_t pc, uint32_t address, uint16_t value) -> uint16_t {
		if (std::find(std::begin(ASTEROID_INIT_READS), std::end(ASTEROID_INIT_READS), pc) != std::end(ASTEROID_INIT_READS) &&
			address == machine.cpu().reg(4) && address >= ATTRACT_ASTEROIDS + 26 && address < ATTRACT_ASTEROIDS_END &&
			(address - ATTRACT_ASTEROIDS - 26) % ASTEROID_RECORD_SIZE == 0)
			state->asteroid_objects[address - 26] = machine.cpu().reg(LIST_REG);
		if (pc == CLUSTER_INIT_READ && address == machine.cpu().reg(4) && address >= ATTRACT_CLUSTERS + 16 && address < ATTRACT_CLUSTERS_END &&
			(address - ATTRACT_CLUSTERS - 16) % CLUSTER_RECORD_SIZE == 0)
			state->asteroid_objects[address - 16] = machine.cpu().reg(LIST_REG);
		if (pc == DESTROYER_MANAGER_INIT && address == machine.cpu().reg(4)) {
			state->destroyer_manager = machine.cpu().reg(OBJECT_REG);
			state->destroyers_wrap = false;
			state->destroyer_models.clear();
			state->last_destroyer_models.clear();
		}
		if (pc == WRAP_FLAG_READ && address == machine.cpu().reg(VARIABLES_REG) + WRAP_FLAGS) {
			state->destroyers_wrap = (machine.cpu().reg(0) & value) != 0;
		}
		if (state->lod) return value;
		const uint32_t object = machine.cpu().reg(OBJECT_REG);
		if (pc == DESTROYER_DETAIL_READ && address == object + DETAIL) {
			const uint8_t result = destroyer_detail(false, uint8_t(value));
			if (result != value) state->destroyers_near++;
			return result;
		}
		const uint32_t mode = machine.cpu().reg(VARIABLES_REG) + MODE;
		if (pc == ALL_FAR_READ && address == mode && value == MODE_ALL_FAR) return 0;
		if (pc == TURRET_MASK_READ && address == object + TURRET_MASK &&
			(machine.read8(object + DETAIL) != DESTROYER_NEAR || machine.read8(mode) == MODE_ALL_FAR)) return 0xffff;
		return value;
	};
	callbacks.read32_watch = OBJECTS_FIRST;
	callbacks.read32_watch_end = OBJECTS_LAST;
	callbacks.read32_value = [&machine, state](uint32_t pc, uint32_t address, uint32_t value) -> uint32_t {
		if (state->lod || (pc != FLEET_MODEL_READ && pc != FLEET_DATA_READ)) return value;
		const uint32_t object = machine.cpu().reg(OBJECT_REG);
		// D55FA replaces D558C's complete globe with three smaller approach patches.
		// Keep the complete geometry and its matching colour tables across that swap.
		if (pc == FLEET_MODEL_READ && address == object + FLEET_MODEL &&
			value >= 0x23b68e && value <= 0x23b696 && (value - 0x23b68e) % 4 == 0)
			return value - 12;
		if (pc == FLEET_DATA_READ && address == object + FLEET_DATA) {
			if (value == 0x451a8) return 0x44f9f;
			if (value == 0x45224) return 0x450da;
			if (value == 0x452a0) return 0x451a7;
		}
		if (pc == FLEET_MODEL_READ && address == object + FLEET_MODEL && (value == FLEET_FAR_MODEL || value == FLEET_NEAR_MODEL)) {
			state->destroyers_near++;
			state->fleet_stern = true;
			return FLEET_HULL_MODEL;
		}
		if (pc == FLEET_DATA_READ && address == object + FLEET_DATA && (value == FLEET_FAR_DATA || value == FLEET_NEAR_DATA)) return FLEET_HULL_DATA;
		return value;
	};
	return callbacks;
}

// The settings (R25 + 7C80, battery-backed RAM): "S32A", a ROM word, then a record (CRC, a count
// of saves, the settings) and its backup copy; 736DC loads the 0x80 bytes from the I/O board's
// EEPROM at boot (the backup if the record's CRC fails, defaults if both do) and 737BB saves them.
namespace {
constexpr uint32_t SETTINGS = 0xfc80, SETTINGS_SIZE = 0x80;   // in nvram()
constexpr uint32_t RECORD = 0x08, BACKUP = 0x44, RECORD_SIZE = 0x3c, CRC_FROM = 2, CRC_SIZE = 0x3a;
constexpr std::array<uint32_t, 4> ASSIGNMENT_BYTE{ 0x1d, 0x1e, 0x1f, 0x0e };   // in the block (the record's from 0x08)
constexpr std::array<int, 4> ASSIGNMENT_COUNT{ 5, 5, 5, 2 };
constexpr std::array<int, 4> ASSIGNMENT_STORED_FROM{ 1, 1, 1, 0 };   // the difficulties are stored 1-5
// COIN ASSIGNMENT's coin/credit setting: a 4-byte entry of the table at 70482 (setting #1-#26) or
// MANUAL SETTING's own values, then the byte MANUAL SETTING's COIN TO CREDIT also sets
constexpr uint32_t COIN = 0x0f, COIN_SIZE = 5;

uint8_t *settings_block(model1 &machine)
{
	uint8_t *s = &machine.nvram()[SETTINGS];
	if (memcmp(s, "S32A", 4)) return nullptr;
	const uint8_t *r = s + RECORD;
	if (settings_crc(r + CRC_FROM, CRC_SIZE) != uint16_t(r[0] | r[1] << 8)) return nullptr;
	return s;
}
}

uint16_t settings_crc(const uint8_t *data, size_t size)
{
	uint32_t r = 0xdebdeb00;
	auto shift = [&] {
		for (int b = 0; b < 8; b++) r = (r & 0x80000000) ? (r << 1) ^ 0x10210000 : r << 1;
	};
	for (size_t i = 0; i < size; i++) {
		r = (r & 0xffffff00) | data[i];
		shift();
	}
	shift();
	return uint16_t(r >> 16);
}

int game_assignment_value(model1 &machine, game_assignment setting)
{
	const auto i = size_t(setting);
	const uint8_t *s = settings_block(machine);
	if (i >= ASSIGNMENT_BYTE.size() || !s) return -1;
	const int value = s[ASSIGNMENT_BYTE[i]] - ASSIGNMENT_STORED_FROM[i];
	return value >= 0 && value < ASSIGNMENT_COUNT[i] ? value : -1;
}

namespace {
// The record's CRC and backup copy, then the block into the EEPROM
void save_settings(model1 &machine, uint8_t *s)
{
	uint8_t *r = s + RECORD;
	const uint16_t crc = settings_crc(r + CRC_FROM, CRC_SIZE);
	r[0] = uint8_t(crc);
	r[1] = uint8_t(crc >> 8);
	memcpy(s + BACKUP, r, RECORD_SIZE);
	// the EEPROM's words hold the bytes high first, as the I/O board stores them
	if (uint16_t *words = machine.eeprom_words())
		for (uint32_t w = 0; w < SETTINGS_SIZE / 2; w++) words[w] = uint16_t(s[2 * w] << 8 | s[2 * w + 1]);
	else memcpy(machine.eeprom(), s, SETTINGS_SIZE);
}
}

bool set_game_assignment(model1 &machine, game_assignment setting, int value)
{
	const auto i = size_t(setting);
	uint8_t *s = settings_block(machine);
	if (i >= ASSIGNMENT_BYTE.size() || !s || value < 0 || value >= ASSIGNMENT_COUNT[i]) return false;
	s[ASSIGNMENT_BYTE[i]] = uint8_t(value + ASSIGNMENT_STORED_FROM[i]);
	save_settings(machine, s);
	return true;
}

int64_t coin_setting(model1 &machine)
{
	const uint8_t *s = settings_block(machine);
	if (!s) return -1;
	int64_t value = 0;
	for (uint32_t i = 0; i < COIN_SIZE; i++) value = value << 8 | s[COIN + i];
	return value;
}

bool set_coin_setting(model1 &machine, int64_t value)
{
	uint8_t *s = settings_block(machine);
	if (!s || value < 0 || value >> (8 * COIN_SIZE)) return false;
	uint8_t bytes[COIN_SIZE];
	for (uint32_t i = 0; i < COIN_SIZE; i++) bytes[i] = uint8_t(value >> (8 * (COIN_SIZE - 1 - i)));
	if (!memcmp(s + COIN, bytes, COIN_SIZE)) return false;
	memcpy(s + COIN, bytes, COIN_SIZE);
	save_settings(machine, s);
	return true;
}

}

