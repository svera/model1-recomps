#pragma once
#include "model1.h"
#include <stdexcept>

// One or two racers and an optional receive-only monitor advance on one thread. Take all
// outgoing snapshots before any board receives them to avoid execution-order effects.
class linked_pair
{
public:
	linked_pair(model1 &first, model1 &second, model1 *relay = nullptr) : linked_pair(first, &second, relay) {}
	linked_pair(model1 &first, model1 *second, model1 *relay) : m_machines{&first, second, relay}
	{
		if ((!second && !relay) || &first == second || relay == &first || (relay && relay == second))
			throw std::invalid_argument("linked cabinets must be distinct machines");
		for (int p = 0; p < 2; p++)
			if (m_machines[p]) m_machines[p]->enable_link([this, p](const auto &, auto &peer) {
				peer = m_snapshots[1 - p];
				return true;
			}, true);
		if (relay)
			relay->enable_link([](const auto &, auto &) -> bool {
				throw std::logic_error("a relay must not publish racing data");
			}, true);
	}
	linked_pair(const linked_pair &) = delete;
	linked_pair &operator=(const linked_pair &) = delete;
	~linked_pair() { disconnect(); }
	void disconnect()
	{
		for (model1 *&machine : m_machines)
			if (machine) { machine->disable_link(); machine = nullptr; }
	}
	void run_frame()
	{
		if (!m_machines[0]) throw std::logic_error("linked cabinets are disconnected");
		for (model1 *machine : m_machines)
			if (machine && machine->scanline())
				throw std::logic_error("linked cabinets must start at a frame boundary");
		for (int line = 0; line < model1::FRAME_LINES; line++) {
			if (line == model1::FRAME_VBLANK && m_machines[0]->link()->ready() &&
				(!m_machines[1] || m_machines[1]->link()->ready())) {
				for (int p = 0; p < 2; p++)
					if (m_machines[p]) m_snapshots[p] = m_machines[p]->link()->snapshot();
				if (m_snapshots[0][1] != 1 || (m_machines[1] && m_snapshots[1][1] != 2))
					throw std::logic_error("linked cabinets must be configured as master and slave");
				for (int p = 0; p < 2; p++)
					if (m_machines[p]) m_machines[p]->link()->tick(!m_machines[1]);
				if (m_machines[2] && m_machines[2]->link()->ready())
					m_machines[2]->link()->receive(m_snapshots[0], m_machines[1] ? &m_snapshots[1] : nullptr);
			}
			for (model1 *machine : m_machines)
				if (machine) machine->run_scanline();
		}
	}
private:
	std::array<model1 *, 3> m_machines;
	std::array<m1comm::memory, 2> m_snapshots{};
};
