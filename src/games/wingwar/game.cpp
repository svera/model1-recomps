#include "profile.h"
#include "gameplay.h"

namespace game::wingwar {

namespace {
const registration REGISTERED{ { &PROFILE, &IO_PROFILE, &VIDEO, nullptr, create_gameplay } };
}

}
