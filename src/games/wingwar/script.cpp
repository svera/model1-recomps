#include "../script.h"
#include "../../host/input_replay.h"
#include "../../machine/model1.h"
#include "profile.h"

namespace game::wingwar {

// the recorded human runs the "recording" and "complete" scenarios replay, from the repository's folder:
// a playthrough without cheats, and a run to the end of the credits (invincibility in the UFO stage)
constexpr const char *RECORDING = "tests/games/wingwar/recordings/manual-playthrough.inputs",
	*COMPLETE_RECORDING = "tests/games/wingwar/recordings/manual-complete-game.inputs";
namespace {

class play final : public auto_play
{
public:
	play(bool boot, bool checks) : m_boot(boot), m_checks(checks) {}

	script_inputs frame(int) override
	{
		script_inputs in;
		in.analog[0] = 0x80;
		in.analog[1] = 0x80;
		in.analog[2] = 0x01;
		return in;
	}

	std::string check(int f, model1 &m) override
	{
		if (!m_checks) return {};
		m_frames = f;
		if (!m.real_ioboard()) return "Wing War script requires the Advanced I/O board ROM";
		if (!m.tgp_is_lle()) return "Wing War script requires the real TGP program";
		if (!m.m_tgp_missing.empty()) return "Wing War script encountered an unhandled TGP call";
		if (m.frame_quads() > 0) {
			m_geometry++;
			if (f > 700) m_attract_geometry++;
		}
		if (f == 700) {
			if (m.io_board_byte(0x1a) != 'S' || m.io_board_byte(0x1b) != 'E' ||
				m.io_board_byte(0x1c) != 'G' || m.io_board_byte(0x1d) != 'A')
				return "Wing War boot did not complete the I/O board handshake";
			if (!m.advanced_ioboard_cycles() || !m.tgp_cycles_executed() || m_geometry < 100)
				return "Wing War boot did not reach sustained geometry submission";
			m_booted = true;
		}
		return {};
	}

	std::string finish() override
	{
		if (!m_checks) return {};
		if (!m_booted || m_frames < min_frames()) return "Wing War script ended before its boot/attract checkpoint";
		if (!m_boot && m_attract_geometry < 100) return "Wing War attract run did not sustain geometry submission";
		return {};
	}

	int min_frames() const override { return m_checks ? (m_boot ? 700 : 7200) : 0; }

private:
	bool m_boot, m_checks, m_booted = false;
	int m_frames = 0, m_geometry = 0, m_attract_geometry = 0;
};

}

std::unique_ptr<auto_play> make_auto_play(const std::string &scenario, uint32_t, bool checks)
{
	if (scenario == "recording" || scenario == "complete")
		return input_replay::recorded_scenario(scenario == "complete" ? COMPLETE_RECORDING : RECORDING, ID, "Wing War");
	if (scenario != "" && scenario != "attract" && scenario != "boot") return nullptr;
	return std::make_unique<play>(scenario == "boot", checks);
}

}
