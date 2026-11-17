#pragma once
#include "../../machine/m1comm.h"
#include <algorithm>

namespace game::vr {

inline m1comm::memory retire_lan_snapshot(m1comm::memory snapshot)
{
	// FE19A1 still copies the car's pose and model pointers until the race ends.
	std::fill(snapshot.begin() + 0x10 + 0xb7, snapshot.end(), 0);
	snapshot[1] = 2;
	snapshot[4] = 1;
	return snapshot;
}

}
