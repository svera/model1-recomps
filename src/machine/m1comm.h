// SPDX-License-Identifier: BSD-3-Clause
// Model 1 communication-board HLE for linked racers and a receive-only relay.
// Shared-memory protocol documented by MAME's m1comm.cpp (BSD-3-Clause,
// copyright Ariane Fugmann). The transport exchanges snapshots at vblank.
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <span>
#include <stdexcept>

class m1comm
{
public:
	using memory = std::array<uint8_t, 0x1000>;
	using exchange = std::function<bool(const memory &, memory &)>;

	explicit m1comm(exchange transfer) : m_transfer(std::move(transfer)) {}
	void reset() { m_shared.fill(0); m_cn = m_fg = 0; m_alive = m_failed = false; m_ticks = 0; }
	uint8_t read(unsigned offset) const { return m_shared[offset]; }
	void write(unsigned offset, uint8_t value) { m_shared[offset] = value; }
	uint8_t cn() const { return 0xfe | m_cn; }
	uint8_t fg() const { return 0x7e | m_fg | (m_alive ? 0 : 0x80); }
	void set_cn(uint8_t value)
	{
		m_cn = value & 1;
		m_alive = m_failed = false;
		m_ticks = 0;
	}
	void set_fg(uint8_t value) { if (m_cn) m_fg = value & 1; }
	uint64_t ticks() const { return m_ticks; }
	bool ready() const { return m_cn && !m_failed && m_shared[1] <= 2; }
	const memory &snapshot() const { return m_shared; }

	// Mode 0 observes the same pre-exchange snapshots as the racers, but never
	// publishes its local block or contributes to the racer count.
	void receive(const memory &master, const memory &slave)
	{
		receive(master, &slave);
	}
	void receive(const memory &master, const memory *slave = nullptr)
	{
		if (!m_cn) return;
		if (m_failed) { m_shared[0] = 0xff; return; }
		if (m_shared[1] != 0 || master[1] != 1 || (slave && (*slave)[1] != 2)) {
			fprintf(stderr, "Model 1 relay: expected relay/master/slave modes 0/1/2, received %u/%u/%u\n",
				m_shared[1], master[1], slave ? (*slave)[1] : 0);
			m_failed = true;
			m_alive = false;
			m_shared[0] = 0xff;
			return;
		}
		m_alive = true;
		m_shared[0] = 1;
		m_shared[2] = 0;
		m_shared[3] = slave ? 2 : 1;
		receive_block(master, 1);
		if (slave) receive_block(*slave, 2);
		std::copy_n(master.begin() + 6, 10, m_shared.begin() + 6);
		m_shared[5] = 0;
		++m_ticks;
	}

	void tick(bool single_racer = false)
	{
		if (!m_cn) return;
		if (m_failed) { m_shared[0] = 0xff; return; }
		if (m_shared[1] != 1 && m_shared[1] != 2) return;
		m_shared[0] = m_alive ? 1 : 5;
		memory peer{};
		if (single_racer && m_shared[1] != 1) {
			fprintf(stderr, "Model 1 link: a single racer must be master, received %u\n", m_shared[1]);
			m_failed = true;
			m_alive = false;
			m_shared[0] = 0xff;
			return;
		}
		if (!single_racer && !m_transfer(m_shared, peer)) {
			m_failed = true;
			m_alive = false;
			m_shared[0] = 0xff;
			return;
		}
		const unsigned id = m_shared[1], other = 3 - id;
		if (!single_racer && peer[1] != other) {
			fprintf(stderr, "Model 1 link: expected cabinet mode %u, received %u\n", other, peer[1]);
			m_failed = true;
			m_alive = false;
			m_shared[0] = 0xff;
			return;
		}
		m_alive = true;
		m_shared[0] = 1;
		m_shared[2] = uint8_t(id);
		m_shared[3] = single_racer ? 1 : 2;
		receive_block(m_shared, id);
		if (!single_racer) receive_block(peer, other);
		if (id == 2) std::copy_n(peer.begin() + 6, 10, m_shared.begin() + 6);
		m_shared[5] = 0;
		++m_ticks;
	}

	void tick_group(unsigned id, std::span<const memory> cabinets)
	{
		if (!m_cn) return;
		if (m_failed) { m_shared[0] = 0xff; return; }
		if (id < 1 || id > cabinets.size() || cabinets.size() < 2 || cabinets.size() > 4)
			throw std::invalid_argument("Model 1 link requires two to four cabinets and a valid ID");
		for (size_t i = 0; i < cabinets.size(); i++)
			if (cabinets[i][1] != (i == 0 ? 1 : 2))
				throw std::invalid_argument("Model 1 link requires one master followed by slaves");
		if (m_shared[1] != (id == 1 ? 1 : 2))
			throw std::invalid_argument("Model 1 link cabinet role does not match its ID");
		m_alive = true;
		m_shared[0] = 1;
		m_shared[2] = uint8_t(id);
		m_shared[3] = uint8_t(cabinets.size());
		for (size_t i = 0; i < cabinets.size(); i++) receive_block(cabinets[i], unsigned(i + 1));
		if (id != 1) std::copy_n(cabinets[0].begin() + 6, 10, m_shared.begin() + 6);
		m_shared[5] = 0;
		++m_ticks;
	}

private:
	void receive_block(const memory &source, unsigned id)
	{
		constexpr unsigned start = 0x10, size = 0x1c4;
		if (source[4]) std::copy_n(source.begin() + start, size, m_shared.begin() + start + id * size);
	}

	exchange m_transfer;
	memory m_shared{};
	uint8_t m_cn = 0, m_fg = 0;
	bool m_alive = false, m_failed = false;
	uint64_t m_ticks = 0;
};
