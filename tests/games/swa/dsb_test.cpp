// Star Wars Arcade's Digital Sound Board: the i8251's pacing, receiver enable and overrun, and the
// MPEG registers' latching, on a stub program; then the real board in the attract sequence.
#include "../../../src/games/swa/controls.h"
#include "../../../src/host/regions.h"
#include <cassert>
#include <cmath>
#include <cstdio>

namespace {

using namespace game::swa;

void out(dsbz80 &d, uint8_t port, uint32_t v)
{
	d.io_w(port, uint8_t(v >> 16));
	d.io_w(port + 1, uint8_t(v >> 8));
	d.io_w(port + 2, uint8_t(v));
}

void stub()
{
	const std::vector<uint8_t> rom = { 0xf3, 0x76 };   // di; halt
	const std::vector<uint8_t> mpeg(0x1000, 0);        // nothing decodable
	dsbz80 d(rom, mpeg);
	std::vector<float> l, r;
	const double byte_us = dsbz80::BYTE_CYCLES * 1e6 / dsbz80::CLOCK;
	assert(byte_us == 320);   // 31.25 kbaud, 8N1

	// receiver off: the character is lost
	d.rx_byte(0x12);
	d.run(1000, l, r);
	assert(d.bytes_received() == 0 && !(d.io_r(0xf1) & 0x02));
	// the ROM's init: three dummies, internal reset, mode 0x4e, command 0x37 (RxE)
	for (uint8_t v : { 0x00, 0x00, 0x00, 0x40, 0x4e, 0x37 }) d.io_w(0xf1, v);

	// one character time per byte
	d.rx_byte(0x34);
	d.rx_byte(0x56);
	d.run(byte_us - 20, l, r);
	assert(!(d.io_r(0xf1) & 0x02));
	d.run(40, l, r);
	assert(d.io_r(0xf1) == 0x87 && d.io_r(0xf0) == 0x34 && d.io_r(0xf1) == 0x85);
	d.run(byte_us, l, r);
	assert(d.io_r(0xf0) == 0x56);
	// overrun: an unread byte is replaced, OE set until an error reset
	d.rx_byte(0x78);
	d.rx_byte(0x9a);
	d.run(3 * byte_us, l, r);
	assert(d.io_r(0xf1) & 0x10);
	assert(d.io_r(0xf0) == 0x9a && d.bytes_received() == 4);
	d.io_w(0xf1, 0x37 | 0x10);
	assert(d.io_r(0xf1) == 0x85);

	// volume (inverted, 7 bits) and pan
	d.io_w(0xe8, 0xc4);
	d.io_w(0xe9, 0x06);
	assert(d.volume() == 0x3b && d.pan() == 2);
	d.io_w(0xe9, 0);

	// idle: start and end go straight in; play once from start, stop at undecodable data
	out(d, 0xe2, 0x000100);
	out(d, 0xe5, 0x000800);
	d.io_w(0xe0, 1);
	assert(d.state() == 1 && d.position() == 0x100);
	assert(d.io_r(0xe2) == 0x00 && d.io_r(0xe3) == 0x01 && d.io_r(0xe4) == 0x00);
	d.run(100, l, r);
	assert(d.state() == 0 && d.frames_decoded() == 0);
	// looped: writes while playing are latched, and the loop restarts there
	d.io_w(0xe0, 2);
	assert(d.position() == 0x100);
	out(d, 0xe2, 0x000200);
	assert(d.position() == 0x100);
	d.run(100, l, r);
	assert(d.state() == 0 && d.position() == 0x200);   // undecodable at the loop point too: stop
	// stop
	d.io_w(0xe0, 0);
	assert(d.state() == 0);
	for (float s : l) assert(s == 0);
	for (float s : r) assert(s == 0);
	// 32 kHz output, whatever is playing
	l.clear();
	r.clear();
	d.run(10000, l, r);
	assert(l.size() >= 319 && l.size() <= 321 && r.size() == l.size());
	puts("SWA DSB: i8251 pacing, RxE and overrun; MPEG start/end latching, loop, volume and pan");
}

// The attract's opening crawl: the STAR WARS logo comes up with the theme, an MPEG-1 Layer II stream
// the DSB plays from 0x334000 at 32 kHz. The other boards are silent under it, so the mixed output
// is the DSB's alone and must match the board's own decode, resampled to 48 kHz.
void attract(const char *dir)
{
	model1 m(load_regions(dir, ID));
	m.enable_sound();
	m.reset();
	m.run_cpu = [&](int cycles) { m.cpu().run(cycles); };
	for (int c : { 0, 1, 2, 4, 5 }) assert(m.set_analog_input(c, IO_PROFILE.analog_neutral[c]));
	dsbz80 *d = m.audio()->dsb();
	assert(d);
	constexpr uint64_t CYCLES_PER_FRAME = 656 * 424;
	constexpr int FRAMES = 4200, WINDOW = 3600;   // the comparison: the last 600 frames
	int music = 0, logo = 0;
	uint64_t samples = 0, playing_frames = 0, window_stream = 0;
	std::vector<int16_t> window;
	for (int f = 1; f <= FRAMES; f++) {
		m.run_frame();
		samples += m.m_audio_out.size() / 2;
		if (f > WINDOW) window.insert(window.end(), m.m_audio_out.begin(), m.m_audio_out.end());
		m.m_audio_out.clear();
		if (!music && d->state()) {
			music = f;
			d->take_peak();
		}
		if (!music) assert(d->frames_decoded() == 0);   // the stream's samples are counted from its start
		if (music) playing_frames++;
		if (f == WINDOW) window_stream = d->stream_samples();
		if (!logo && f > 3000) {
			int yellow = 0;
			for (size_t i = 0; i < size_t(m.frame_w()) * m.frame_h(); i++) {
				const uint32_t px = m.frame().data()[i];
				yellow += (px >> 16 & 255) > 180 && (px >> 8 & 255) > 140 && (px & 255) < 90;
			}
			if (yellow > 2000) logo = f;
		}
	}
	const float peak = d->take_peak();
	assert(d->bytes_received() >= 4 && d->state() == 1 && d->volume() == 0x3b && d->pan() == 0);
	assert(music > 3000 && logo > 3000 && std::abs(logo - music) <= 5);
	// 1152 samples per Layer II frame at 32 kHz, against Model 1's 16 MHz frame clock
	const double expected = playing_frames * double(CYCLES_PER_FRAME) / 16e6 * dsbz80::RATE / 1152;
	const uint64_t decoded = d->frames_decoded();
	assert(decoded >= expected - 2 && decoded <= expected + 2);
	assert(d->stream_rate() == dsbz80::RATE && d->stream_channels() == 2);
	assert(d->position() >= 0x334000 && d->position() < 0x3f4000);
	assert(peak > 1000);
	const uint64_t expected_samples = CYCLES_PER_FRAME * FRAMES * m1audio::OUT_RATE / 16000000;
	assert(samples >= expected_samples - 1 && samples <= expected_samples + 1);

	// the reference: the stream decoded on its own, at the board's volume
	auto regions = load_regions(dir, ID);
	mpeg_audio decoder(regions.mpeg.data(), mpeg_audio::L2, false, 0);
	std::vector<float> ref;
	int pos = 0x334000 * 8, n, rate, channels;
	short buf[1152 * 2];
	while (ref.size() / 2 < window_stream + window.size() && decoder.decode_buffer(pos, 0x3f4000 * 8, buf, n, rate, channels))
		for (int i = 0; i < n * 2; i++) ref.push_back(buf[i] * (0x3b / 128.0f));
	// the window's first output is a few stream samples before the board's count (the resampler's lead)
	const size_t outputs = window.size() / 2;
	const double step = double(dsbz80::RATE) / m1audio::OUT_RATE;
	double best_error = 1e300, best_q = 0, power = 0;
	auto fit = [&](double q) {
		double error = 0, p = 0;
		for (size_t k = 0; k < outputs; k++) {
			const double t = q + k * step;
			const size_t i = size_t(t);
			const double frac = t - i;
			for (int c = 0; c < 2; c++) {
				const double r = ref[2 * i + c] + (ref[2 * (i + 1) + c] - ref[2 * i + c]) * frac;
				const double e = window[2 * k + c] - r;
				error += e * e;
				p += r * r;
			}
		}
		if (error < best_error) best_error = error, best_q = q, power = p;
	};
	for (double q = double(window_stream) - 8; q <= double(window_stream) + 2; q += 0.05) fit(q);
	const double coarse = best_q;
	for (double q = coarse - 0.05; q <= coarse + 0.05; q += 0.002) fit(q);
	const double snr = 10 * std::log10(power / best_error);
	printf("SWA DSB attract: %llu bytes, theme from frame %d (logo %d), %llu MPEG frames (%.1f expected), "
			"%d Hz %d ch, volume %02x, peak %.0f; output vs decode %.1f dB (lead %.2f samples)\n",
		(unsigned long long)d->bytes_received(), music, logo, (unsigned long long)decoded, expected,
		d->stream_rate(), d->stream_channels(), d->volume(), peak, snr, window_stream - best_q);
	assert(snr > 60);
	puts("SWA DSB attract: the theme starts with the logo and plays the MPEG stream faithfully at 48 kHz");
}

}

int main(int argc, char **argv)
{
	stub();
	if (argc != 2) {
		fprintf(stderr, "usage: %s regions-dir (requires SWA and Model 1 I/O ROMs)\n", argv[0]);
		return 2;
	}
	attract(argv[1]);
}
