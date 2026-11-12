#include "../../../src/games/wingwar/gameplay.h"
#include "../../../src/games/wingwar/profile.h"
#include "../../../src/host/regions.h"
#include <cassert>
#include <cstdio>

int main(int argc, char **argv)
{
	if (argc != 2) {
		fprintf(stderr, "usage: %s regions-dir (requires wingwar.zip)\n", argv[0]);
		return 2;
	}

	model1 full_detail(load_regions(argv[1], game::wingwar::ID));
	model1 arcade_detail(load_regions(argv[1], game::wingwar::ID));
	game::wingwar::set_extended_draw_distance(full_detail, false);
	game::wingwar::set_extended_draw_distance(arcade_detail, false);
	game::wingwar::set_lod(full_detail, false);
	game::wingwar::set_lod(arcade_detail, true);
	full_detail.reset();
	arcade_detail.reset();
	uint64_t full_quads = 0, arcade_quads = 0;
	int full_geometry_frames = 0, arcade_geometry_frames = 0;
	for (int frame = 1; frame <= 1450; frame++) {
		if (frame == 200) {
			full_detail.m_inputs[0] &= uint8_t(~0x01);
			arcade_detail.m_inputs[0] &= uint8_t(~0x01);
		}
		if (frame == 260) {
			full_detail.m_inputs[0] |= 0x01;
			arcade_detail.m_inputs[0] |= 0x01;
		}
		if (frame == 280) {
			full_detail.m_inputs[0] &= uint8_t(~0x10);
			arcade_detail.m_inputs[0] &= uint8_t(~0x10);
		}
		if (frame == 300) {
			full_detail.m_inputs[0] |= 0x10;
			arcade_detail.m_inputs[0] |= 0x10;
		}
		full_detail.run_frame();
		arcade_detail.run_frame();
		full_quads += full_detail.frame_quads();
		arcade_quads += arcade_detail.frame_quads();
		full_geometry_frames += full_detail.frame_quads() > 0;
		arcade_geometry_frames += arcade_detail.frame_quads() > 0;
	}
	assert(full_geometry_frames > 100 && arcade_geometry_frames > 100);
	assert(full_detail.io_board_byte(0x1a) == 'S' && arcade_detail.io_board_byte(0x1a) == 'S');
	printf("Wing War LOD: full detail %llu quads, arcade detail %llu quads; TGP cycles %llu / %llu\n",
		(unsigned long long)full_quads, (unsigned long long)arcade_quads,
		(unsigned long long)full_detail.tgp_cycles_executed(),
		(unsigned long long)arcade_detail.tgp_cycles_executed());
	assert(full_quads > arcade_quads);
}
