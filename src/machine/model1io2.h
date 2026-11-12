// Sega Advanced Model 1 I/O board (837-10859), used by Wing War.
#pragma once
#include "mame_lite.h"
extern "C" {
#include "../../third_party/z80/z80.h"
}

class model1io2
{
public:
	static constexpr int CLOCK = 9830400;

	model1io2(const std::vector<uint8_t> &rom, uint8_t *dpram);
	void reset();
	void run(double usec);
	uint64_t cycles_executed() const { return m_cpu.cyc; }

	uint8_t inputs[3] = { 0xff, 0xff, 0xff };
	uint8_t analog_inputs[8] = { 0x80, 0x80, 0x01, 0xff, 0xff, 0xff, 0xff, 0xff };
	uint8_t outputs = 0;
	uint16_t eeprom[64];

	uint8_t mem_r(uint16_t address);
	void mem_w(uint16_t address, uint8_t value);
	uint8_t port_r(uint8_t port);
	void port_w(uint8_t port, uint8_t value);

private:
	std::vector<uint8_t> m_rom;
	uint8_t m_ram[0x2000]{};
	uint8_t *m_dpram;
	z80 m_cpu;
	double m_carry = 0;
	uint64_t m_time = 0;

	uint8_t m_port_value[7]{ 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
	uint8_t m_port_config = 0;
	uint8_t m_serial_output = 0;
	uint8_t m_command = 0;
	uint16_t m_address = 0;
	bool m_secondary = false;
	uint8_t m_adc_shift = 0;
	uint32_t m_fpga_counter = 0;
	struct ctc_channel {
		uint8_t control = 0;
		uint16_t constant = 0x100;
		uint64_t next_cycle = 0;
		bool waiting_for_constant = false;
		bool running = false;
	};
	ctc_channel m_ctc[4]{};
	uint8_t m_ctc_vector = 0;
	uint8_t port_input(int port);
	void port_output(int port, uint8_t value);
	void ctc_write(uint8_t channel, uint8_t value);
	uint8_t ctc_read(uint8_t channel) const;
	void ctc_advance();

	enum ee_state { EE_RESET, EE_START, EE_COMMAND, EE_READ, EE_DATA, EE_DONE };
	ee_state m_ee = EE_RESET;
	int m_cs = 0, m_clk = 0, m_di = 0;
	uint32_t m_accum = 0, m_shift = 0;
	int m_bits = 0, m_ee_command = 0, m_ee_address = 0;
	bool m_locked = true;
	uint64_t m_ready_at = 0, m_cs_rise = 0;
	bool ee_ready() const { return m_time >= m_ready_at; }
	void ee_cs(int state);
	void ee_clk(int state);
	int ee_do() const;
};
