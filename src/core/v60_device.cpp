// Standalone NEC V60 interpreter. The flag macros, register names and PSW helpers below are
// MAME's v60.cpp (BSD-3-Clause, copyright-holders: Farfetch'd, R. Belmont); reset, run and the
// IRQ entry are MAME's too, without the device framework.
#include "v60_device.h"
#include <cstdarg>

void v60_fatal(const char *fmt, ...)
{
	va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap);
	fputc('\n', stderr);
	abort();
}

void v60_log(const char *fmt, ...)
{
	if (!getenv("V60_LOG")) return;
	va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap);
}

#include "v60_macros.h"

void v60_device::v60SaveStack()
{
	if (PSW & 0x10000000)
		ISP = SP;
	else
		m_reg[37 + ((PSW >> 24) & 3)] = SP;
}

void v60_device::v60ReloadStack()
{
	if (PSW & 0x10000000)
		SP = ISP;
	else
		SP = m_reg[37 + ((PSW >> 24) & 3)];
}

uint32_t v60_device::v60ReadPSW()
{
	PSW &= 0xfffffff0;
	PSW |= (_Z?1:0) | (_S?2:0) | (_OV?4:0) | (_CY?8:0);
	return PSW;
}

void v60_device::v60WritePSW(uint32_t newval)
{
	/* determine if we need to save / restore the stacks */
	int updateStack = 0;

	/* if the interrupt state is changing, we definitely need to update */
	if ((newval ^ PSW) & 0x10000000)
		updateStack = 1;

	/* if we are not in interrupt mode and the level is changing, we also must update */
	else if (!(PSW & 0x10000000) && ((newval ^ PSW) & 0x03000000))
		updateStack = 1;

	/* save the previous stack value */
	if (updateStack)
		v60SaveStack();

	/* set the new value and update the flags */
	PSW = newval;
	_Z =  (uint8_t)(PSW & 1);
	_S =  (uint8_t)(PSW & 2);
	_OV = (uint8_t)(PSW & 4);
	_CY = (uint8_t)(PSW & 8);

	/* fetch the new stack value */
	if (updateStack)
		v60ReloadStack();
}


uint32_t v60_device::v60_update_psw_for_exception(int is_interrupt, int target_level)
{
	uint32_t oldPSW = v60ReadPSW();
	uint32_t newPSW = oldPSW;

	// Change to interrupt context
	newPSW &= ~(3 << 24);  // PSW.EL = 0
	newPSW |= target_level << 24; // set target level
	newPSW &= ~(1 << 18);  // PSW.IE = 0
	newPSW &= ~(1 << 16);  // PSW.TE = 0
	newPSW &= ~(1 << 27);  // PSW.TP = 0
	newPSW &= ~(1 << 17);  // PSW.AE = 0
	newPSW &= ~(1 << 29);  // PSW.EM = 0
	if (is_interrupt)
		newPSW |=  (1 << 28);// PSW.IS = 1
	newPSW |=  (1 << 31);  // PSW.ASA = 1
	v60WritePSW(newPSW);

	return oldPSW;
}




// Addressing mode decoding functions
#include "../../third_party/mame/v60/am.hxx"

// Opcode functions
#include "../../third_party/mame/v60/op12.hxx"
#include "../../third_party/mame/v60/op2.hxx"
#include "../../third_party/mame/v60/op3.hxx"
#include "../../third_party/mame/v60/op4.hxx"
#include "../../third_party/mame/v60/op5.hxx"
#include "../../third_party/mame/v60/op6.hxx"
#include "../../third_party/mame/v60/op7a.hxx"

uint32_t v60_device::opUNHANDLED()
{
	fatalerror("Unhandled OpCode found : %02x at %08x\n", OpRead16(PC), PC);
}

// Opcode jump table
#include "../../third_party/mame/v60/optable.hxx"

void v60_device::reset()
{
	m_stall_io = 0;
	m_irq_line = CLEAR_LINE;
	m_nmi_line = CLEAR_LINE;
	for (int i = 0; i < 68; i++)
		m_reg[i] = 0;
	m_reg[45] = 0x00006000; // PIR: V60
	m_op1 = m_op2 = 0; m_flag1 = m_flag2 = 0; m_instflags = 0; m_lenop1 = m_lenop2 = 0;
	m_subop = 0; m_bamoffset1 = m_bamoffset2 = 0; m_amflag = 0; m_amout = 0; m_bamoffset = 0;
	m_amlength1 = m_amlength2 = 0; m_modadd = 0; m_modm = 0; m_modval = m_modval2 = 0;
	m_modwritevalb = 0; m_modwritevalh = 0; m_modwritevalw = 0; m_moddim = 0;
	m_icount = 0;

	PSW   = 0x10000000;
	PC    = m_start_pc & 0xffffff;
	SBR   = 0x00000000;
	SYCW  = 0x00000070;
	TKCW  = 0x0000e000;
	PSW2  = 0x0000f002;
	_CY = _OV = _S = _Z = 0;
}

v60_device::state v60_device::save_state() const
{
	state saved;
	std::copy(std::begin(m_reg), std::end(m_reg), saved.reg.begin());
	saved.flags = {m_flags.CY, m_flags.OV, m_flags.S, m_flags.Z};
	saved.irq_line = m_irq_line;
	saved.nmi_line = m_nmi_line;
	saved.ppc = m_PPC;
	saved.icount = m_icount;
	saved.stall_io = m_stall_io;
	saved.cycle_residue = m_cycles ? *m_cycles : m_icount;
	saved.op1 = m_op1;
	saved.op2 = m_op2;
	saved.flag1 = m_flag1;
	saved.flag2 = m_flag2;
	saved.instflags = m_instflags;
	saved.lenop1 = m_lenop1;
	saved.lenop2 = m_lenop2;
	saved.subop = m_subop;
	saved.bamoffset1 = m_bamoffset1;
	saved.bamoffset2 = m_bamoffset2;
	saved.amflag = m_amflag;
	saved.amout = m_amout;
	saved.bamoffset = m_bamoffset;
	saved.amlength1 = m_amlength1;
	saved.amlength2 = m_amlength2;
	saved.modadd = m_modadd;
	saved.modm = m_modm;
	saved.modval = m_modval;
	saved.modval2 = m_modval2;
	saved.modwritevalb = m_modwritevalb;
	saved.modwritevalh = m_modwritevalh;
	saved.modwritevalw = m_modwritevalw;
	saved.moddim = m_moddim;
	saved.debugger_temp = m_debugger_temp;
	saved.irq_taken = m_irq_taken;
	return saved;
}

void v60_device::load_state(const state &saved)
{
	std::copy(saved.reg.begin(), saved.reg.end(), std::begin(m_reg));
	m_flags.CY = saved.flags[0];
	m_flags.OV = saved.flags[1];
	m_flags.S = saved.flags[2];
	m_flags.Z = saved.flags[3];
	m_irq_line = saved.irq_line;
	m_nmi_line = saved.nmi_line;
	m_PPC = saved.ppc;
	m_icount = saved.icount;
	m_stall_io = saved.stall_io;
	if (m_cycles) *m_cycles = saved.cycle_residue;
	m_op1 = saved.op1;
	m_op2 = saved.op2;
	m_flag1 = saved.flag1;
	m_flag2 = saved.flag2;
	m_instflags = saved.instflags;
	m_lenop1 = saved.lenop1;
	m_lenop2 = saved.lenop2;
	m_subop = saved.subop;
	m_bamoffset1 = saved.bamoffset1;
	m_bamoffset2 = saved.bamoffset2;
	m_amflag = saved.amflag;
	m_amout = saved.amout;
	m_bamoffset = saved.bamoffset;
	m_amlength1 = saved.amlength1;
	m_amlength2 = saved.amlength2;
	m_modadd = saved.modadd;
	m_modm = saved.modm;
	m_modval = saved.modval;
	m_modval2 = saved.modval2;
	m_modwritevalb = saved.modwritevalb;
	m_modwritevalh = saved.modwritevalh;
	m_modwritevalw = saved.modwritevalw;
	m_moddim = saved.moddim;
	m_debugger_temp = saved.debugger_temp;
	m_irq_taken = saved.irq_taken;
}

void v60_device::v60_do_irq(int vector)
{
	standard_irq_callback(vector, PC);
	uint32_t oldPSW = v60_update_psw_for_exception(1, 0);
	SP -= 4;
	m_program->write_dword_unaligned(SP, oldPSW);
	SP -= 4;
	m_program->write_dword_unaligned(SP, PC);
	uint32_t from = PC;
	PC = GETINTVECT(vector);
	m_irq_taken = true;
	if (on_irq) on_irq(vector, from, PC);
}

void v60_device::v60_try_irq()
{
	if (m_irq_line == CLEAR_LINE)
		return;
	if ((PSW & (1 << 18)) != 0) {
		if (m_irq_line != ASSERT_LINE)
			m_irq_line = CLEAR_LINE;
		int vector = m_irq_cycle();
		v60_do_irq(vector + 0x40);
	}
}

void v60_device::set_nmi_line(int state)
{
	if (state == ASSERT_LINE) {
		if (m_nmi_line == CLEAR_LINE) {
			m_nmi_line = ASSERT_LINE;
			v60_do_irq(2);
		}
	} else
		m_nmi_line = CLEAR_LINE;
}

// MAME applies a CPU's input lines through the scheduler, so a line raised by a memory write
// is taken after the current instruction, never in the middle of it.
void v60_device::set_irq_line(int state)
{
	m_irq_line = state;
}

uint32_t v60_device::step()
{
	if (m_irq_line != CLEAR_LINE)
		v60_try_irq();
	m_PPC = PC;
	uint32_t inc = (this->*s_OpCodeTable[OpRead8(PC)])();
	PC += inc;
	return m_PPC;
}

int v60_device::run(int cycles)
{
	// what the last slice overran is owed by this one, as on MAME's scheduler
	m_icount += cycles;
	if (m_irq_line != CLEAR_LINE)
		v60_try_irq();
	while (m_icount > 0) {
		m_PPC = PC;
		if (on_exec) on_exec(PC);
		m_icount -= 8;
		uint32_t inc = (this->*s_OpCodeTable[OpRead8(PC)])();
		PC += inc;
		if (m_irq_line != CLEAR_LINE)
			v60_try_irq();
	}
	return cycles;
}
