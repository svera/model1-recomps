#include "../../../src/games/swa/profile.h"
#include "../../../src/host/regions.h"
#include <cassert>
#include <cstdio>
#include <zlib.h>

namespace {

constexpr int FRAMES = 1800;
constexpr uint64_t CYCLES_PER_FRAME = 656 * 424;

struct checkpoint
{
	uint32_t pc, picture;
	int polygons;
	uint64_t tgp_cycles, input_read, output_read, vblank_irqs, timer_irqs;
	bool operator==(const checkpoint &) const = default;
};

void interrupt_timing()
{
	model1::regions regions;
	regions.title = game::swa::ID;
	model1 m(std::move(regions));
	m.reset();
	m.set_drawing(false);
	m.run_cpu = [](int) {};
	m.write8(0xe00002, 0xfe);
	m.write16(0xe00008, 4);
	assert(m.read16(0xe0000c) == 4);
	uint64_t elapsed = 0, events = 0;
	m.on_line = [&](int) {
		const bool expired = (elapsed + 656) / 8192 != elapsed / 8192;
		elapsed += 656;
		assert(m.cpu().irq_line_up() == expired);
		assert(m.read16(0xe0000c) == (8192 - elapsed % 8192) / 2048);
		if (expired) {
			events++;
			m.write8(0xe00000, 0x10);
		}
	};
	for (int frame = 0; frame < 3; frame++) m.run_frame();
	assert(elapsed == 3 * CYCLES_PER_FRAME);
	assert(events == elapsed / 8192);
	m.write16(0xe00008, 0);
	m.write8(0xe00002, 0xfd);
	events = 0;
	m.on_line = [&](int line) {
		assert(m.cpu().irq_line_up() == (line == 384));
		if (line == 384) {
			events++;
			m.write8(0xe00000, 0x10);
		}
	};
	for (int frame = 0; frame < 3; frame++) m.run_frame();
	assert(events == 3);
	puts("SWA timing: timer IRQ at period * 2048 clocks; vblank at line 384 of 424");
}

std::vector<checkpoint> boot(const char *dir)
{
	model1 m(load_regions(dir, game::swa::ID));
	assert(m.tgp_is_lle() && !m.tgp_has_hle());
	assert(m.real_ioboard());
	assert(!m.run_cpu);
	m.enable_sound();
	m.reset();
	assert(m.tgp_cycles_executed() == 0);
	assert(m.tgp_status().input_written == 0);
	assert(m.tgp_status().output_written == 0);
	uint64_t cpu_cycles = 0, vblank_irqs = 0, timer_irqs = 0, audio_samples = 0;
	m.run_cpu = [&](int cycles) {
		cpu_cycles += cycles;
		m.cpu().run(cycles);
	};
	m.cpu().on_irq = [&](int vector, uint32_t, uint32_t) {
		if (vector == 0x41) vblank_irqs++;
		if (vector == 0x40) timer_irqs++;
	};
	int first_geometry = 0, geometry_frames = 0;
	uint64_t peak_tgp_frame = 0;
	std::vector<checkpoint> checkpoints;
	for (int frame = 1; frame <= FRAMES; frame++) {
		const uint64_t before = m.tgp_cycles_executed();
		m.run_frame();
		assert(m.frame_number() == uint64_t(frame));
		assert(cpu_cycles == CYCLES_PER_FRAME * frame);
		const auto t = m.tgp_status();
		assert(t.input_written - t.input_read == t.input_pending);
		assert(t.output_written - t.output_read == t.output_pending);
		assert(t.input_peak <= 16 && t.output_peak <= 16);
		assert(t.output_waits == 0 && t.sync_limits == 0);
		peak_tgp_frame = std::max(peak_tgp_frame, m.tgp_cycles_executed() - before);
		audio_samples += m.m_audio_out.size() / 2;
		m.m_audio_out.clear();
		if (m.frame_quads() > 0) {
			if (!first_geometry) first_geometry = frame;
			geometry_frames++;
		}
		if (frame % 300 == 0) {
			const auto &picture = m.frame();
			const uint32_t digest = uint32_t(crc32(0, reinterpret_cast<const Bytef *>(picture.data()),
				uInt(m.frame_w() * m.frame_h() * sizeof(uint32_t))));
			checkpoints.push_back({ m.cpu().pc(), digest, m.frame_quads(),
				m.tgp_cycles_executed(), t.input_read, t.output_read, vblank_irqs, timer_irqs });
			printf("SWA frame %d: pc=%06x, polygons=%d, picture=%08x, TGP cycles=%llu\n",
				frame, m.cpu().pc(), m.frame_quads(), digest, (unsigned long long)m.tgp_cycles_executed());
		}
	}
	const auto t = m.tgp_status();
	printf("SWA boot: first geometry=%d, 3D frames=%d, IRQ vblank/timer=%llu/%llu, "
			"FIFO input/output=%llu/%llu, waits=%llu/%llu, peak TGP cycles/frame=%llu\n",
		first_geometry, geometry_frames, (unsigned long long)vblank_irqs, (unsigned long long)timer_irqs,
		(unsigned long long)t.input_read, (unsigned long long)t.output_read,
		(unsigned long long)t.input_waits, (unsigned long long)t.output_waits,
		(unsigned long long)peak_tgp_frame);
	assert(first_geometry > 0 && first_geometry < 1500);
	assert(geometry_frames > 200);
	// Cold-boot baseline at the profile's eight V60 cycles per instruction.
	assert(vblank_irqs == 1788 && timer_irqs == 16);
	assert(t.input_read > 1000 && t.output_read > 1000 && t.input_waits > 1000);
	assert(t.input_pending == 0 && t.output_pending == 0 && t.waiting);
	assert(m.tgp_cycles_executed() < cpu_cycles);
	const dsbz80 *dsb = m.audio()->dsb();
	assert(dsb);
	printf("SWA DSB: bytes=%llu, triggers=%llu, MPEG frames=%llu, state=%d, volume=%02x\n",
		(unsigned long long)dsb->bytes_received(), (unsigned long long)dsb->triggers(),
		(unsigned long long)dsb->frames_decoded(), dsb->state(), dsb->volume());
	assert(m.m_tgp_missing.empty());
	const uint64_t expected_samples = CYCLES_PER_FRAME * FRAMES * m1audio::OUT_RATE / 16000000;
	assert(audio_samples >= expected_samples - 1 && audio_samples <= expected_samples + 1);
	return checkpoints;
}

}

int main(int argc, char **argv)
{
	if (argc != 2) {
		fprintf(stderr, "usage: %s regions-dir (requires SWA and Model 1 I/O ROMs)\n", argv[0]);
		return 2;
	}
	interrupt_timing();
	const auto first = boot(argv[1]);
	const auto second = boot(argv[1]);
	assert(first == second);
	puts("SWA interpreter boot: repeated cold boots, FIFO exchange, IRQ timing and audio pacing passed");
}
