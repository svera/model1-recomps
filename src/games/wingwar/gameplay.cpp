#include "gameplay.h"
#include "../../machine/model1.h"
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <glm/geometric.hpp>
#include <glm/matrix.hpp>

namespace game::wingwar {

namespace {
constexpr uint32_t NEAREST_DETAIL_DISTANCE = std::bit_cast<uint32_t>(256.0f);

bool local_aircraft(model1 &machine, uint32_t aircraft)
{
	return (aircraft == 0x40b400 || aircraft == 0x40b800) && aircraft == machine.read32(0x40f32c);
}

void clear_hit_reaction(model1 &machine, uint32_t aircraft)
{
	uint32_t handler;
	switch (machine.read32(aircraft + 0xf8)) {
	case 0x2a9b5: case 0x2a9f7: case 0x2aa38: case 0x2ab62:
		handler = 0x2a90a;
		break;
	case 0x2c211: case 0x2c24f: case 0x2c285: case 0x2c2c2: case 0x2c382:
		handler = 0x2c1a4;
		break;
	default:
		return;
	}
	// Match the ROM's reaction exit, including bit D, then release its steering penalty.
	machine.write32(aircraft + 0xf8, handler);
	machine.write32(aircraft + 0x50, machine.read32(aircraft + 0x50) & ~0x2000u);
	for (uint32_t offset = 0x70; offset <= 0x74; offset++) machine.write8(aircraft + offset, 0);
	machine.write16(aircraft + 0xf6, 0);
	machine.write8(aircraft + 0x9a, 0);
	machine.write8(aircraft + 0x9b, 0);
}

uint32_t rom_word(model1 &machine, uint32_t address, uint32_t bytes = 4)
{
	if (address < 0x200000 || address > 0x300000 - bytes) {
		fprintf(stderr, "Wing War draw distance: invalid scenery ROM address %08x\n", address);
		std::abort();
	}
	return machine.read32(address);
}

void capture_camera(model1 &machine, gameplay_state &state, uint32_t word)
{
	if (machine.cpu().pc() != 0x02ffae || !(word & 1)) return;
	const uint32_t cursor = machine.cpu().reg(14);
	if (cursor < 0x600000 || cursor >= 0x620000 || word < 23) {
		fprintf(stderr, "Wing War interpolation: invalid camera matrix cursor %08x/%04x\n", cursor, word);
		std::abort();
	}
	const int buffer = int((cursor - 0x600000) >> 16);
	const uint32_t record = machine.read32(machine.cpu().reg(21) - 6);
	rom_word(machine, record, 12);
	std::array<float, 12> camera;
	for (int i = 0; i < 12; i++)
		camera[i] = std::bit_cast<float>(machine.read32((cursor & 0xff0000) + (word - 23) * 2 + i * 4));
	// The startup lists contain zero matrices before the TGP has a camera.
	const float determinant = camera[0] * (camera[4] * camera[8] - camera[7] * camera[5]) -
		camera[3] * (camera[1] * camera[8] - camera[7] * camera[2]) +
		camera[6] * (camera[1] * camera[5] - camera[4] * camera[2]);
	auto &sample = state.cameras[buffer];
	sample.frame = machine.frame_number();
	sample.matrix.reset();
	if (std::all_of(camera.begin(), camera.begin() + 9, [](float value) { return value == 0; })) return;
	if (!std::isfinite(determinant) || std::abs(determinant - 1) > 0.01f) {
		fprintf(stderr, "Wing War interpolation: invalid scenery camera rotation for record %08x\n", record);
		std::abort();
	}
	for (int axis = 0; axis < 3; axis++)
		for (int k = 0; k < 3; k++)
			camera[9 + axis] -= camera[k * 3 + axis] * std::bit_cast<float>(rom_word(machine, record + k * 4));
	if (!std::all_of(camera.begin(), camera.end(), [](float value) { return std::isfinite(value); })) {
		fprintf(stderr, "Wing War interpolation: non-finite camera for record %08x\n", record);
		std::abort();
	}
	sample.matrix = camera;
}

// The ending's hangar scenery (0x3af2f) has no camera matrix of its own: each piece is drawn at a
// world position (8/C/10[R24]) with fixed turns and scale, all on one height. The camera is fitted
// from the moves of three or more pieces: move = R * position + T, and R's middle column is the
// cross product of the other two.
void capture_credits_camera(model1 &machine, gameplay_state &state, uint32_t word)
{
	if (machine.cpu().pc() != 0x0026d8 || !(word & 1) || machine.read32(machine.cpu().reg(31)) != 0x03afa0) return;
	const uint32_t cursor = machine.cpu().reg(11);
	if (cursor < 0x600000 || cursor >= 0x620000 || word < 23) {
		fprintf(stderr, "Wing War interpolation: invalid credits matrix cursor %08x/%04x\n", cursor, word);
		std::abort();
	}
	const int buffer = int((cursor - 0x600000) >> 16);
	auto &credits = state.credits[buffer];
	auto &sample = state.cameras[buffer];
	if (credits.frame != machine.frame_number()) credits = { {}, machine.frame_number() };
	const uint32_t piece = machine.cpu().reg(24);
	std::array<float, 6> point;
	for (int k = 0; k < 3; k++) {
		point[k] = std::bit_cast<float>(machine.read32(piece + 8 + k * 4));
		point[3 + k] = std::bit_cast<float>(machine.read32((cursor & 0xff0000) + (word - 23) * 2 + 36 + k * 4));
	}
	credits.points.push_back(point);
	sample.frame = machine.frame_number();
	sample.matrix.reset();
	if (credits.points.size() < 3) return;
	glm::dmat3 normal(0);
	glm::dmat3 moved(0);
	for (const auto &p : credits.points) {
		const glm::dvec3 row(p[0], p[2], 1);
		normal += glm::outerProduct(row, row);
		moved += glm::outerProduct(row, glm::dvec3(p[3], p[4], p[5]));
	}
	if (std::abs(glm::determinant(normal)) < 1e-6) return;
	// solution's columns: the move per x, per z, then the constant
	const glm::dmat3 solution = glm::transpose(glm::inverse(normal) * moved);
	const glm::dvec3 x = solution[0], z = solution[1], y = glm::cross(z, x);
	const glm::dvec3 move = solution[2] - y * double(credits.points[0][1]);
	if (std::abs(glm::length(x) - 1) > 0.01 || std::abs(glm::length(z) - 1) > 0.01 || std::abs(glm::dot(x, z)) > 0.01) return;
	for (const auto &p : credits.points) {
		const glm::dvec3 fit = x * double(p[0]) + y * double(p[1]) + z * double(p[2]) + move;
		if (glm::length(fit - glm::dvec3(p[3], p[4], p[5])) > 0.05 * std::max(1.0, glm::length(fit))) return;
	}
	std::array<float, 12> camera;
	for (int k = 0; k < 3; k++) {
		camera[k] = float(x[k]);
		camera[3 + k] = float(y[k]);
		camera[6 + k] = float(z[k]);
		camera[9 + k] = float(move[k]);
	}
	sample.matrix = camera;
}

std::vector<render_object> scenery_objects(model1 &machine, gameplay_state &state, uint32_t &word)
{
	const uint32_t pc = machine.cpu().pc();
	if ((word & 1) || (pc != 0x02ff79 && pc != 0x02fe99 && pc != 0x02fdf4)) return {};
	const uint32_t cursor = machine.cpu().reg(14);
	if (cursor < 0x600000 || cursor >= 0x620000) {
		fprintf(stderr, "Wing War draw distance: invalid scenery display-list cursor %08x\n", cursor);
		std::abort();
	}
	auto &list = state.scenery[(cursor - 0x600000) >> 16];
	if (pc != 0x02fdf4) {
		if (!list.started) {
			list.started = true;
			list.extended = state.extended_draw_distance;
		}
		if (!list.extended) return {};
		const uint32_t node = machine.cpu().reg(21);
		if (node < 0x40c106 || node > 0x40f000) {
			fprintf(stderr, "Wing War draw distance: invalid scenery node %08x\n", node);
			std::abort();
		}
		const uint32_t record = machine.read32(node - 6);
		rom_word(machine, record, 0x1c);
		list.submitted.insert(record);
		if (pc == 0x02ff79 && !list.matrix) {
			list.matrix = word;
			list.anchor = record;
		}
		return {};
	}
	if (!list.matrix) {
		list = {};
		return {};
	}
	word = *list.matrix;
	std::vector<render_object> objects;
	if (list.extended) {
		const uint32_t variables = machine.cpu().reg(25);
		const uint32_t rows = machine.read32(variables + 0x734c) - 128 * 4;
		const uint32_t counts = machine.read32(variables + 0x7350) - 128;
		const uint32_t models = machine.read32(variables + 0x7354);
		rom_word(machine, rows, 256 * 4);
		rom_word(machine, counts, 256);
		auto number = [&](uint32_t address) { return std::bit_cast<float>(rom_word(machine, address)); };
		const std::array<float, 3> anchor{ number(list.anchor), number(list.anchor + 4), number(list.anchor + 8) };
		for (uint32_t row = 0; row < 256; row++) {
			const uint32_t count = machine.read8(counts + row);
			const uint32_t first = rom_word(machine, rows + row * 4);
			if (!count) continue;
			rom_word(machine, first, count * 0x1c);
			for (uint32_t i = 0; i < count; i++) {
				const uint32_t record = first + i * 0x1c;
				if (list.submitted.contains(record)) continue;
				const uint32_t type = rom_word(machine, record + 0x10);
				if (type >= 256) {
					fprintf(stderr, "Wing War draw distance: invalid scenery type %08x\n", type);
					std::abort();
				}
				// Tier one is also the model selected by LOD Off; tier zero runs compound handlers.
				const uint32_t model = rom_word(machine, models + type * 16 + 4);
				rom_word(machine, model, 4);
				const uint32_t entry = 0x271484 + uint32_t(machine.read16(model + 2)) * 16;
				rom_word(machine, entry, 12);
				std::array<float, 12> transform{ 1, 0, 0, 0, 1, 0, 0, 0, 1 };
				for (int axis = 0; axis < 3; axis++) {
					transform[9 + axis] = number(record + axis * 4) - anchor[axis];
					if (!std::isfinite(transform[9 + axis])) {
						fprintf(stderr, "Wing War draw distance: non-finite scenery position %08x\n", record);
						std::abort();
					}
				}
				objects.push_back({ rom_word(machine, entry), rom_word(machine, entry + 4),
					rom_word(machine, entry + 8), record, transform });
				objects.back().key = record;
			}
		}
	}
	list = {};
	return objects;
}
}

uint32_t lod_result(bool arcade_lod, uint32_t pc, uint32_t value)
{
	// The V60 converts this distance to 256, then shifts by eight to select model tier 1.
	return !arcade_lod && pc == LOD_RESULT_READ ? NEAREST_DETAIL_DISTANCE : value;
}

gameplay_callbacks create_gameplay(model1 &machine)
{
	gameplay_callbacks callbacks;
	auto state = std::make_shared<gameplay_state>();
	callbacks.state = state;
	callbacks.supported_cheats = INVINCIBILITY | INFINITE_MISSILES;
	callbacks.read_pc_first = HIT_EVENT_READ;
	callbacks.read_pc_last = MISSILE_STOCK_READ;
	callbacks.read_value = [&machine, state](uint32_t pc, uint32_t address, uint16_t value) {
		const uint32_t target = machine.cpu().reg(0);
		const bool pitch_hit = pc == PROJECTILE_PITCH_READ && address == target + 0xb8;
		const bool roll_hit = pc == PROJECTILE_ROLL_READ && address == target + 0xbc;
		if ((state->cheats & INVINCIBILITY) && (pitch_hit || roll_hit) && local_aircraft(machine, target)) {
			// Projectiles add these impulses outside the damage routine; cancel only the new addend.
			return uint16_t(value - machine.cpu().reg(pitch_hit ? 15 : 17));
		}
		const uint32_t player = machine.cpu().reg(24);
		const bool immunity = (state->cheats & INVINCIBILITY) && pc == HIT_EVENT_READ &&
			address >= 0x502b00 && address <= 0x522afe;
		const bool missiles = (state->cheats & INFINITE_MISSILES) && pc == MISSILE_STOCK_READ && address == player + 0xea;
		if ((!immunity && !missiles) || !local_aircraft(machine, player)) return value;
		if (immunity && address == 0x502b00 + 2 * machine.read16(player + 0x4c)) {
			// Consume the latch even though returning zero skips the ROM's nonzero-event acknowledgement.
			machine.write16(address, 0);
			clear_hit_reaction(machine, player);
			return uint16_t(0);
		}
		if (missiles) {
			// Use the ROM's initial magazine size, leaving its launch/reload timers alone.
			const uint32_t weapons = machine.read32(0x1feb18 + machine.read8(player + 0x54) * 4);
			const uint8_t stock = machine.read8(weapons + 2);
			if (!stock || stock > 127) {
				fprintf(stderr, "Wing War: invalid missile magazine size %u at %08x\n", stock, weapons);
				std::abort();
			}
			machine.write8(address, stock);
			return uint16_t(stock);
		}
		return value;
	};
	callbacks.repeat_tile_layer = [&machine](int layer) {
		if ((layer != 4 && layer != 5) || machine.tile_word(0x2000) != 0x1c80 ||
			machine.tile_word(0x5002) != 0 || machine.tile_word(0x5006) != 0) return false;
		// Continue's purple starfield repeats every 16 tiles; don't wrap other screen artwork.
		for (int y = 0; y < 48; y++)
			for (int x = 0; x < 64; x++) {
				const uint16_t tile = machine.tile_word(0x2000 + y * 64 + x);
				if (tile < 0x1c80 || tile > 0x1dc1 ||
					tile != machine.tile_word(0x2000 + y * 64 + x % 16)) return false;
			}
		return true;
	};
	callbacks.reset = [state] { state->scenery = {}; state->cameras = {}; state->credits = {}; state->cheats = 0; };
	callbacks.matrix_objects = [&machine, state](uint32_t &word) {
		capture_camera(machine, *state, word);
		capture_credits_camera(machine, *state, word);
		return scenery_objects(machine, *state, word);
	};
	callbacks.draw_camera = [&machine, state](int buffer) -> std::optional<std::array<float, 12>> {
		const auto &sample = state->cameras[buffer];
		return machine.frame_number() - sample.frame <= 2 ? sample.matrix : std::nullopt;
	};
	callbacks.copro_result = [state](uint32_t pc, uint32_t value) {
		return lod_result(state->lod, pc, value);
	};
	callbacks.object = [&machine](uint32_t, uint32_t texture, uint32_t polygon, uint32_t size, const float *data) {
		object_effects effect;
		if (!machine.wide_ext()) return effect;
		const bool sky = texture == 0x5b471 && polygon == 0xa7 && size == 10 &&
			data == machine.polygon_data(true);
		const bool city_ground = texture == 0x46a75 && polygon == 0xc6e0f && size == 8 &&
			data == machine.polygon_data(false);
		const bool coast_ground = texture == 0x46a68 && polygon == 0xc6d4d && size == 5 &&
			data == machine.polygon_data(false);
		const bool countryside_ground = texture == 0x46aab && polygon == 0xc7ca2 && size == 20 &&
			data == machine.polygon_data(false);
		if (!sky && !city_ground && !coast_ground && !countryside_ground) return effect;
		// A banked view needs the corner-to-centre reach, not just the horizontal width.
		const float width = std::hypot(model1::SCREEN_W / 2.0f + machine.wide_ext(), model1::SCREEN_H / 2.0f) /
			(model1::SCREEN_W / 2.0f);
		if (sky) effect.point = [width](float &x, float &, float &) { x *= width; };
		else {
			const float left = city_ground ? -5120.0f : countryside_ground ? -8192.0f : -12800.0f;
			const float right = city_ground ? 12800.0f : countryside_ground ? 10240.0f : 5120.0f;
			const float near = city_ground ? -3072.0f : countryside_ground ? -8192.0f : -2560.0f;
			const float far = city_ground ? 23040.0f : countryside_ground ? 10240.0f : 20480.0f;
			const float ground_y = countryside_ground ? 0.0f : -2.0f;
			const auto *matrix = machine.render_matrix();
			const glm::mat3 rotation(glm::vec3(matrix[0], matrix[1], matrix[2]),
				glm::vec3(matrix[3], matrix[4], matrix[5]), glm::vec3(matrix[6], matrix[7], matrix[8]));
			if (!std::isfinite(glm::determinant(rotation)) || std::abs(glm::determinant(rotation)) < 1e-6f) {
				fprintf(stderr, "Wing War horizon: invalid ground camera rotation\n");
				std::abort();
			}
			const auto camera = -glm::inverse(rotation) * glm::vec3(matrix[9], matrix[10], matrix[11]);
			const auto zoom = machine.render_zoom();
			// Place the finite perimeter within 1/8 render pixel of the true horizon.
			const float reach = std::abs(camera.y - ground_y) * 8 * machine.render_scale() *
				std::hypot(std::max(std::abs(zoom[0]), std::abs(zoom[1])),
					std::hypot(model1::SCREEN_W / 2.0f + machine.wide_ext(), model1::SCREEN_H / 2.0f));
			if (!std::isfinite(camera.x) || !std::isfinite(camera.z) || !std::isfinite(reach)) {
				fprintf(stderr, "Wing War horizon: non-finite ground camera or projection\n");
				std::abort();
			}
			const float outer_left = std::min(left, camera.x - reach);
			const float outer_right = std::max(right, camera.x + reach);
			const float outer_near = std::min(near, camera.z - reach);
			const float outer_far = std::max(far, camera.z + reach);
			// Only the artificial perimeter moves; interior contour vertices stay fixed.
			effect.point = [=](float &x, float &, float &z) {
				if (x == left) x = outer_left;
				else if (x == right) x = outer_right;
				if (z == near) z = outer_near;
				else if (z == far) z = outer_far;
			};
		}
		return effect;
	};
	return callbacks;
}

void set_lod(model1 &machine, bool enabled)
{
	std::static_pointer_cast<gameplay_state>(machine.game_state())->lod = enabled;
}

void set_extended_draw_distance(model1 &machine, bool enabled)
{
	std::static_pointer_cast<gameplay_state>(machine.game_state())->extended_draw_distance = enabled;
}

}
