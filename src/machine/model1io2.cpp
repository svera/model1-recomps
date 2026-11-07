#include "model1io2.h"

namespace {
uint8_t z_read(void *user, uint16_t address) { return static_cast<model1io2 *>(user)->mem_r(address); }
void z_write(void *user, uint16_t address, uint8_t value) { static_cast<model1io2 *>(user)->mem_w(address, value); }
uint8_t z_in(z80 *cpu, uint8_t port) { return static_cast<model1io2 *>(cpu->userdata)->port_r(port); }
void z_out(z80 *cpu, uint8_t port, uint8_t value) { static_cast<model1io2 *>(cpu->userdata)->port_w(port, value); }

constexpr uint64_t cycles_us(double usec) { return uint64_t(usec * model1io2::CLOCK / 1e6); }
constexpr uint64_t WRITE_TIME = cycles_us(1750), WRITE_ALL_TIME = cycles_us(8000);
constexpr uint64_t ERASE_TIME = cycles_us(1000), ERASE_ALL_TIME = cycles_us(8000);
}

model1io2::model1io2(const std::vector<uint8_t> &rom, uint8_t *dpram) : m_rom(rom), m_dpram(dpram)
{
	m_rom.resize(0x8000, 0xff);
	std::fill(std::begin(eeprom), std::end(eeprom), 0xffff);
}

void model1io2::reset()
{
	z80_init(&m_cpu);
	m_cpu.read_byte = z_read;
	m_cpu.write_byte = z_write;
	m_cpu.port_in = z_in;
	m_cpu.port_out = z_out;
	m_cpu.userdata = this;
	m_carry = 0;
	m_time = m_ready_at = m_cs_rise = 0;
	std::fill(std::begin(m_ram), std::end(m_ram), 0);
	std::fill(std::begin(m_port_value), std::end(m_port_value), 0xff);
	m_port_config = 0;
	m_serial_output = m_command = 0;
	m_address = 0;
	m_secondary = false;
	m_adc_shift = 0;
	m_fpga_counter = 0;
	std::fill(std::begin(m_ctc), std::end(m_ctc), ctc_channel{});
	m_ctc_vector = 0;
	m_ee = EE_RESET;
	m_cs = m_clk = m_di = 0;
	m_accum = m_shift = 0;
	m_bits = m_ee_command = m_ee_address = 0;
	m_locked = true;
	outputs = 0;
}

void model1io2::run(double usec)
{
	const double target = usec * CLOCK / 1e6 + m_carry;
	const unsigned long start = m_cpu.cyc;
	while (double(m_cpu.cyc - start) < target) {
		m_time = m_cpu.cyc;
		z80_step(&m_cpu);
		ctc_advance();
	}
	m_carry = target - double(m_cpu.cyc - start);
	m_time = m_cpu.cyc;
}

uint8_t model1io2::mem_r(uint16_t address)
{
	m_time = m_cpu.cyc;
	if (address < 0x8000) return m_rom[address];
	if (address >= 0x8000 && address <= 0x800f) return port_r(uint8_t(address & 0x0f));
	if (address == 0x8040) return uint8_t(0xbf | (ee_do() << 6));
	if (address == 0x8080) return 0xff;
	if (address >= 0x8100 && address <= 0x810f) return m_fpga_counter < 0x1400 ? 0x80 : 0xff;
	if (address >= 0x8200 && address <= 0x8207) {
		const uint8_t value = uint8_t(0xfe | (m_adc_shift >> 7));
		m_adc_shift <<= 1;
		return value;
	}
	if (address >= 0xe000) return m_ram[address - 0xe000];
	return 0xff;
}

void model1io2::mem_w(uint16_t address, uint8_t value)
{
	m_time = m_cpu.cyc;
	if (address >= 0x8000 && address <= 0x800f) {
		port_w(uint8_t(address & 0x0f), value);
		return;
	}
	if (address >= 0x8100 && address <= 0x810f) {
		m_fpga_counter++;
		return;
	}
	if (address >= 0x8200 && address <= 0x8207) {
		m_adc_shift = analog_inputs[(address & 3) + (m_secondary ? 4 : 0)];
		return;
	}
	if (address >= 0xe000) m_ram[address - 0xe000] = value;
}

uint8_t model1io2::port_r(uint8_t address)
{
	if (address <= 0x0f) {
		const int reg = address;
		if (reg <= 6) {
			const uint8_t value = (m_port_config & (1 << reg)) ? port_input(reg) : m_port_value[reg];
			return value;
		}
		switch (reg) {
		case 8: return m_port_config;
		case 10: return m_serial_output;
		case 11: return m_command;
		case 12: return m_dpram[m_address & 0x7ff];
		case 13: return 0x08;
		default: return 0xff;
		}
	}

	if (address >= 0x10 && address <= 0x13)
		return ctc_read(address & 3);

	// TMPZ84C015 internal PIO: the board's DIP banks are left at their MAME defaults.
	const uint8_t reg = address & 0x03;
	if (reg == 0 || reg == 2) return 0xff;
	return 0xff;
}

void model1io2::port_w(uint8_t address, uint8_t value)
{
	if (address <= 0x0f) {
		const int reg = address;
		if (reg <= 6) {
			m_port_value[reg] = value;
			port_output(reg, value);
		} else if (reg == 8) {
			const uint8_t changed = value ^ m_port_config;
			for (int i = 0; i < 7; i++)
				if ((changed & (1 << i)) && !(value & (1 << i)))
					port_output(i, m_port_value[i]);
			m_port_config = value;
		} else if (reg == 9) {
			m_command = value;
			switch (value) {
			case 0: m_address = (m_address & 0xff00) | m_serial_output; break;
			case 1: m_address = (m_address & 0x00ff) | (m_serial_output << 8); break;
			case 7: m_dpram[m_address & 0x7ff] = m_serial_output; break;
			default:
				if (value >= 0x70 && value <= 0x77) m_dpram[value & 7] = m_serial_output;
				break;
			}
		} else if (reg == 10) {
			m_serial_output = value;
		}
		return;
	}

	if (address >= 0x10 && address <= 0x13) {
		ctc_write(address & 3, value);
		return;
	}
}

void model1io2::ctc_write(uint8_t channel, uint8_t value)
{
	auto &ctc = m_ctc[channel];
	if (ctc.waiting_for_constant) {
		ctc.constant = value ? value : 0x100;
		ctc.waiting_for_constant = false;
		ctc.control &= ~0x02;
		ctc.running = (ctc.control & 0x48) == 0;
		if (ctc.running) {
			const uint64_t prescaler = (ctc.control & 0x20) ? 256 : 16;
			ctc.next_cycle = m_cpu.cyc + ctc.constant * prescaler;
		}
		return;
	}

	if (!(value & 1)) {
		if (channel == 0) m_ctc_vector = value & 0xf8;
		return;
	}

	ctc.control = value;
	if (value & 0x02) ctc.running = false;
	if (value & 0x04) {
		ctc.waiting_for_constant = true;
		ctc.running = false;
	} else if (!(value & 0x40) && !(value & 0x08) && !ctc.running) {
		const uint64_t prescaler = (value & 0x20) ? 256 : 16;
		ctc.next_cycle = m_cpu.cyc + ctc.constant * prescaler;
		ctc.running = true;
	}
}

uint8_t model1io2::ctc_read(uint8_t channel) const
{
	const auto &ctc = m_ctc[channel];
	if (!ctc.running) return uint8_t(ctc.constant);
	const uint64_t prescaler = (ctc.control & 0x20) ? 256 : 16;
	const uint64_t remaining = ctc.next_cycle > m_cpu.cyc ? ctc.next_cycle - m_cpu.cyc : 0;
	const uint64_t count = (remaining + prescaler - 1) / prescaler;
	return uint8_t(count);
}

void model1io2::ctc_advance()
{
	for (uint8_t channel = 0; channel < 4; channel++) {
		auto &ctc = m_ctc[channel];
		if (!ctc.running || ctc.next_cycle > m_cpu.cyc) continue;

		if ((ctc.control & 0x80) && !m_cpu.int_pending)
			z80_gen_int(&m_cpu, uint8_t(m_ctc_vector + channel * 2));
		const uint64_t prescaler = (ctc.control & 0x20) ? 256 : 16;
		ctc.next_cycle += ctc.constant * prescaler;
	}
}

uint8_t model1io2::port_input(int port)
{
	if (port >= 0 && port <= 2) return inputs[port];
	return 0xff;
}

void model1io2::port_output(int port, uint8_t value)
{
	if (port == 3) outputs = value;
	else if (port == 5) {
		m_di = (value >> 6) & 1;
		ee_cs((value >> 4) & 1);
		ee_clk((value >> 5) & 1);
	} else if (port == 6) {
		m_secondary = (value & 0x40) != 0;
	}
}

void model1io2::ee_cs(int state)
{
	if (state == m_cs) return;
	m_cs = state;
	if (state) {
		m_cs_rise = m_time;
		if (m_ee == EE_RESET) m_ee = EE_START;
	} else {
		m_ee = EE_RESET;
	}
}

void model1io2::ee_clk(int state)
{
	if (state == m_clk) return;
	m_clk = state;
	if (!state) return;
	switch (m_ee) {
	case EE_START:
		if (m_di && ee_ready() && m_time > m_cs_rise) {
			m_accum = 0;
			m_bits = 0;
			m_ee = EE_COMMAND;
		}
		break;
	case EE_COMMAND:
		m_accum = (m_accum << 1) | m_di;
		if (++m_bits == 8) {
			const int address = m_accum & 0x3f;
			const int op = m_accum >> 6;
			m_bits = 0;
			if (op == 0) {
				switch (address >> 4) {
				case 0: m_locked = true; m_ee = EE_RESET; break;
				case 1: m_ee_command = 1; m_shift = 0; m_ee = EE_DATA; break;
				case 2:
					if (m_locked) { m_ee = EE_RESET; break; }
					std::fill(std::begin(eeprom), std::end(eeprom), 0xffff);
					m_ready_at = m_time + ERASE_ALL_TIME;
					m_ee = EE_DONE;
					break;
				case 3: m_locked = false; m_ee = EE_RESET; break;
				}
			} else if (op == 1) {
				m_ee_command = 0;
				m_ee_address = address;
				m_shift = 0;
				m_ee = EE_DATA;
			} else if (op == 2) {
				m_ee_address = address;
				m_shift = uint32_t(eeprom[address]) << 16;
				m_bits = 0;
				m_ee = EE_READ;
			} else if (!m_locked) {
				eeprom[address] = 0xffff;
				m_ready_at = m_time + ERASE_TIME;
				m_ee = EE_DONE;
			} else {
				m_ee = EE_RESET;
			}
		}
		break;
	case EE_READ:
		m_shift <<= 1;
		if (++m_bits == 16) {
			m_bits = 0;
			m_ee_address = (m_ee_address + 1) & 0x3f;
			m_shift = uint32_t(eeprom[m_ee_address]) << 16;
		}
		break;
	case EE_DATA:
		m_shift = (m_shift << 1) | m_di;
		if (++m_bits == 16) {
			if (m_locked) { m_ee = EE_RESET; break; }
			if (m_ee_command == 0) {
				eeprom[m_ee_address] = uint16_t(m_shift);
				m_ready_at = m_time + WRITE_TIME;
			} else {
				for (auto &word : eeprom) word &= uint16_t(m_shift);
				m_ready_at = m_time + WRITE_ALL_TIME;
			}
			m_ee = EE_DONE;
		}
		break;
	default:
		break;
	}
}

int model1io2::ee_do() const
{
	if (m_ee == EE_START) return ee_ready() ? 1 : 0;
	if (m_ee == EE_READ) return (m_shift & 0x80000000) ? 1 : 0;
	return 1;
}
