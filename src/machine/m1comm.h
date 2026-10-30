// SPDX-License-Identifier: BSD-3-Clause
// Model 1 communication-board HLE, restricted to two local racing cabinets.
// Shared-memory protocol documented by MAME's m1comm.cpp (BSD-3-Clause,
// copyright Ariane Fugmann). The transport exchanges snapshots at vblank.
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <functional>

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
	bool ready() const { return m_cn && !m_failed && (m_shared[1] == 1 || m_shared[1] == 2); }
	const memory &snapshot() const { return m_shared; }

	void tick()
	{
		if (!m_cn) return;
		if (m_failed) { m_shared[0] = 0xff; return; }
		if (m_shared[1] != 1 && m_shared[1] != 2) return;
		m_shared[0] = m_alive ? 1 : 5;
		memory peer{};
		if (!m_transfer(m_shared, peer)) {
			m_failed = true;
			m_alive = false;
			m_shared[0] = 0xff;
			return;
		}
		const unsigned id = m_shared[1], other = 3 - id;
		if (peer[1] != other) {
			fprintf(stderr, "Model 1 link: expected cabinet mode %u, received %u\n", other, peer[1]);
			m_failed = true;
			m_alive = false;
			m_shared[0] = 0xff;
			return;
		}
		m_alive = true;
		m_shared[0] = 1;
		m_shared[2] = uint8_t(id);
		m_shared[3] = 2;
		constexpr unsigned start = 0x10, size = 0x1c4;
		if (m_shared[4]) std::copy_n(m_shared.begin() + start, size, m_shared.begin() + start + id * size);
		if (peer[4]) std::copy_n(peer.begin() + start, size, m_shared.begin() + start + other * size);
		if (id == 2) std::copy_n(peer.begin() + 6, 10, m_shared.begin() + 6);
		m_shared[5] = 0;
		++m_ticks;
	}

private:
	exchange m_transfer;
	memory m_shared{};
	uint8_t m_cn = 0, m_fg = 0;
	bool m_alive = false, m_failed = false;
	uint64_t m_ticks = 0;
};
