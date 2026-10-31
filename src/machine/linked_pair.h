#pragma once
#include "model1.h"
#include <stdexcept>

// Both cabinets advance on one thread. Take both outgoing snapshots before either
// board applies the exchange, so execution order cannot change what the peer receives.
class linked_pair
{
public:
	linked_pair(model1 &first, model1 &second) : m_machines{&first, &second}
	{
		for (int p = 0; p < 2; p++)
			m_machines[p]->enable_link([this, p](const auto &, auto &peer) {
				peer = m_snapshots[1 - p];
				return true;
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
		if (!m_machines[0] || !m_machines[1]) throw std::logic_error("linked cabinets are disconnected");
		if (m_machines[0]->scanline() || m_machines[1]->scanline())
			throw std::logic_error("linked cabinets must start at a frame boundary");
		for (int line = 0; line < model1::FRAME_LINES; line++) {
			if (line == model1::FRAME_VBLANK && m_machines[0]->link()->ready() && m_machines[1]->link()->ready()) {
				for (int p = 0; p < 2; p++) m_snapshots[p] = m_machines[p]->link()->snapshot();
				if (m_snapshots[0][1] != 1 || m_snapshots[1][1] != 2)
					throw std::logic_error("linked cabinets must be configured as master and slave");
				for (model1 *machine : m_machines) machine->link()->tick();
			}
			for (model1 *machine : m_machines) machine->run_scanline();
		}
	}
private:
	std::array<model1 *, 2> m_machines;
	std::array<m1comm::memory, 2> m_snapshots{};
};
