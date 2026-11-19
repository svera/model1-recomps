#pragma once
#include "../../host/local_cabinet_link.h"
#include "../../machine/linked_pair.h"
#include <array>

namespace game::wingwar {

class local_link final : public local_cabinet_link
{
public:
	local_link(model1 &first, model1 &second) : pair(first, second), machines{&first, &second} {}
	void run_frame() override { pair.run_frame(); }
	local_link_status status(unsigned cabinet) const override
	{
		if (cabinet >= machines.size() || !machines[cabinet]->link()) return {};
		const auto &board = *machines[cabinet]->link();
		return {board.read(2), board.read(3), board.ticks()};
	}

private:
	linked_pair pair;
	std::array<model1 *, 2> machines;
};

}
