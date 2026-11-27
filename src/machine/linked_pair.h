#pragma once
#include "model1.h"
#include <stdexcept>
#include <vector>

// Up to four racers and an optional receive-only monitor advance on one thread. Take all
// outgoing snapshots before any board receives them to avoid execution-order effects.
class linked_pair
{
public:
	linked_pair(model1 &first, model1 &second, model1 *relay = nullptr) : linked_pair(first, &second, relay) {}
	linked_pair(model1 &first, model1 *second, model1 *relay)
		: linked_pair(second ? std::vector<model1 *>{&first, second} : std::vector<model1 *>{&first}, relay) {}
	linked_pair(std::vector<model1 *> racers, model1 *relay = nullptr)
		: m_machines(std::move(racers)), m_relay(relay), m_snapshots(m_machines.size())
	{
		if (m_machines.empty() || m_machines.size() > 4 || (m_machines.size() == 1 && !relay))
			throw std::invalid_argument("linked cabinets require two to four players or a monitor");
		for (size_t p = 0; p < m_machines.size(); p++)
			if (!m_machines[p] || m_machines[p] == relay ||
				std::find(m_machines.begin(), m_machines.begin() + p, m_machines[p]) != m_machines.begin() + p)
				throw std::invalid_argument("linked cabinets must be distinct machines");
		for (auto *machine : m_machines)
			machine->enable_link([](const auto &, auto &) -> bool {
				throw std::logic_error("group links exchange snapshots together");
			}, true);
		if (relay)
			relay->enable_link([](const auto &, auto &) -> bool {
				throw std::logic_error("a relay must not publish racing data");
			}, true);
		m_retired.resize(m_machines.size());
	}
	linked_pair(const linked_pair &) = delete;
	linked_pair &operator=(const linked_pair &) = delete;
	~linked_pair() { disconnect(); }
	void disconnect()
	{
		for (model1 *&machine : m_machines)
			if (machine) { machine->disable_link(); machine = nullptr; }
		if (m_relay) { m_relay->disable_link(); m_relay = nullptr; }
	}
	bool retire(model1 &machine,
		const std::function<m1comm::memory(m1comm::memory, const m1comm::memory &)> &policy)
	{
		const auto at = std::find(m_machines.begin(), m_machines.end(), &machine);
		if (at == m_machines.end() || !policy || !machine.link()) return false;
		const size_t index = size_t(at - m_machines.begin());
		m_snapshots[index] = machine.link()->snapshot();
		m_retired[index] = policy;
		return true;
	}
	void run_frame()
	{
		if (!m_machines[0]) throw std::logic_error("linked cabinets are disconnected");
		for (model1 *machine : m_machines)
			if (machine && machine->scanline())
				throw std::logic_error("linked cabinets must start at a frame boundary");
		if (m_relay && m_relay->scanline())
			throw std::logic_error("linked cabinets must start at a frame boundary");
		if (m_relay) m_relay->observe(m_machines[0]);
		for (int line = 0; line < model1::FRAME_LINES; line++) {
			if (line == model1::FRAME_VBLANK && std::all_of(m_machines.begin(), m_machines.end(),
				[](auto *machine) { return machine->link()->ready(); })) {
				for (size_t p = 0; p < m_machines.size(); p++) {
					const auto &snapshot = m_machines[p]->link()->snapshot();
					if (!m_retired[p]) m_snapshots[p] = snapshot;
					else m_snapshots[p] = m_retired[p](m_snapshots[p], snapshot);
				}
				for (size_t p = 0; p < m_machines.size(); p++) {
					if (m_snapshots[p][1] != (p == 0 ? 1 : 2))
						throw std::logic_error("linked cabinets must be configured as master and slaves");
					if (m_machines.size() == 1) m_machines[p]->link()->tick(true);
					else if (m_retired[p]) {
						// The hidden cabinet still runs the game's clock, especially the master.
						auto local = m_snapshots;
						local[p] = m_machines[p]->link()->snapshot();
						m_machines[p]->link()->tick_group(unsigned(p + 1), local);
					} else m_machines[p]->link()->tick_group(unsigned(p + 1), m_snapshots);
				}
				if (m_relay && m_relay->link()->ready()) m_relay->link()->receive_group(m_snapshots);
			}
			for (model1 *machine : m_machines)
				if (machine) machine->run_scanline();
			if (m_relay) m_relay->run_scanline();
		}
	}
private:
	std::vector<model1 *> m_machines;
	model1 *m_relay;
	std::vector<m1comm::memory> m_snapshots;
	std::vector<std::function<m1comm::memory(m1comm::memory, const m1comm::memory &)>> m_retired;
};
