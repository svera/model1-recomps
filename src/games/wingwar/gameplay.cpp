#include "gameplay.h"
#include "../../machine/model1.h"
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <glm/matrix.hpp>

namespace game::wingwar {

namespace {
constexpr uint32_t NEAREST_DETAIL_DISTANCE = std::bit_cast<uint32_t>(256.0f);

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
	callbacks.supported_cheats = INVINCIBILITY;
	callbacks.read_pc_first = callbacks.read_pc_last = DAMAGE_IMMUNITY_READ;
	callbacks.read_value = [&machine, state](uint32_t pc, uint32_t address, uint16_t value) {
		// 732c[R25] identifies the local pilot; the other combat aircraft is CPU-controlled.
		const uint32_t player = machine.cpu().reg(24);
		return uint16_t((state->cheats & INVINCIBILITY) && pc == DAMAGE_IMMUNITY_READ &&
			address == 0x40f308 && (player == 0x40b400 || player == 0x40b800) &&
			player == machine.read32(0x40f32c) ? 1 : value);
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
	callbacks.reset = [state] { state->scenery = {}; state->cameras = {}; state->cheats = 0; };
	callbacks.matrix_objects = [&machine, state](uint32_t &word) {
		capture_camera(machine, *state, word);
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
