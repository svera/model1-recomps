// Sega Z80 Digital Sound Board (Star Wars Arcade on Model 1): a Z80 with 32 KB of RAM, an i8251
// receiving the sound board's serial stream, and an MPEG-1 Layer II decoder playing from its own
// ROMs at 32 kHz. Follows MAME's sega/dsbz80.cpp (BSD-3-Clause, copyright-holders: R. Belmont,
// Olivier Galibert). The Z80 is superzazu/z80 (MIT); the decoder is mpeg_audio.*.
#pragma once
#include "mpeg_audio.h"
extern "C" {
#include "../../third_party/z80/z80.h"
}
#include <cstdint>
#include <deque>
#include <memory>
#include <vector>

class dsbz80
{
public:
	static constexpr double CLOCK = 4000000;   // MAME's guess
	static constexpr int RATE = 32000;
	// 500 kHz UART clock, x16 mode, 8N1: 10 bits at 31.25 kbaud
	static constexpr double BYTE_CYCLES = 10 * 16 * CLOCK / 500000;

	dsbz80(const std::vector<uint8_t> &cpu_rom, const std::vector<uint8_t> &mpeg_rom);
	void reset();

	// A byte from the sound board's UART; it lands in the i8251 one character time later.
	void rx_byte(uint8_t b) { m_rx.push_back(b); }

	// Runs the board for `usec`, appending stereo output at RATE (int16 scale, before mixing).
	void run(double usec, std::vector<float> &l, std::vector<float> &r);

	// Diagnostics (tests)
	uint64_t bytes_received() const { return m_bytes_received; }
	uint64_t frames_decoded() const { return m_frames_decoded; }
	uint64_t triggers() const { return m_triggers; }
	uint64_t stream_samples() const { return m_stream_samples; }   // decoded samples played
	int state() const { return m_mp_state; }
	uint8_t volume() const { return m_mp_vol; }
	uint8_t pan() const { return m_mp_pan; }
	uint32_t position() const { return uint32_t(m_mp_pos) >> 3; }
	int stream_rate() const { return m_stream_rate; }
	int stream_channels() const { return m_stream_channels; }
	float take_peak() { float p = m_peak; m_peak = 0; return p; }

	uint8_t mem_r(uint16_t a) const { return a < 0x8000 ? m_rom[a] : m_ram[a - 0x8000]; }
	void mem_w(uint16_t a, uint8_t v) { if (a >= 0x8000) m_ram[a - 0x8000] = v; }
	uint8_t io_r(uint8_t port);
	void io_w(uint8_t port, uint8_t v);

private:
	std::vector<uint8_t> m_rom, m_mpeg;
	uint8_t m_ram[0x8000] = {};
	z80 m_cpu;
	double m_carry = 0, m_sample_phase = 0;

	// the i8251 (receive side; SWA never transmits)
	std::deque<uint8_t> m_rx;
	int m_rx_latch = -1;
	bool m_overrun = false;
	bool m_uart_mode_next = true;
	uint8_t m_uart_cmd = 0;
	double m_rx_ready_at = 0;   // Z80 cycle when the next character finishes arriving
	void uart_tick();

	// the MPEG side (names as MAME's)
	std::unique_ptr<mpeg_audio> m_decoder;
	uint32_t m_mp_start = 0, m_mp_end = 0, m_lp_start = 0, m_lp_end = 0, m_start = 0, m_end = 0;
	int m_mp_pos = 0;
	uint8_t m_mp_vol = 0x7f, m_mp_pan = 0;
	int m_mp_state = 0;
	short m_audio_buf[1152 * 2] = {};
	int m_audio_pos = 0, m_audio_avail = 0;
	void sample(float &l, float &r);

	uint64_t m_bytes_received = 0, m_frames_decoded = 0, m_triggers = 0, m_stream_samples = 0;
	int m_stream_rate = 0, m_stream_channels = 0;
	float m_peak = 0;
};
