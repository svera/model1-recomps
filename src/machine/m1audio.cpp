#include "m1audio.h"
#include "../../third_party/ymfm/ymfm_opn.h"
extern "C" {
#include "../../third_party/musashi/m68k.h"
}
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace {
m1audio *g_board;
constexpr double CPU_HZ = 10000000.0;      // 20 MHz / 2
constexpr double YM_HZ = 8000000.0;        // 16 MHz / 2
}

extern "C" {
unsigned int m68k_read_memory_8(unsigned int a) { return g_board->read8(a & 0xffffff); }
unsigned int m68k_read_memory_16(unsigned int a) { return g_board->read16(a & 0xffffff); }
unsigned int m68k_read_memory_32(unsigned int a) { return (g_board->read16(a & 0xffffff) << 16) | g_board->read16((a + 2) & 0xffffff); }
void m68k_write_memory_8(unsigned int a, unsigned int v) { g_board->write8(a & 0xffffff, uint8_t(v)); }
void m68k_write_memory_16(unsigned int a, unsigned int v) { g_board->write16(a & 0xffffff, uint16_t(v)); }
void m68k_write_memory_32(unsigned int a, unsigned int v) { g_board->write16(a & 0xffffff, uint16_t(v >> 16)); g_board->write16((a + 2) & 0xffffff, uint16_t(v)); }
unsigned int m68k_read_disassembler_8(unsigned int a) { return g_board->read8(a & 0xffffff); }
unsigned int m68k_read_disassembler_16(unsigned int a) { return g_board->read16(a & 0xffffff); }
unsigned int m68k_read_disassembler_32(unsigned int a) { return (g_board->read16(a & 0xffffff) << 16) | g_board->read16((a + 2) & 0xffffff); }
unsigned char m68k_watch[M68K_WATCH_LIMIT / 2];   // a busy loop's number + 1 at its head
void m68k_watch_hit(unsigned int pc) { g_board->watch_hit(pc); }
}

// ymfm's host interface: the timers count in YM clocks and set the status flags when they fire.
struct m1audio::ym_glue : ymfm::ymfm_interface
{
	ymfm::ym3438 chip{*this};
	int64_t timer[2] = { -1, -1 };
	void ymfm_set_timer(uint32_t tnum, int32_t clocks) override { timer[tnum] = clocks; }
	void advance(int64_t clocks)
	{
		for (int t = 0; t < 2; t++)
			if (timer[t] >= 0 && (timer[t] -= clocks) < 0) {
				timer[t] = -1;
				m_engine->engine_timer_expired(t);
			}
	}
};

m1audio::m1audio(std::vector<uint8_t> &sndcpu, std::vector<uint8_t> &pcm1, std::vector<uint8_t> &pcm2)
	: m_rom(sndcpu), m_pcm1_rom(pcm1), m_pcm2_rom(pcm2), m_ram(0x10000), m_ym(std::make_unique<ym_glue>())
{
	if (g_board) m68k_get_context(g_board->m_cpu_context.data());
	m_pcm1.read_byte = [this](uint32_t a) -> uint8_t {
		a &= 0x3fffff;
		uint32_t off = a < 0x100000 ? a : m_bank1 * 0x100000 + (a & 0xfffff);
		return off < m_pcm1_rom.size() ? m_pcm1_rom[off] : 0;
	};
	m_pcm2.read_byte = [this](uint32_t a) -> uint8_t {
		a &= 0x3fffff;
		uint32_t off = a < 0x100000 ? a : m_bank2 * 0x100000 + (a & 0xfffff);
		return off < m_pcm2_rom.size() ? m_pcm2_rom[off] : 0;
	};
	m_pcm1.start();
	m_pcm2.start();
	m68k_init();
	m68k_set_cpu_type(M68K_CPU_TYPE_68000);
	m_cpu_context.resize(m68k_context_size());
	m68k_get_context(m_cpu_context.data());
	g_board = this;
	m_idle_skip = !getenv("M1_NO_SOUND_IDLE_SKIP");
}

void m1audio::attach_dsb(const std::vector<uint8_t> &cpu_rom, const std::vector<uint8_t> &mpeg_rom)
{
	m_dsb = std::make_unique<dsbz80>(cpu_rom, mpeg_rom);
}

m1audio::~m1audio() { if (g_board == this) g_board = nullptr; }

void m1audio::activate()
{
	if (g_board == this) return;
	if (g_board) m68k_get_context(g_board->m_cpu_context.data());
	m68k_set_context(m_cpu_context.data());
	g_board = this;
}

void m1audio::reset()
{
	activate();
	std::fill(m_ram.begin(), m_ram.end(), 0);
	m_rx.clear(); m_tx.clear();
	m_rx_latch = -1;
	m_uart_mode_next = true;
	m_uart_cmd = 0;
	m_bank1 = m_bank2 = 0;
	m_pcm1.reset();
	m_pcm2.reset();
	m_ym->chip.reset();
	m68k_pulse_reset();
	m68k_set_irq(0);
	if (m_dsb) m_dsb->reset();
	// Two samples of lead (62.5 us): the Z80 delivers samples at instruction boundaries, and
	// without it the slice's last interpolation would often need a sample not produced yet.
	m_dsb_buf.l.assign(DSB_LEAD, 0); m_dsb_buf.r.assign(DSB_LEAD, 0); m_dsb_buf.pos = 0;
	find_idle_loops();
}

// ---- the sound program's busy loops ----
//
// Most of the 68000's time goes round loops that wait. Wing War and Star Wars Arcade check 28
// voice slots, twice, between looks at the YM's timer, and nearly all of the slots are empty;
// Virtua Racing just reads the timer until it fires. A slice of the board (a scanline, ~630
// cycles) sees nothing from outside change: the chips are run after the 68000, the UART's byte
// arrives before it, and Musashi only takes an interrupt when the slice starts or the 68000
// itself changes something. So a loop's passes can be counted off instead of run, cycle for
// cycle, as long as one is left to end the slice on: the 68000 is then where it would have been.

void m1audio::find_idle_loops()
{
	m_loops.clear();
	const size_t end = std::min<size_t>(m_rom.size(), M68K_WATCH_LIMIT) & ~size_t(1);
	auto w16 = [&](size_t a) { return uint32_t(m_rom[a] << 8 | m_rom[a + 1]); };
	for (size_t h = 0; h + 12 <= end && m_loops.size() < 255; h += 2) {
		// btst #bit,(offset,A4); beq.w next  ...  next: adda.l #stride,A4; dbra D4,h
		if (w16(h) == 0x082c && w16(h + 2) < 8 && w16(h + 6) == 0x6700) {
			const size_t next = h + 8 + int16_t(w16(h + 8));
			if (next + 10 <= end && w16(next) == 0xd9fc && w16(next + 6) == 0x51cc &&
				next + 8 + int16_t(w16(next + 8)) == h) {
				idle_loop l;
				l.kind = idle_loop::SCAN;
				l.pc = uint32_t(h);
				l.bit = uint8_t(w16(h + 2));
				l.offset = int16_t(w16(h + 4));
				l.stride = w16(next + 2) << 16 | w16(next + 4);
				m_loops.push_back(l);
			}
		}
		// movea.l #$d00001,A0; move.b (A0),D0; lsr.w #1,D0; bcc.s/bcs.s back: the YM's timer
		if (w16(h) == 0x207c && w16(h + 2) == 0x00d0 && w16(h + 4) == 0x0001 && w16(h + 6) == 0x1010 &&
			w16(h + 8) == 0xe248 && (w16(h + 10) == 0x64f4 || w16(h + 10) == 0x65f4)) {
			idle_loop l;
			l.kind = idle_loop::SPIN;
			l.pc = uint32_t(h);
			m_loops.push_back(l);
		}
	}
}

void m1audio::watch_loops()
{
	static std::vector<uint32_t> watched;   // (the boards of linked cabinets take turns)
	for (uint32_t pc : watched) m68k_watch[pc >> 1] = 0;
	watched.clear();
	if (!m_idle_skip) return;
	for (size_t i = 0; i < m_loops.size(); i++) {
		m68k_watch[m_loops[i].pc >> 1] = uint8_t(i + 1);
		watched.push_back(m_loops[i].pc);
	}
}

// Musashi calls this before the instruction at a watched loop's head.
void m1audio::watch_hit(uint32_t pc)
{
	auto &l = m_loops[m68k_watch[pc >> 1] - 1];
	const int now = m68k_cycles_run(), left = m68k_cycles_remaining();
	const bool same_slice = l.slice == m_slice;
	int skip = 0;
	if (l.kind == idle_loop::SCAN) {
		// a pass over an empty slot is btst, beq, adda and dbra: its cycles, from one seen whole
		const uint32_t a4 = m68k_get_reg(nullptr, M68K_REG_A4), d4 = m68k_get_reg(nullptr, M68K_REG_D4);
		if (!l.period && same_slice && l.empty && (l.regs[1] & 0xffff) && a4 == l.regs[0] + l.stride &&
			(d4 & 0xffff) == ((l.regs[1] - 1) & 0xffff))
			l.period = now - l.at;
		int n = 0;
		if (l.period > 0) {
			const int count = int(d4 & 0xffff);   // dbra goes back while the count stays >= 0
			while (n < count && int64_t(n + 1) * l.period < left &&
				   slot_empty((a4 + l.offset + uint32_t(n) * l.stride) & 0xffffff, l.bit))
				n++;
		}
		// (the flags the passes leave are the btst's, which the next one sets again)
		const uint32_t a = a4 + uint32_t(n) * l.stride, d = (d4 & 0xffff0000) | ((d4 - uint32_t(n)) & 0xffff);
		if (n) {
			m68k_set_reg(M68K_REG_A4, a);
			m68k_set_reg(M68K_REG_D4, d);
			skip = n * l.period;
		}
		l.regs[0] = a;
		l.regs[1] = d;
		l.empty = slot_empty((a + l.offset) & 0xffffff, l.bit);
	} else {
		// back where it was, as it was, and nothing written or taken in between: the same pass
		// again, and again, until the slice ends
		uint32_t regs[17];
		for (int i = 0; i < 16; i++) regs[i] = m68k_get_reg(nullptr, m68k_register_t(M68K_REG_D0 + i));
		regs[16] = m68k_get_reg(nullptr, M68K_REG_SR);
		const int period = now - l.at;
		if (same_slice && l.changes == m_changes && period > 0 && std::equal(regs, regs + 17, l.regs))
			skip = (left - 1) / period * period;
		std::copy(regs, regs + 17, l.regs);
	}
	if (skip) {
		m68k_modify_timeslice(-skip);
		m_skipped += skip;
		m_skipped_total += skip;
	}
	l.slice = m_slice;
	l.changes = m_changes;
	l.at = now;
}

// ---- 68000 memory map (MAME: segam1audio_map) ----

uint16_t m1audio::read16(uint32_t a)
{
	a &= 0xfffffe;
	if (a < 0x40000) return (m_rom[a] << 8) | m_rom[a + 1];
	if (a >= 0x80000 && a < 0xa0000) { uint32_t o = 0x20000 + (a - 0x80000); return (m_rom[o] << 8) | m_rom[o + 1]; }
	if (a >= 0xf00000 && a < 0xf10000) return (m_ram[a - 0xf00000] << 8) | m_ram[a - 0xf00000 + 1];
	return 0xff00 | read8(a + 1);
}

uint8_t m1audio::read8(uint32_t a)
{
	if (a < 0x40000) return m_rom[a];
	if (a >= 0x80000 && a < 0xa0000) return m_rom[0x20000 + (a - 0x80000)];
	if (a >= 0xf00000 && a < 0xf10000) return m_ram[a - 0xf00000];
	if (!(a & 1)) return 0xff;   // the chips sit on the low byte lane
	if (a >= 0xc20000 && a < 0xc20004) {
		if ((a & 2) == 0) {          // data
			uint8_t v = m_rx_latch >= 0 ? uint8_t(m_rx_latch) : 0;
			m_rx_latch = -1;
			m_changes++;
			update_irq();
			return v;
		}
		// status: TxRDY | RxRDY | TxEMPTY | DSR
		return uint8_t(0x01 | (m_rx_latch >= 0 ? 0x02 : 0) | 0x04 | 0x80);
	}
	if (a >= 0xc40000 && a < 0xc40008) return m_pcm1.read();
	if (a >= 0xc60000 && a < 0xc60008) return m_pcm2.read();
	if (a >= 0xd00000 && a < 0xd00008) return m_ym->chip.read((a >> 1) & 3);
	return 0xff;
}

void m1audio::write16(uint32_t a, uint16_t v)
{
	a &= 0xfffffe;
	m_changes++;
	if (a >= 0xf00000 && a < 0xf10000) { m_ram[a - 0xf00000] = v >> 8; m_ram[a - 0xf00000 + 1] = uint8_t(v); return; }
	if (a == 0xc50000) { m_bank1 = v & 3; return; }
	if (a == 0xc70000) { m_bank2 = v & 3; return; }
	write8(a + 1, uint8_t(v));
}

void m1audio::write8(uint32_t a, uint8_t v)
{
	m_changes++;
	if (a >= 0xf00000 && a < 0xf10000) { m_ram[a - 0xf00000] = v; return; }
	if ((a & ~1u) == 0xc50000) { if (a & 1) m_bank1 = v & 3; return; }
	if ((a & ~1u) == 0xc70000) { if (a & 1) m_bank2 = v & 3; return; }
	if (!(a & 1)) return;
	if (a >= 0xc20000 && a < 0xc20004) {
		if ((a & 2) == 0) {
			m_tx.push_back(v);
			if (m_dsb) m_dsb->rx_byte(v);
			return;
		}
		if (m_uart_mode_next) { m_uart_mode_next = false; return; }   // mode byte
		m_uart_cmd = v;
		if (v & 0x40) m_uart_mode_next = true;                       // internal reset
		return;
	}
	if (a >= 0xc40000 && a < 0xc40008) { m_pcm1.write((a >> 1) & 3, v); return; }
	if (a >= 0xc60000 && a < 0xc60008) { m_pcm2.write((a >> 1) & 3, v); return; }
	if (a >= 0xd00000 && a < 0xd00008) { m_ym->chip.write((a >> 1) & 3, v); return; }
}

// RxRDY drives the 68000's IRQ 2 (autovectored).
void m1audio::update_irq()
{
	activate();
	m68k_set_irq(m_rx_latch >= 0 ? 2 : 0);
}

int m1audio::irq_ack(int) { return M68K_INT_ACK_AUTOVECTOR; }

// ---- running and mixing ----

void m1audio::run(double usec, std::vector<int16_t> &out)
{
	activate();
	// a received byte reaches the UART's buffer when the previous one has been read
	if (m_rx_latch < 0 && !m_rx.empty()) {
		m_rx_latch = m_rx.front();
		m_rx.pop_front();
		update_irq();
	}
	double cycles = usec * CPU_HZ / 1e6 + m_68k_carry;
	int n = int(cycles);
	watch_loops();
	m_slice++;
	m_skipped = 0;
	int done = m68k_execute(n) + m_skipped;
	m_68k_carry = cycles - done;

	// chip samples for this slice
	const double pcm_rate = m_pcm1.sample_rate();
	const double ym_rate = m_ym->chip.sample_rate(uint32_t(YM_HZ));
	m_pcm_phase += usec * pcm_rate / 1e6;
	m_ym_phase += usec * ym_rate / 1e6;
	int npcm = int(m_pcm_phase), nym = int(m_ym_phase);
	m_pcm_phase -= npcm;
	m_ym_phase -= nym;
	if (npcm > 0) {
		// (kept between calls: this runs every scanline, and a new buffer each time was ~100k allocations a second)
		auto &l1 = m_tmp[0], &r1 = m_tmp[1], &l2 = m_tmp[2], &r2 = m_tmp[3];
		for (auto &t : m_tmp) t.resize(npcm);
		m_pcm1.generate(l1.data(), r1.data(), npcm);
		m_pcm2.generate(l2.data(), r2.data(), npcm);
		for (int i = 0; i < npcm; i++) {
			// MAME's routes: each MultiPCM at 0.5 (times the player's music and effects volumes)
			m_pcm_buf.l.push_back(0.5f * (m_music * l1[i] + m_effects * l2[i]));
			m_pcm_buf.r.push_back(0.5f * (m_music * r1[i] + m_effects * r2[i]));
		}
	}
	for (int i = 0; i < nym; i++) {
		ymfm::ym3438::output_data o;
		m_ym->chip.generate(&o);
		m_ym->advance(int64_t(YM_HZ / ym_rate));
		// MAME's route: 0.30
		m_ym_buf.l.push_back(0.30f * m_music * o.data[0]);
		m_ym_buf.r.push_back(0.30f * m_music * o.data[1]);
	}

	if (m_dsb) {
		size_t n0 = m_dsb_buf.l.size();
		m_dsb->run(usec, m_dsb_buf.l, m_dsb_buf.r);
		for (size_t i = n0; i < m_dsb_buf.l.size(); i++) { m_dsb_buf.l[i] *= m_music; m_dsb_buf.r[i] *= m_music; }
	}

	// resample to OUT_RATE (linear) and mix
	m_out_phase += usec * OUT_RATE / 1e6;
	int nout = int(m_out_phase);
	m_out_phase -= nout;
	auto take = [](src_buf &b, double step, int k, float &l, float &r) {
		double p = b.pos + step * k;
		size_t i = size_t(p);
		float f = float(p - i);
		if (b.l.empty()) { l = r = 0; return; }
		size_t i1 = std::min(i + 1, b.l.size() - 1);
		i = std::min(i, b.l.size() - 1);
		l = b.l[i] + (b.l[i1] - b.l[i]) * f;
		r = b.r[i] + (b.r[i1] - b.r[i]) * f;
	};
	const double pstep = pcm_rate / OUT_RATE, ystep = ym_rate / OUT_RATE;
	const double dstep = double(dsbz80::RATE) / OUT_RATE;
	for (int k = 0; k < nout; k++) {
		float pl, pr, yl, yr, dl = 0, dr = 0;
		take(m_pcm_buf, pstep, k, pl, pr);
		take(m_ym_buf, ystep, k, yl, yr);
		if (m_dsb) take(m_dsb_buf, dstep, k, dl, dr);
		out.push_back(int16_t(std::clamp(pl + yl + dl, -32768.0f, 32767.0f)));
		out.push_back(int16_t(std::clamp(pr + yr + dr, -32768.0f, 32767.0f)));
	}
	// drop consumed input, keep one sample of history for the interpolation
	auto consume = [](src_buf &b, double step, int k) {
		b.pos += step * k;
		size_t drop = b.pos >= 1 ? size_t(b.pos) - 1 + 1 : 0;
		drop = std::min(drop, b.l.size() ? b.l.size() - 1 : 0);
		b.l.erase(b.l.begin(), b.l.begin() + drop);
		b.r.erase(b.r.begin(), b.r.begin() + drop);
		b.pos -= drop;
	};
	consume(m_pcm_buf, pstep, nout);
	consume(m_ym_buf, ystep, nout);
	if (m_dsb) consume(m_dsb_buf, dstep, nout);
}
