#include "profile.h"
#include "gameplay.h"
#include "../script.h"

namespace game::wingwar {

std::unique_ptr<auto_play> make_auto_play(const std::string &scenario, uint32_t seed, bool checks);

namespace {
const registration REGISTERED{ { &PROFILE, &IO_PROFILE, &VIDEO, nullptr, create_gameplay, make_auto_play } };
}

}
