#include "../../../src/games/wingwar/controls.h"
#include "../../../src/games/wingwar/gameplay.h"
#include <cassert>

int main()
{
	using namespace game::wingwar;
	assert(game::find("wingwar") == ID);
	assert(game::get(ID).display_name == "Wing War");
	assert(game::get(ID).romset == "wingwar");
	assert(game::get(ID).cpu_cycles_per_instruction == 8);

	const auto &io = game::get_io_profile(ID);
	assert(io.digital_controls[0] == "Coin 1");
	assert(io.digital_controls[4] == "Start");
	assert(io.digital_controls[5] == "View 1");
	assert(io.digital_controls[6] == "View 2");
	assert(io.digital_controls[7] == "View 3");
	assert(io.digital_controls[8] == "View 4");
	assert(io.digital_controls[12] == "Machine Gun");
	assert(io.digital_controls[13] == "Missile");
	assert(io.digital_controls[14] == "Smoke");
	assert(!io.standin_analog);
	assert(io.analog_neutral[0] == 0x80 && io.analog_neutral[1] == 0x80 && io.analog_neutral[2] == 0x01);
	assert(io.analog_port(0, 0) == 255 && io.analog_port(0, 255) == 0);
	assert(io.analog_port(1, 0) == 0 && io.analog_port(1, 255) == 255);
	assert(io.analog_port(2, 0) == 1 && io.analog_port(2, 255) == 255);
	assert(io.host_axis_defaults[0] == game::host_axis::left_x);
	assert(io.host_axis_defaults[1] == game::host_axis::left_y);
	assert(io.host_axis_defaults[2] == game::host_axis::right_trigger);

	auto pressed = [](int action) {
		uint8_t in[3] = {};
		CONTROLS.press(in, 0, action);
		return std::array<uint8_t, 3>{ in[0], in[1], in[2] };
	};
	auto expect = [&](int action, uint8_t system, uint8_t player) {
		const auto in = pressed(action);
		assert(in[0] == system && in[1] == player && in[2] == 0);
	};
	expect(C_MACHINE_GUN, 0, 0x10);
	expect(C_MISSILE, 0, 0x20);
	expect(C_SMOKE, 0, 0x40);
	expect(C_VIEW, 0, VIEW_CYCLE_INPUT);
	assert(CONTROLS.keys[0][C_VIEW] == SDL_SCANCODE_C && CONTROLS.pad[C_VIEW] == SDL_GAMEPAD_BUTTON_NORTH);
	assert(CONTROLS.keys[0][C_SMOKE] == SDL_SCANCODE_V);
	assert(CONTROLS.start_bits[0] == 0x10);
	assert(CONTROLS.pad_actions.size() == 7);
	{
		// saved Smoke on C (its old settings name) moves to V; another key stays
		player_controls c = player_controls::defaults(0, CONTROLS);
		c.load({ { "p1_key_button_3", "C" } }, 0, CONTROLS);
		assert(c.key[C_SMOKE] == SDL_SCANCODE_V && c.key[C_VIEW] == SDL_SCANCODE_C);
		c = player_controls::defaults(0, CONTROLS);
		c.load({ { "p1_key_button_3", "B" } }, 0, CONTROLS);
		assert(c.key[C_SMOKE] == SDL_SCANCODE_B);
		c = player_controls::defaults(0, CONTROLS);
		c.load({ { "p1_key_smoke", "C" }, { "p1_key_view", "Q" } }, 0, CONTROLS);
		assert(c.key[C_SMOKE] == SDL_SCANCODE_C && c.key[C_VIEW] == SDL_SCANCODE_Q);
	}
	assert(game::get_video(ID).smooth_frames && game::get_video(ID).separate_parts);
	assert(game::get_video(ID).default_fps == 30);
	assert(!game::get_video(ID).stretch_backdrops);
	const auto &video = game::get_video(ID);
	assert(video.centered_scroll_layers == 0x33);
	assert(video.centered_scroll_y0 == 80 && video.centered_scroll_y1 == 304);
	assert(video.records && video.records->camera == 0);
	assert(video.records->records_from == 0x400000 && video.records->records_to == 0x410000);
	assert(video.records->writers.size() == 6);
	assert(video.records->writers[0].indirect && video.records->writers[0].record_offset == -6);
	assert(!gameplay_state{}.lod);
	assert(gameplay_state{}.extended_draw_distance);
	assert(lod_result(false, LOD_RESULT_READ, 0x12345678) == 0x43800000);
	assert(lod_result(true, LOD_RESULT_READ, 0x12345678) == 0x12345678);
	assert(lod_result(false, LOD_RESULT_READ + 1, 0x12345678) == 0x12345678);
}
