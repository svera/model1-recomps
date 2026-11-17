#pragma once
#include "lan_session.h"
#include "../machine/link_packet.h"
#include "../machine/model1.h"
#include <bit>

// Model 1 M1COMM LAN adapter. Cabinet IDs are frozen independently of room membership.
class model1_lan
{
public:
	bool active() const { return m_player != 0; }
	int player() const { return m_player; }
	int role() const { return m_player == 1 ? 1 : 2; }
	int racers() const { return std::popcount(m_active_mask); }
	bool running() const { return m_running; }
	bool host() const { return m_player == 1; }
	bool paused() const { return active() && m_running && m_pause_step != 0; }
	int countdown() const { return paused() && m_pause_step >= 2 ? 5 - m_pause_step : 0; }
	void pause(bool value, uint64_t now)
	{
		if (!active() || !host()) return;
		if (value) {
			m_resume_requested = false;
			if (m_pause_step != 1) set_pause_step(1, now);
		} else if (m_pause_step) m_resume_requested = true;
	}

	bool begin(lan_session &session, uint32_t generation,
		std::function<m1comm::memory(m1comm::memory)> retire_peer = {})
	{
		const auto members = session.members(true);
		if (members.size() < 2 || members.size() > 4 ||
			std::any_of(members.begin(), members.end(), [](const auto &m) { return m.gone; }))
			return session.fail("A linked race needs two to four connected racers.");
		m_player = 0;
		m_count = int(members.size());
		for (int i = 0; i < m_count; i++) {
			m_ids[i] = members[i].id;
			if (members[i].own) m_player = i + 1;
			m_peers[i].reset(generation, uint8_t(i + 1));
		}
		if (!m_player) return session.fail("This device is not in the racing roster.");
		m_active_mask = uint8_t((1u << m_count) - 1);
		m_retire_peer = std::move(retire_peer);
		m_generation = generation;
		m_sequence = 0;
		m_running = m_received = false;
		m_pause_step = 0;
		m_pause_epoch = 0;
		m_resume_requested = false;
		m_next_publish = 0;
		m_published_frame = UINT64_MAX;
		return true;
	}
	void attach(model1 &machine) { machine.enable_link({}, true); }
	bool update(lan_session &session, uint64_t now, model1 &machine)
	{
		if (!active()) return true;
		auto *board = machine.link();
		if (board && board->ticks()) m_running = true;
		const auto members = session.members(true);
		if (int(members.size()) != m_count || session.interrupted())
			return session.fail("LAN session interrupted. The linked race has ended.");
		for (int i = 0; i < m_count; i++)
			if (members[i].id != m_ids[i])
				return session.fail("The racing roster changed. The linked race has ended.");
		if (!m_received) { m_last_packet.fill(now); m_last_progress.fill(now); m_received = true; }
		for (int i = 0; i < m_count; i++) {
			if (i == m_player - 1 || !(m_active_mask & (1u << i))) continue;
			const auto bytes = session.block(i);
			const bool had_packet = m_peers[i].available();
			const uint32_t previous_frame = m_peers[i].frame();
			if (!bytes.empty() && m_peers[i].take(bytes)) {
				m_last_packet[i] = now;
				if (!had_packet || previous_frame != m_peers[i].frame()) m_last_progress[i] = now;
			}
		}
		if (paused()) m_last_progress.fill(now);
		if (!host()) {
			if (members[0].gone || now > m_last_packet[0] + (m_peers[0].available() ? 3000 : 15000))
				return session.fail("The host disconnected. The linked race has ended.");
			if (m_peers[0].available()) {
				m_active_mask &= m_peers[0].active_mask();
				m_pause_step = m_peers[0].pause_step();
				m_pause_epoch = m_peers[0].pause_epoch();
			}
			if (!(m_active_mask & (1u << (m_player - 1))))
				return session.fail("The host lost this racer's connection. The linked race has ended.");
		} else {
			for (int i = 1; i < m_count; i++)
				if ((m_active_mask & (1u << i)) && (members[i].gone ||
					now > m_last_packet[i] + (m_peers[i].available() ? 3000 : 15000) ||
					(!paused() && board && now > m_last_progress[i] + 3000 &&
						uint32_t(m_peers[i].frame() - uint32_t(board->ticks())) >= 0x80000000u))) {
					m_active_mask &= uint8_t(~(1u << i));
					fprintf(stderr, "LAN: cabinet %d disconnected; %d racers remain (gone=%d data=%d frame=%u/%llu)\n",
						i + 1, racers(), members[i].gone, m_peers[i].available(), m_peers[i].frame(),
						(unsigned long long)(board ? board->ticks() : 0));
				}
		}
		if (racers() < 2)
			return session.fail("Fewer than two racers remain. The linked race has ended.");
		if (m_active_mask != uint8_t((1u << m_count) - 1) && !m_retire_peer)
			return session.fail("A cabinet disconnected. This game cannot continue without it.");
		bool acknowledged = true;
		for (int i = 1; i < m_count; i++)
			if (m_active_mask & (1u << i))
				acknowledged = acknowledged && m_peers[i].available() &&
					m_peers[i].pause_epoch() == m_pause_epoch && m_peers[i].pause_step() == m_pause_step &&
					m_peers[i].active_mask() == m_active_mask;
		if (host() && m_running && m_pause_step && acknowledged) {
			if (m_pause_step == 1 && m_resume_requested) set_pause_step(2, now);
			else if (m_pause_step >= 2 && now - m_step_started >= 1000)
				set_pause_step(m_pause_step == 4 ? 0 : m_pause_step + 1, now);
		}
		if (board && board->ready() && board->snapshot()[1] == role() &&
			(now >= m_next_publish || board->ticks() != m_published_frame)) {
			if (m_published_frame == UINT64_MAX)
				fprintf(stderr, "LAN: cabinet %d board ready (role=%d)\n", m_player, role());
			session.publish(model1_link::encode(board->snapshot(), m_generation, m_sequence++,
				m_pause_step, m_pause_epoch, uint8_t(m_player), m_active_mask, uint32_t(board->ticks())));
			m_next_publish = now + 16;
			m_published_frame = board->ticks();
		} else if (now >= m_next_publish) {
			// arcade-lan needs peer heartbeats even before the ROM enables its board.
			session.publish({});
			m_next_publish = now + 16;
		}
		return true;
	}
	bool ready_for_frame(model1 &machine) const
	{
		const auto *board = machine.link();
		if (!board || !board->ready() || board->snapshot()[1] != role()) return true;
		for (int i = 0; i < m_count; i++)
			if (i != m_player - 1 && (m_active_mask & (1u << i)) &&
				(!m_peers[i].available() || uint32_t(m_peers[i].frame() - uint32_t(board->ticks())) >= 0x80000000u))
				return false;
		return true;
	}
	void exchange(model1 &machine)
	{
		auto *board = machine.link();
		if (!board || !board->ready() || board->snapshot()[1] != role()) return;
		std::array<m1comm::memory, 4> snapshots{};
		for (int i = 0; i < m_count; i++) {
			if (i == m_player - 1) snapshots[i] = board->snapshot();
			else if (!(m_active_mask & (1u << i))) {
				if (!m_retire_peer)
					throw std::logic_error("LAN session cannot retire a cabinet without a game policy");
				snapshots[i] = m_retire_peer(m_peers[i].memory());
			} else {
				if (!m_peers[i].available()) return;
				snapshots[i] = m_peers[i].memory();
			}
		}
		board->tick_group(unsigned(m_player), std::span(snapshots).first(m_count));
		m_running = true;
	}
	void run_frame(model1 &machine)
	{
		for (int line = 0; line < model1::FRAME_LINES; line++) {
			if (line == model1::FRAME_VBLANK) exchange(machine);
			machine.run_scanline();
		}
	}
	void stop(model1 &machine)
	{
		machine.disable_link();
		m_player = 0;
		m_running = false;
	}
private:
	void set_pause_step(uint8_t step, uint64_t now)
	{
		m_pause_step = step;
		++m_pause_epoch;
		m_step_started = now;
	}
	int m_player = 0, m_count = 0;
	uint32_t m_generation = 0, m_sequence = 0;
	std::array<uint32_t, 4> m_ids{};
	std::array<model1_link::receiver, 4> m_peers;
	std::array<uint64_t, 4> m_last_packet{}, m_last_progress{};
	bool m_running = false, m_received = false, m_resume_requested = false;
	uint8_t m_pause_step = 0, m_active_mask = 0;
	uint32_t m_pause_epoch = 0;
	uint64_t m_step_started = 0, m_next_publish = 0;
	uint64_t m_published_frame = UINT64_MAX;
	std::function<m1comm::memory(m1comm::memory)> m_retire_peer;
};
