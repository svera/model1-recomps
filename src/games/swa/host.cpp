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
		const auto it = settings.find("draw_distance");
		if (it == settings.end()) return;
		if (it->second == "arcade" || it->second == "extended") full_distance = it->second == "extended";
		else fprintf(stderr, "SWA: invalid draw_distance '%s'; expected arcade or extended\n", it->second.c_str());
	}
	void save_settings(FILE *f) const override
	{
		fprintf(f, "invert_flight_y = %s\n", invert_y ? "on" : "off");
		fprintf(f, "gunner_assist = %s\n", assist ? "on" : "off");
		fprintf(f, "draw_distance = %s\n", full_distance ? "extended" : "arcade");
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
		// renderer-only: the game runs the same either way, so replays keep it
		set_full_draw_distance(context.machine, full_distance);
	}

	std::vector<menu_item> display_menu_items() override
	{
		std::vector<menu_item> items{ { "Draw distance", "Full draws the Death Star's surface about twice as far as the arcade and lights its "
														 "distant buildings, in the renderer only, without changing the game or its timing. "
														 "Full can lower performance.",
			[this] { return full_distance ? "Full" : "Arcade"; },
			[this](int) {
				full_distance = !full_distance;
				set_full_draw_distance(ctx->machine, full_distance);
				ctx->settings_changed();
			} } };
		if (ctx->replay)
			items.push_back({ "LOD", "Input replay and recording use the arcade's models, so recordings verify.",
				[] { return "On (replay)"; }, [](int) {} });
		else
			items.push_back({ "LOD", "Off keeps the highest-detail models at every distance. On uses the arcade's simpler distant models. "
									 "Off can lower performance.",
				[this] { return lod ? "On" : "Off"; },
				[this](int) {
					lod = !lod;
					set_lod(ctx->machine, lod);
					ctx->settings_changed();
				} });
		return items;
	}

	std::vector<menu_item> game_menu_items() override
	{
		return { { "Invert flight Y", "Off: as the cabinet, pushing the flight stick forward dives. On: forward climbs. "
										"Both seats' sticks, pads and keys.",
			[this] { return invert_y ? "On" : "Off"; },
			[this](int) {
				invert_y = !invert_y;
				ctx->settings_changed();
			} } };
	}

	std::vector<menu_item> menu_items_after_controls() override
	{
		return { { "Cheats", "Gunner assist.", {}, {}, [this] { ctx->push_page(cheats_page()); } } };
	}

private:
	menu_page cheats_page()
	{
		menu_page p{ "Cheats", "", {} };
		p.items.push_back({ "Gunner assist", "On: in PILOT & GUNNER mode, the Gunner's stick and laser aim at the nearest moving "
											 "enemy in reach, for one player to fly with both seats firing. Only the cabinet's "
											 "inputs: recordings replay without it.",
			[this] { return assist ? "On" : "Off"; },
			[this](int) {
				assist = !assist;
				ctx->settings_changed();
			} });
		p.items.push_back(ctx->back_item());
		return p;
	}

	host_context *ctx = nullptr;
	bool invert_y = false;
	bool assist = false;
	bool lod = false;
	bool full_distance = true;
	gunner_assist gunner;
};

}
}

std::unique_ptr<game_host> make_game_host()
{
	return std::make_unique<game::swa::host>();
}
