#pragma once
#include "m1comm.h"
#include <span>

namespace model1_link {

// Only the cabinet's outgoing record and the master's status cross the network.
constexpr size_t RECORD_SIZE = 0x1c4;
constexpr size_t PACKET_SIZE = 28 + 10 + RECORD_SIZE;
using packet = std::array<uint8_t, PACKET_SIZE>;

inline void put32(uint8_t *out, uint32_t value)
{
	for (int i = 0; i < 4; i++) out[i] = uint8_t(value >> (i * 8));
}

inline uint32_t get32(const uint8_t *in)
{
	uint32_t value = 0;
	for (int i = 0; i < 4; i++) value |= uint32_t(in[i]) << (i * 8);
	return value;
}

inline packet encode(const m1comm::memory &memory, uint32_t generation, uint32_t sequence,
	uint8_t pause_step = 0, uint32_t pause_epoch = 0, uint8_t cabinet = 0, uint8_t active_mask = 0x0f,
	uint32_t frame = 0)
{
	packet out{};
	std::copy_n("M1LB", 4, out.begin());
	out[4] = 3;
	out[5] = cabinet ? cabinet : memory[1];
	out[6] = memory[4];
	out[7] = pause_step;
	put32(out.data() + 8, generation);
	put32(out.data() + 12, sequence);
	put32(out.data() + 16, pause_epoch);
	out[20] = active_mask;
	put32(out.data() + 24, frame);
	std::copy_n(memory.begin() + 6, 10, out.begin() + 28);
	std::copy_n(memory.begin() + 0x10, RECORD_SIZE, out.begin() + 38);
	return out;
}

class receiver
{
public:
	void reset(uint32_t generation, uint8_t peer)
	{
		m_generation = generation;
		m_peer = peer;
		m_sequence = 0;
		m_have = false;
		m_pause_step = 0;
		m_pause_epoch = 0;
		m_active_mask = 0;
		m_frame = 0;
		m_memory = {};
	}

	bool take(std::span<const uint8_t> bytes)
	{
		if (bytes.size() != PACKET_SIZE || !std::equal(bytes.begin(), bytes.begin() + 4, "M1LB") ||
			bytes[4] != 3 || bytes[5] != m_peer || m_peer < 1 || m_peer > 4 || bytes[7] > 4 ||
			!(bytes[20] & 1) || (bytes[20] & 0xf0) || get32(bytes.data() + 8) != m_generation)
			return false;
		for (size_t i : {size_t(21), size_t(22), size_t(23)})
			if (bytes[i]) return false;
		const auto sequence = get32(bytes.data() + 12);
		if (m_have && (sequence == m_sequence || uint32_t(sequence - m_sequence) >= 0x80000000u))
			return false;
		m_sequence = sequence;
		m_have = true;
		m_pause_step = bytes[7];
		m_pause_epoch = get32(bytes.data() + 16);
		m_active_mask = bytes[20];
		m_frame = get32(bytes.data() + 24);
		m_memory[1] = m_peer == 1 ? 1 : 2;
		m_memory[4] = bytes[6];
		std::copy_n(bytes.begin() + 28, 10, m_memory.begin() + 6);
		std::copy_n(bytes.begin() + 38, RECORD_SIZE, m_memory.begin() + 0x10);
		return true;
	}

	bool available() const { return m_have; }
	uint8_t pause_step() const { return m_pause_step; }
	uint32_t pause_epoch() const { return m_pause_epoch; }
	uint8_t active_mask() const { return m_active_mask; }
	uint32_t frame() const { return m_frame; }
	const m1comm::memory &memory() const { return m_memory; }

private:
	uint32_t m_generation = 0, m_sequence = 0;
	uint8_t m_peer = 0;
	uint8_t m_pause_step = 0;
	uint8_t m_active_mask = 0;
	uint32_t m_pause_epoch = 0;
	uint32_t m_frame = 0;
	bool m_have = false;
	m1comm::memory m_memory{};
};

}
