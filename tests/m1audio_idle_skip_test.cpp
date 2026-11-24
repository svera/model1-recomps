// The sound board's busy-loop skipping: a sound program with both loop shapes (a scan of
// mostly empty slots, a spin on the YM's timer A) and a serial interrupt sends the same bytes
// and plays the same samples with the skipping on and off, and the skipping does skip.
#include "../src/machine/m1audio.h"
#include <cassert>
#include <cstdio>

static const uint16_t program[] = {
	// 0x400
	0x2a7c, 0x00f0, 0x0100,          // movea.l #$f00100,A5    the slots, 12 bytes apart
	0x7e00,                          // moveq #0,D7
	0x7c00,                          // moveq #0,D6
	0x13fc, 0x0024, 0x00d0, 0x0001,  // YM timer A = $3c0, then load, enable and reset it
	0x13fc, 0x00f0, 0x00d0, 0x0003,
	0x13fc, 0x0027, 0x00d0, 0x0001,
	0x13fc, 0x0015, 0x00d0, 0x0003,
	0x46fc, 0x2000,                  // move.w #$2000,SR
	// 0x42e main:
	0x284d,                          // movea.l A5,A4
	0x383c, 0x001b,                  // move.w #27,D4
	// 0x434 scan:
	0x082c, 0x0000, 0x0000,          // btst #0,(0,A4)
	0x6700, 0x000a,                  // beq.w next
	0x5246,                          // addq.w #1,D6
	0x08ac, 0x0000, 0x0000,          // bclr #0,(0,A4)
	// 0x446 next:
	0xd9fc, 0x0000, 0x000c,          // adda.l #12,A4
	0x51cc, 0xffe6,                  // dbra D4,scan
	// 0x450 spin:
	0x207c, 0x00d0, 0x0001,          // movea.l #$d00001,A0
	0x1010,                          // move.b (A0),D0
	0xe248,                          // lsr.w #1,D0
	0x64f4,                          // bcc.s spin
	0x13fc, 0x0027, 0x00d0, 0x0001,  // reset timer A's flag
	0x13fc, 0x0015, 0x00d0, 0x0003,
	0x5287,                          // addq.l #1,D7
	0x0807, 0x0002,                  // btst #2,D7
	0x6708,                          // beq.s +8
	0x08f9, 0x0000, 0x00f0, 0x0148,  // bset #0,($f00148).l   fill slot 6
	0x13c6, 0x00c2, 0x0001,          // move.b D6,($c20001).l  send
	0x13c7, 0x00c2, 0x0001,          // move.b D7,($c20001).l
	0x6000, 0xffa4,                  // bra.w main
	// 0x48c the UART's interrupt:
	0x1a39, 0x00c2, 0x0001,          // move.b ($c20001).l,D5
	0x08f9, 0x0000, 0x00f0, 0x0124,  // bset #0,($f00124).l   fill slot 3
	0x4e73,                          // rte
};

struct result { std::vector<uint8_t> tx; std::vector<int16_t> out; uint64_t skipped; };

static result play(bool skip)
{
	std::vector<uint8_t> rom(0x40000), pcm1, pcm2;
	auto put32 = [&](size_t a, uint32_t v) { rom[a] = v >> 24; rom[a + 1] = v >> 16; rom[a + 2] = v >> 8; rom[a + 3] = v; };
	put32(0, 0xf0fff0);
	put32(4, 0x400);
	put32(0x68, 0x48c);   // autovector 2
	for (size_t i = 0; i < sizeof program / sizeof *program; i++) {
		rom[0x400 + i * 2] = program[i] >> 8;
		rom[0x401 + i * 2] = uint8_t(program[i]);
	}
	result r;
	{
		m1audio board(rom, pcm1, pcm2);
		board.reset();
		board.set_idle_skip(skip);
		for (int i = 0; i < 3000; i++) {
			if (i % 37 == 0) board.rx_byte(uint8_t(i));
			board.run(20 + (i * 7919) % 180, r.out);   // uneven slices, 20-200 us
			while (board.tx_pending()) r.tx.push_back(board.tx_byte());
		}
		r.skipped = board.idle_cycles_skipped();
	}
	return r;
}

int main()
{
	const result on = play(true), off = play(false);
	assert(off.skipped == 0);
	assert(on.skipped > 0);
	assert(on.tx.size() > 100);
	assert(on.tx == off.tx);
	assert(on.out == off.out);
	std::printf("m1audio idle skip: %zu bytes sent, %llu cycles skipped, same as without\n",
		on.tx.size(), (unsigned long long)on.skipped);
	return 0;
}
