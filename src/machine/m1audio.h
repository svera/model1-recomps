// Sega Model 1 sound board: 68000 at 10 MHz, YM3438 at 8 MHz, two MultiPCMs (315-5560) and an
// i8251 UART on the serial link from the main board. Follows MAME's segam1audio.cpp
// (BSD-3-Clause, copyright-holders: R. Belmont). The 68000 is Musashi, the YM3438 is ymfm.
#pragma once
#include "multipcm.h"
#include "dsbz80.h"
#include <deque>
#include <vector>

class m1audio
{
public:
	static constexpr int OUT_RATE = 48000;

	m1audio(std::vector<uint8_t> &sndcpu, std::vector<uint8_t> &pcm1, std::vector<uint8_t> &pcm2);
	~m1audio();
	void reset();

	// Runs the board for `usec` microseconds, appending stereo samples at OUT_RATE to `out`.
	void run(double usec, std::vector<int16_t> &out);

	// VF plays its music on the first MultiPCM and its effects and voices on the second (the FM
	// chip stays silent): each can be turned down on its own. 1 = as the cabinet.
	void set_volumes(float music, float effects) { m_music = music; m_effects = effects; }

	// Star Wars Arcade's Digital Sound Board: the 68000's serial output also feeds its Z80 (as
	// MAME: every byte, the DSB's program picks out its own commands), and its MPEG playback is
	// mixed in at MAME's gain of 1, under the music volume.
	void attach_dsb(const std::vector<uint8_t> &cpu_rom, const std::vector<uint8_t> &mpeg_rom);
	dsbz80 *dsb() { return m_dsb.get(); }

	// Serial link, one byte at a time (the main board's i8251 paces it).
	void rx_byte(uint8_t b) { m_rx.push_back(b); }
	bool tx_pending() const { return !m_tx.empty(); }
	uint8_t tx_byte() { uint8_t b = m_tx.front(); m_tx.pop_front(); return b; }

	// Musashi's memory callbacks land here (one board per process).
	uint8_t read8(uint32_t a);
	void write8(uint32_t a, uint8_t v);
	uint16_t read16(uint32_t a);
	void write16(uint32_t a, uint16_t v);
	int irq_ack(int level);

private:
	struct ym_glue;
	std::vector<uint8_t> &m_rom, &m_pcm1_rom, &m_pcm2_rom;
	std::vector<uint8_t> m_ram;
	multipcm_device m_pcm1{10000000}, m_pcm2{10000000};
	std::unique_ptr<ym_glue> m_ym;
	std::unique_ptr<dsbz80> m_dsb;
	uint32_t m_bank1 = 0, m_bank2 = 0;
	float m_music = 1, m_effects = 1;

	// the board's i8251
	std::deque<uint8_t> m_rx, m_tx;
	int m_rx_latch = -1;
	bool m_uart_mode_next = true;
	uint8_t m_uart_cmd = 0;
	void update_irq();

	// chip output, resampled to OUT_RATE
	double m_pcm_phase = 0, m_ym_phase = 0, m_out_phase = 0;
	double m_68k_carry = 0;
	struct src_buf { std::vector<float> l, r; double pos = 0; } m_pcm_buf, m_ym_buf, m_dsb_buf;
	static constexpr int DSB_LEAD = 2;
	std::vector<int16_t> m_tmp[4];   // the two MultiPCMs' samples for a slice (left, right each)
};
