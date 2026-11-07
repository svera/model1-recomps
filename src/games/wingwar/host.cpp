#include "../../host/game_host.h"
#include "controls.h"
#include "romset.h"

namespace game::wingwar {
namespace {

class host final : public game_host
{
public:
	const romset::definition &romset() const override { return ROMSET; }
	const control_profile &controls() const override { return CONTROLS; }
};

}
}

std::unique_ptr<game_host> make_game_host() { return std::make_unique<game::wingwar::host>(); }
