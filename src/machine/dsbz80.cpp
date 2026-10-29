#include "dsbz80.h"
#include <algorithm>
#include <cmath>

namespace {
uint8_t z_read(void *u, uint16_t a) { return static_cast<dsbz80 *>(u)->mem_r(a); }
void z_write(void *u, uint16_t a, uint8_t v) { static_cast<dsbz80 *>(u)->mem_w(a, v); }
uint8_t z_in(z80 *z, uint8_t p) { return static_cast<dsbz80 *>(z->userdata)->io_r(p); }
void z_out(z80 *z, uint8_t p, uint8_t v) { static_cast<dsbz80 *>(z->userdata)->io_w(p, v); }
constexpr double SAMPLE_CYCLES = dsbz80::CLOCK / dsbz80::RATE;
}

dsbz80::dsbz80(const std::vector<uint8_t> &cpu_rom, const std::vector<uint8_t> &mpeg_rom)
	: m_rom(cpu_rom), m_mpeg(mpeg_rom)
{
	m_rom.resize(0x8000, 0xff);   // only the first 32 KB is mapped
	m_mpeg.resize(std::max<size_t>(m_mpeg.size(), 0x800000), 0);
	m_decoder = std::make_unique<mpeg_audio>(m_mpeg.data(), mpeg_audio::L2, false, 0);
	reset();
}

void dsbz80::reset()
{
	z80_init(&m_cpu);
	m_cpu.read_byte = z_read;
	m_cpu.write_byte = z_write;
	m_cpu.port_in = z_in;
	m_cpu.port_out = z_out;
	m_cpu.userdata = this;
	std::fill(std::begin(m_ram), std::end(m_ram), 0);
	m_carry = m_sample_phase = 0;
	m_rx.clear();
	m_rx_latch = -1;
	m_overrun = false;
	m_uart_mode_next = true;
	m_uart_cmd = 0;
	m_rx_ready_at = 0;
	m_decoder->clear();
	// MAME's device_reset (the other MPEG registers keep their power-on zeroes)
	m_start = m_end = 0;
	m_audio_pos = m_audio_avail = 0;
	std::fill(std::begin(m_audio_buf), std::end(m_audio_buf), 0);
	m_mp_vol = 0x7f;
	m_mp_state = 0;
	m_mp_start = m_mp_end = m_lp_start = m_lp_end = 0;
	m_mp_pos = 0;
	m_mp_pan = 0;
	m_bytes_received = m_frames_decoded = m_triggers = m_stream_samples = 0;
	m_stream_rate = m_stream_channels = 0;
	m_peak = 0;
}

// ---- i8251 ----

void dsbz80::uart_tick()
{
	double now = double(m_cpu.cyc);
	if (m_rx.empty()) {
		// an idle line: the next byte needs a full character time from now
		m_rx_ready_at = std::max(m_rx_ready_at, now + BYTE_CYCLES);
		return;
	}
	if (now < m_rx_ready_at) return;
	uint8_t b = m_rx.front();
	m_rx.pop_front();
	m_rx_ready_at = now + BYTE_CYCLES;
	if (!(m_uart_cmd & 0x04)) return;   // receiver disabled (RxE): the character is lost
	if (m_rx_latch >= 0) m_overrun = true;
	m_rx_latch = b;
	m_bytes_received++;
}

// ---- Z80 I/O map (MAME: dsbz80io_map) ----

uint8_t dsbz80::io_r(uint8_t port)
{
	switch (port) {
	case 0xe2: return uint8_t(uint32_t(m_mp_pos) >> 19);
	case 0xe3: return uint8_t(uint32_t(m_mp_pos) >> 11);
	case 0xe4: return uint8_t(uint32_t(m_mp_pos) >> 3);
	case 0xf0: {
		uint8_t v = m_rx_latch >= 0 ? uint8_t(m_rx_latch) : 0;
		m_rx_latch = -1;
		return v;
	}
	case 0xf1:   // TxRDY | RxRDY | TxEMPTY | OE | DSR
		return uint8_t(0x01 | (m_rx_latch >= 0 ? 0x02 : 0) | 0x04 | (m_overrun ? 0x10 : 0) | 0x80);
	}
	return 0xff;
}

void dsbz80::io_w(uint8_t port, uint8_t v)
{
	switch (port) {
	case 0xe0:   // trigger: 0 stop, 1 play, 2 play looped
		m_mp_state = v;
		m_triggers++;
		if (v == 0) m_audio_pos = m_audio_avail = 0;
		else if (v == 1 || v == 2) m_mp_pos = int(m_mp_start * 8);
		return;
	case 0xe2: m_start = (m_start & 0x00ffff) | uint32_t(v) << 16; return;
	case 0xe3: m_start = (m_start & 0xff00ff) | uint32_t(v) << 8; return;
	case 0xe4:
		m_start = (m_start & 0xffff00) | v;
		// while playing, start and end are latched for when the current stream ends
		if (m_mp_state == 0) m_mp_start = m_start;
		else m_lp_start = m_start;
		return;
	case 0xe5: m_end = (m_end & 0x00ffff) | uint32_t(v) << 16; return;
	case 0xe6: m_end = (m_end & 0xff00ff) | uint32_t(v) << 8; return;
	case 0xe7:
		m_end = (m_end & 0xffff00) | v;
		if (m_mp_state == 0) m_mp_end = m_end;
		else m_lp_end = m_end;
		return;
	case 0xe8: m_mp_vol = ~v & 0x7f; return;   // (the top bit's use is unknown)
	case 0xe9: m_mp_pan = v & 3; return;       // 0 stereo, 1 left on both, 2 right on both
	case 0xf0: return;                          // transmit: not connected on SWA
	case 0xf1:
		if (m_uart_mode_next) { m_uart_mode_next = false; return; }   // mode byte
		m_uart_cmd = v;
		if (v & 0x10) m_overrun = false;                             // error reset
		if (v & 0x40) m_uart_mode_next = true;                       // internal reset
		return;
	}
}

// ---- the MPEG stream (MAME: sound_stream_update, one sample at a time) ----

void dsbz80::sample(float &l, float &r)
{
	l = r = 0;
	while (m_audio_pos >= m_audio_avail) {
		if (m_mp_state == 0) return;
		int rate, channels;
		int limit = int(std::min<uint64_t>(uint64_t(m_mp_end) * 8, uint64_t(m_mpeg.size()) * 8));
		if (m_decoder->decode_buffer(m_mp_pos, limit, m_audio_buf, m_audio_avail, rate, channels)) {
			m_audio_pos = 0;
			m_frames_decoded++;
			m_stream_rate = rate;   // (played at RATE regardless, as MAME)
			m_stream_channels = channels;
		} else if (m_mp_state == 2) {
			// looping: carry on from the latched start (and end, if one was given)
			if (m_mp_pos == int(m_lp_start * 8)) m_mp_state = 0;   // looping on undecodable data
			m_mp_pos = int(m_lp_start * 8);
			if (m_lp_end) m_mp_end = m_lp_end;
		} else {
			m_mp_state = 0;
		}
	}
	const float g = m_mp_vol / 128.0f;
	const short *s = &m_audio_buf[m_audio_pos * 2];
	switch (m_mp_pan) {
	case 0: l = s[0] * g; r = s[1] * g; break;
	case 1: l = r = s[0] * g; break;
	case 2: l = r = s[1] * g; break;
	}
	m_audio_pos++;
	m_stream_samples++;
	m_peak = std::max({ m_peak, std::abs(l), std::abs(r) });
}

void dsbz80::run(double usec, std::vector<float> &l, std::vector<float> &r)
{
	double target = usec * CLOCK / 1e6 + m_carry;
	unsigned long start = m_cpu.cyc;
	while (double(m_cpu.cyc - start) < target) {
		uart_tick();
		// RxRDY holds the Z80's INT line
		if (m_rx_latch >= 0) z80_gen_int(&m_cpu, 0xff);
		else m_cpu.int_pending = false;
		unsigned long before = m_cpu.cyc;
		z80_step(&m_cpu);
		m_sample_phase += double(m_cpu.cyc - before);
		while (m_sample_phase >= SAMPLE_CYCLES) {
			m_sample_phase -= SAMPLE_CYCLES;
			float sl, sr;
			sample(sl, sr);
			l.push_back(sl);
			r.push_back(sr);
		}
	}
	m_carry = target - double(m_cpu.cyc - start);
}
