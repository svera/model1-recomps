#include "../../host/game_host.h"
#include "controls.h"
#include "gameplay.h"
#include "gunner_assist.h"
#include "profile.h"
#include "romset.h"

namespace game::swa {
namespace {

class host final : public game_host
{
public:
	const romset::definition &romset() const override { return ROMSET; }
	const control_profile &controls() const override { return CONTROLS; }

	uint8_t analog_key_input(model1 &machine, int channel, bool low, bool high, uint8_t value) override
	{
		value = game_host::analog_key_input(machine, channel, low, high, value);
		return invert_y && (channel == 1 || channel == 5) ? uint8_t(255 - value) : value;
	}

	void load_settings(const settings_map &settings) override
	{
		for (const auto &[key, flag] : { std::pair{ "invert_flight_y", &invert_y }, std::pair{ "gunner_assist", &assist }, std::pair{ "lod", &lod } }) {
			const auto it = settings.find(key);
			if (it == settings.end()) continue;
			if (it->second == "on" || it->second == "off") *flag = it->second == "on";
			else fprintf(stderr, "SWA: invalid %s '%s'; expected on or off\n", key, it->second.c_str());
		}
	}
	void save_settings(FILE *f) const override
	{
		fprintf(f, "invert_flight_y = %s\n", invert_y ? "on" : "off");
		fprintf(f, "gunner_assist = %s\n", assist ? "on" : "off");
		fprintf(f, "lod = %s\n", lod ? "on" : "off");
	}

	void before_frame() override
	{
		if (assist) gunner.aim(ctx->machine);
	}

	void start(host_context &context) override
	{
		ctx = &context;
		// a replay (and the recording made from it) keeps the arcade's models: LOD off draws more,
		// which moves where a frame ends in the game's update and so the RAM hash a recording checks
		set_lod(context.machine, lod || context.replay);
	}

	std::vector<menu_item> display_menu_items() override
	{
		if (ctx->replay)
			return { { "LOD", "Input replay and recording use the arcade's models, so recordings verify.",
				[] { return "On (replay)"; }, [](int) {} } };
		return { { "LOD", "Off keeps the highest-detail models at every distance. On uses the arcade's simpler distant models. "
						  "Off can lower performance.",
			[this] { return lod ? "On" : "Off"; },
			[this](int) {
				lod = !lod;
				set_lod(ctx->machine, lod);
				ctx->settings_changed();
			} } };
	}

	std::vector<menu_item> game_menu_items() override
	{
		return { { "Invert flight Y", "Off: as the cabinet, pushing the flight stick forward dives. On: forward climbs. "
										"Both seats' sticks, pads and keys.",
			[this] { return invert_y ? "On" : "Off"; },
			[this](int) {
				invert_y = !invert_y;
				ctx->settings_changed();
			} },
			{ "Gunner assist", "On: in PILOT & GUNNER mode, the Gunner's stick and laser aim at the nearest moving "
								"enemy in reach, for one player to fly with both seats firing. Only the cabinet's "
								"inputs: recordings replay without it.",
			[this] { return assist ? "On" : "Off"; },
			[this](int) {
				assist = !assist;
				ctx->settings_changed();
			} } };
	}

private:
	host_context *ctx = nullptr;
	bool invert_y = false;
	bool assist = false;
	bool lod = false;
	gunner_assist gunner;
};

}
}

std::unique_ptr<game_host> make_game_host()
{
	return std::make_unique<game::swa::host>();
}
