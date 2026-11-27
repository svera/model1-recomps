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

inline m1comm::memory retire_local_snapshot(m1comm::memory frozen, const m1comm::memory &live)
{
	auto retired = retire_lan_snapshot(frozen);
	if (live[1] == 1) {
		// FE1670 reads the master record's race roster, timer and lap status.
		std::copy(live.begin() + 0x10 + 0xb8, live.begin() + 0x10 + 0x1c4,
			retired.begin() + 0x10 + 0xb8);
		retired[0x10 + 0x10e] = 0;
	}
	retired[1] = live[1];
	std::copy_n(live.begin() + 6, 10, retired.begin() + 6);
	return retired;
}

}
