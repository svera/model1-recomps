#include "../src/core/mb86233.h"
#include <cassert>
#include <cfenv>
#include <cstring>

namespace {

struct bus : mb86233_bus
{
	u32 prog[8]{};
	u32 data[8]{};
	mb86233_device *cpu = nullptr;
	bool blocked = false;
	u32 prog_r(u32 a) override { return a < 8 ? prog[a] : 0; }
	u32 data_r(u32 a) override
	{
		if (blocked) { cpu->stall(); return 0; }
		return a < 8 ? data[a] : 0;
	}
	void data_w(u32 a, u32 v) override { if (a < 8) data[a] = v; }
	u32 io_r(u32) override { return 0; }
	void io_w(u32, u32) override {}
};

u32 mov(u32 alu, u32 r2, u32 r1) { return 0x07u << 26 | alu << 21 | 7u << 18 | r2 << 9 | r1; }

// D = 1.0 + 0.75 ulp through fadd, with `mode` (the stmh word) set first
u32 fadd_sum(u32 mode)
{
	bus b;
	b.prog[0] = mode;
	b.prog[1] = mov(0, 0xd9, 0);      // D = data[0]
	b.prog[2] = mov(0, 0xd0, 1);      // A = data[1]
	b.prog[3] = mov(6, 0xd3, 2);      // fadd, D += A
	b.prog[4] = mov(0, 0x19, 3);      // data[3] = D
	b.data[0] = 0x3f800000;           // 1.0
	b.data[1] = 0x33c00000;           // 1.5 * 2^-24, 0.75 ulp of 1.0
	mb86233_device cpu(b);
	cpu.reset();
	cpu.run(6);
	return b.data[3];
}

}

int main()
{
	{
		bus b;
		mb86233_device cpu(b);
		b.cpu = &cpu;
		b.prog[0] = mov(0, 0xd9, 0);
		b.prog[1] = mov(0, 0x19, 1);
		b.data[0] = 0x12345678;
		b.blocked = true;
		cpu.reset();
		assert(!cpu.stalled());
		assert(cpu.run(100000) == 1);
		assert(cpu.stalled() && cpu.pc() == 0);
		assert(b.data[1] == 0);
		assert(cpu.run(100000) == 1);
		assert(cpu.stalled() && cpu.pc() == 0);
		b.blocked = false;
		assert(cpu.run(2) == 2);
		assert(!cpu.stalled() && cpu.pc() == 2);
		assert(b.data[1] == 0x12345678);
		b.blocked = true;
		cpu.reset();
		assert(!cpu.stalled());
		assert(cpu.run(100000) == 1);
	}
	fesetround(FE_TONEAREST);
	assert(fadd_sum(0x360a0001) == 0x3f800001);   // nearest
	assert(fadd_sum(0x360a0007) == 0x3f800000);   // toward zero, as the Model 1 programs select
	assert(fadd_sum(0x360a0003) == 0x3f800001);   // up
	assert(fegetround() == FE_TONEAREST);         // the host's mode is restored
}
