#include "../../host/game_host.h"
#include "controls.h"
#include "gameplay.h"
#include "gunner_assist.h"
#include "profile.h"
#include "romset.h"
#include <array>
#include <optional>

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
		for (const auto &[key, flag] : { std::pair{ "invert_flight_y", &invert_y }, std::pair{ "gunner_assist", &assist }, std::pair{ "lod", &lod },
				 std::pair{ "wide_cockpit", &wide_cockpit } }) {
			const auto it = settings.find(key);
			if (it == settings.end()) continue;
			if (it->second == "on" || it->second == "off") *flag = it->second == "on";
			else fprintf(stderr, "SWA: invalid %s '%s'; expected on or off\n", key, it->second.c_str());
		}
		if (const auto it = settings.find("draw_distance"); it != settings.end()) {
			if (it->second == "arcade" || it->second == "extended") full_distance = it->second == "extended";
			else fprintf(stderr, "SWA: invalid draw_distance '%s'; expected arcade or extended\n", it->second.c_str());
		}
		if (const auto it = settings.find("arcade_coins"); it != settings.end()) {
			char *end = nullptr;
			const long long value = strtoll(it->second.c_str(), &end, 16);
			if (it->second.size() == 10 && end && !*end && value >= 0) arcade_coin = value;
			else fprintf(stderr, "SWA: invalid arcade_coins '%s'; expected 10 hex digits\n", it->second.c_str());
		}
	}
	void save_settings(FILE *f) const override
	{
		fprintf(f, "invert_flight_y = %s\n", invert_y ? "on" : "off");
		fprintf(f, "gunner_assist = %s\n", assist ? "on" : "off");
		fprintf(f, "draw_distance = %s\n", full_distance ? "extended" : "arcade");
		fprintf(f, "lod = %s\n", lod ? "on" : "off");
		fprintf(f, "wide_cockpit = %s\n", wide_cockpit ? "on" : "off");
		if (arcade_coin >= 0) fprintf(f, "arcade_coins = %010llx\n", (long long)arcade_coin);
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
		set_wide_cockpit(context.machine, wide_cockpit);
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
		items.push_back({ "Widescreen cockpit", "On: at aspect ratios wider than 4:3, the X-wing's cockpit continues out to the "
												"screen's sides, its canopy struts and consoles extended along their own lines rather "
												"than stretched. Off: the arcade's 4:3 cockpit in the middle.",
			[this] { return wide_cockpit ? "On" : "Off"; },
			[this](int) {
				wide_cockpit = !wide_cockpit;
				set_wide_cockpit(ctx->machine, wide_cockpit);
				ctx->settings_changed();
			} });
		return items;
	}

	std::vector<menu_item> game_menu_items() override
	{
		std::vector<menu_item> items{ { "Invert flight Y", "Off: as the cabinet, pushing the flight stick forward dives. On: forward climbs. "
										"Both seats' sticks, pads and keys.",
			[this] { return invert_y ? "On" : "Off"; },
			[this](int) {
				invert_y = !invert_y;
				ctx->settings_changed();
			} },
			{ "Service menu",
				"The arcade's own test menu: memory, input, output, sound and screen tests, game and coin settings "
				"and bookkeeping. Use 9 to move and F2 to select. Choose EXIT to return.",
				{}, {}, [this] { ctx->service_menu(); } } };
		if (!assignments_before) {
			std::array<int, 4> values;
			for (size_t i = 0; i < values.size(); i++) values[i] = game_assignment_value(ctx->machine, game_assignment(i));
			assignments_before = values;
			assignment_choices = values;
		}
		static const std::array<const char *, 4> names{ "Difficulty (player)", "Difficulty (enemy)", "Difficulty (timer)",
			"Advertise sound" };
		static const std::array<const char *, 4> help{
			"The test menu's GAME DIFFICULTY #1 (PLAYER): the player side. 1/5 is the easiest, 3/5 the arcade's default. "
			"Changing it restarts the game when the menu closes.",
			"The test menu's GAME DIFFICULTY #2 (ENEMY): the enemies. 1/5 is the easiest, 3/5 the arcade's default. "
			"Changing it restarts the game when the menu closes.",
			"The test menu's GAME DIFFICULTY #3 (TIMER): the time limits. 1/5 is the easiest, 3/5 the arcade's default. "
			"Changing it restarts the game when the menu closes.",
			"The test menu's ADVERTISE SOUND: sound and music in the attract mode. Changing it restarts the game when the menu closes."
		};
		static const std::array<std::vector<const char *>, 4> choices{ {
			{ "1/5", "2/5", "3/5", "4/5", "5/5" },
			{ "1/5", "2/5", "3/5", "4/5", "5/5" },
			{ "1/5", "2/5", "3/5", "4/5", "5/5" },
			{ "Off", "On" },
		} };
		for (size_t i = 0; i < names.size(); i++)
			items.insert(items.end() - 1, { names[i], help[i],
				[this, i] {
					const int value = assignment_choices[i];
					return std::string(value < 0 ? "-" : choices[i][value]);
				},
				[this, i](int d) {
					auto &value = assignment_choices[i];
					if (value < 0) {
						fprintf(stderr, "SWA: cannot change %s before the game loads its settings\n", names[i]);
						return;
					}
					const int count = int(choices[i].size());
					value = ((value + d) % count + count) % count;
				} });
		return items;
	}

	bool has_play_style() const override { return true; }

	// Arcade mode's coin setting as it is now (the service menu may have changed it), kept for when
	// PC mode's free play replaces it
	void keep_arcade_settings() override
	{
		apply_settings();
		const int64_t now = coin_setting(ctx->machine);
		if (!ctx->pc_mode && now >= 0 && now != arcade_coin) {
			arcade_coin = now;
			ctx->settings_changed();
		}
	}

	// The play style's coins: PC mode is free play (COIN/CREDIT SETTING #26); Arcade mode has the
	// cabinet's own setting, kept in settings.ini while free play is in its place. The game reads
	// them at boot: when they change, boot again.
	bool booted() override
	{
		assignments_before.reset();
		model1 &m = ctx->machine;
		// (SWA's boot isn't skipped, so this is straight after the reset: with no settings saved yet,
		// let the game load its defaults first)
		for (int i = 0; i < 60 && coin_setting(m) < 0; i++) {
			m.set_drawing(false);
			m.run_frame();
			m.set_drawing(true);
			m.m_audio_out.clear();
		}
		const int64_t now = coin_setting(m);
		if (now < 0) return false;
		if (ctx->pc_mode) {
			if (!is_free_play(now) && now != arcade_coin) {
				arcade_coin = now;
				ctx->settings_changed();
			}
			return set_coin_setting(m, with_coin_entry(now, COIN_SETTING_FREE_PLAY));
		}
		// Arcade: its own setting back; free play left by PC mode before one was kept: setting #1
		return set_coin_setting(m, arcade_coin >= 0 ? arcade_coin : is_free_play(now) ? with_coin_entry(now, COIN_SETTING_1) : now);
	}

	restart menu_closed() override { return apply_settings() ? restart::game : restart::none; }

	std::vector<menu_item> menu_items_after_controls() override
	{
		return { { "Cheats", "Gunner assist.", {}, {}, [this] { ctx->push_page(cheats_page()); } } };
	}

private:
	bool apply_settings()
	{
		bool changed = false;
		if (assignments_before) {
			for (size_t i = 0; i < assignment_choices.size(); i++) {
				if (assignment_choices[i] == (*assignments_before)[i]) continue;
				if (set_game_assignment(ctx->machine, game_assignment(i), assignment_choices[i])) changed = true;
				else fprintf(stderr, "SWA: could not apply game assignment %zu\n", i);
			}
			assignments_before.reset();
		}
		return changed;
	}

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
	std::optional<std::array<int, 4>> assignments_before;
	std::array<int, 4> assignment_choices{};
	int64_t arcade_coin = -1;
	bool invert_y = false;
	bool assist = false;
	bool lod = false;
	bool full_distance = true;
	bool wide_cockpit = true;
	gunner_assist gunner;
};

}
}

std::unique_ptr<game_host> make_game_host()
{
	return std::make_unique<game::swa::host>();
}
