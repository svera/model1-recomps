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

	model1 m(load_regions(argv[1], game::wingwar::ID));
	assert(m.real_ioboard());
	assert(m.tgp_is_lle() && !m.tgp_has_hle());
	assert(m.has_analog_input(0) && m.has_analog_input(1) && m.has_analog_input(2));
	assert(!m.has_analog_input(3));
	assert(m.set_analog_input(0, 0x35));
	assert(m.set_analog_input(1, 0xc1));
	assert(m.set_analog_input(2, 0xa0));
	m.reset();
	int first_geometry = 0, geometry_frames = 0;
	for (int frame = 1; frame <= 700; frame++) {
		if (frame == 200) m.m_inputs[0] &= uint8_t(~0x01);
		if (frame == 260) m.m_inputs[0] |= 0x01;
		if (frame == 280) m.m_inputs[0] &= uint8_t(~0x10);
		if (frame == 300) m.m_inputs[0] |= 0x10;
		m.run_frame();
		if (frame == 290) assert(m.io_board_byte(8) == 0xef);
		if (m.frame_quads() > 0) {
			if (!first_geometry) first_geometry = frame;
			geometry_frames++;
		}
	}
	printf("Wing War boot: first geometry=%d, 3D frames=%d, TGP cycles=%llu\n",
		first_geometry, geometry_frames, (unsigned long long)m.tgp_cycles_executed());
	assert(m.io_board_byte(0x1a) == 'S' && m.io_board_byte(0x1b) == 'E' &&
		m.io_board_byte(0x1c) == 'G' && m.io_board_byte(0x1d) == 'A');
	assert(first_geometry > 0 && first_geometry < 650);
	assert(geometry_frames > 100);
	assert(m.advanced_ioboard_cycles() > 0);
	assert(m.tgp_cycles_executed() > 0);
	assert(m.m_tgp_missing.empty());
	puts("Wing War interpreter boot and Advanced I/O board passed");
}
