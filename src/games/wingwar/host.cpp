#include "../../host/game_host.h"
#include "controls.h"
#include "gameplay.h"
#include "romset.h"
#include "assignments.h"
#include <charconv>

namespace game::wingwar {
namespace {

class host final : public game_host
{
public:
	const romset::definition &romset() const override { return ROMSET; }
	const control_profile &controls() const override { return CONTROLS; }

	void load_settings(const settings_map &settings) override
	{
		if (const auto it = settings.find("arcade_coins"); it != settings.end()) {
			int64_t value;
			const auto &text = it->second;
			const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value, 16);
			if (parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() && valid_coin_setting(value))
				arcade_coin = value;
			else fprintf(stderr, "Wing War: invalid arcade_coins '%s'; expected a valid six-byte hexadecimal coin setting\n", text.c_str());
		}
		if (const auto it = settings.find("draw_distance"); it != settings.end()) {
			if (it->second == "arcade" || it->second == "extended") extended = it->second == "extended";
			else fprintf(stderr, "Wing War: invalid draw_distance '%s'; expected arcade or extended\n", it->second.c_str());
		}
		if (const auto it = settings.find("lod"); it != settings.end()) {
			if (it->second == "on" || it->second == "off") lod = it->second == "on";
			else fprintf(stderr, "Wing War: invalid lod '%s'; expected on or off\n", it->second.c_str());
		}
		if (ctx) {
			set_extended_draw_distance(ctx->machine, extended);
			set_lod(ctx->machine, lod);
		}
	}

	void save_settings(FILE *f) const override
	{
		fprintf(f, "draw_distance = %s\n", extended ? "extended" : "arcade");
		fprintf(f, "lod = %s\n", lod ? "on" : "off");
		if (arcade_coin >= 0) fprintf(f, "arcade_coins = %012llx\n", (long long)arcade_coin);
	}

	std::vector<uint8_t> new_eeprom(model1 &machine) override { return game::wingwar::new_eeprom(machine); }

	void start(host_context &context) override
	{
		ctx = &context;
		assignments_changed = false;
		if (context.replay) lod = false;
		set_extended_draw_distance(context.machine, extended);
		set_lod(context.machine, lod);
	}

	bool has_play_style() const override { return true; }

	void skip_boot() override
	{
		bool busy_seen = false;
		for (int frame = 0; frame < 900; frame++) {
			ctx->machine.run_frame();
			busy_seen |= ctx->machine.read8(0x40f004) != 0;
			if (busy_seen && ctx->machine.read8(0x40f004) == 0 && ctx->machine.read8(0x40f006) != 0) return;
		}
		fprintf(stderr, "Wing War: PC boot did not complete\n");
		std::abort();
	}

	void keep_arcade_settings() override
	{
		// (a recording's restart writes them, after the recording is saved)
		if (ctx->replay) return;
		apply_assignments();
		const auto now = coin_setting(ctx->machine);
		if (!ctx->pc_mode && now >= 0 && now != arcade_coin) {
			arcade_coin = now;
			ctx->settings_changed();
		}
	}

	void play_style_changed() override
	{
		if (!ctx->replay) apply_coin_setting();
	}

	bool booted() override
	{
		if (ctx->replay) return false;
		return apply_coin_setting();
	}

	std::vector<menu_item> display_menu_items() override
	{
		return { { "Draw distance", "Full draws missing static scenery directly in the renderer, without changing game timing or collisions. Aircraft and other moving objects keep their original visibility. Full can lower performance. Applies to the next scenery list.",
			[this] { return extended ? "Full" : "Arcade"; },
			[this](int) {
				extended = !extended;
				set_extended_draw_distance(ctx->machine, extended);
				ctx->settings_changed();
			} },
			{ "LOD", "Off keeps the highest-detail single-piece scenery models. On uses the arcade's simpler distant models within its visibility range. Full draw distance adds single-piece scenery independently of LOD. Off can lower performance.",
			[this] { return lod ? "On" : "Off"; },
			[this](int) {
				lod = !lod;
				set_lod(ctx->machine, lod);
				ctx->settings_changed();
			} } };
	}

	std::vector<menu_item> game_menu_items() override
	{
		std::vector<menu_item> items;
		load_choices();
		for (size_t i = 0; i < ASSIGNMENTS.size() - 1; i++) items.push_back(assignment_item(i));
		items.push_back({ "Service menu",
			"The arcade's own test menu: cabinet settings and diagnostics. Use 9 to move and F2 to select. Choose EXIT to return.",
			{}, {}, [this] { ctx->service_menu(); } });
		return items;
	}

	std::vector<menu_item> sound_menu_items() override
	{
		load_choices();
		return {assignment_item(6)};
	}

	std::vector<menu_item> menu_items_after_controls() override
	{
		return { { "Cheats", "Optional invincibility.", {}, {},
			[this] {
				menu_page page{"Cheats", "", {}};
				page.items.push_back({ "Invincibility",
					"On prevents damage to your aircraft only; CPU opponents remain vulnerable. "
					"Does not change flight controls, restore lost health or stop the stage timer. "
					"Recorded per frame for deterministic replay. Off by default; resets when the game restarts.",
					[this] { return ctx->machine.cheats() & INVINCIBILITY ? "On" : "Off"; },
					[this](int) {
						if (!ctx->machine.set_cheats(ctx->machine.cheats() ^ INVINCIBILITY)) {
							fprintf(stderr, "Wing War: invincibility is unavailable\n");
							std::abort();
						}
					} });
				page.items.push_back(ctx->back_item());
				ctx->push_page(std::move(page));
			} } };
	}

	// Changed assignments restart the game, which reads them as it starts. They are written at the
	// restart itself, after a recording (whose RAM hash covers the settings) has been saved.
	restart menu_closed() override { return assignments_changed ? restart::game : restart::none; }

	bool before_restart() override
	{
		apply_assignments();
		return false;
	}

private:
	bool apply_coin_setting()
	{
		const auto now = coin_setting(ctx->machine);
		if (now < 0) {
			if (assignment_value(ctx->machine, 0) >= 0) {
				fprintf(stderr, "Wing War: invalid cabinet coin setting\n");
				std::abort();
			}
			return false; // Cold boot has not initialized cabinet settings yet.
		}
		if (ctx->pc_mode && (now & 0xff) != FREE_PLAY && now != arcade_coin) {
			arcade_coin = now;
			ctx->settings_changed();
		}
		auto target = ctx->pc_mode ? free_play_coin_setting(now) : arcade_coin >= 0 ? arcade_coin : now;
		if (!ctx->pc_mode && arcade_coin < 0 && (now & 0xff) == FREE_PLAY) {
			const auto &rom = ctx->machine.program_rom();
			if (rom.size() < 0x12c17) {
				fprintf(stderr, "Wing War: default coinage table is missing\n");
				std::abort();
			}
			target = now & 0x3f00;
			for (int i = 0; i < 4; i++) target |= int64_t(rom[0x12c13 + i]) << (8 * (i + 2));
		}
		if (target == now) return false;
		if (!set_coin_setting(ctx->machine, target)) {
			fprintf(stderr, "Wing War: failed to apply play-style coin setting\n");
			std::abort();
		}
		return true;
	}

	void apply_assignments()
	{
		if (!assignments_changed) return;
		assignments_changed = false;
		if (!set_assignments(ctx->machine, choices))
			fprintf(stderr, "Wing War: failed to save game assignments; cabinet settings are invalid or the save counter is exhausted\n");
	}

	void load_choices()
	{
		if (!assignments_changed)
			for (size_t i = 0; i < ASSIGNMENTS.size(); i++) choices[i] = assignment_value(ctx->machine, i);
	}

	menu_item assignment_item(size_t i)
	{
		return {i == 0 ? "Game difficulty (1 easy - 8 hard)" : ASSIGNMENTS[i].name,
				i == 0
					? "1 is the easiest; 8 is the hardest. Higher levels make the game harder. Changes apply when this menu closes and restart the game."
					: "The arcade's Game Assignments setting. Changes apply when this menu closes and restart the game.",
				[this, i] {
					load_choices();   // (the game may set up its settings while the menu is open)
					if (choices[i] < 0) return std::string("Not ready");
					if (i == 6) return std::string(choices[i] ? "Off" : "On");
					return std::to_string(ASSIGNMENTS[i].first + choices[i] * ASSIGNMENTS[i].step);
				},
				[this, i](int direction) {
					load_choices();
					if (choices[i] < 0) {
						fprintf(stderr, "Wing War: cannot change game assignments before the game sets up its settings\n");
						return;
					}
					const int count = ASSIGNMENTS[i].count;
					choices[i] = ((choices[i] + direction) % count + count) % count;
					assignments_changed = true;
				}};
	}
	host_context *ctx = nullptr;
	bool lod = false;
	bool extended = true;
	std::array<int, 7> choices{};
	bool assignments_changed = false;
	int64_t arcade_coin = -1;
};

}
}

std::unique_ptr<game_host> make_game_host() { return std::make_unique<game::wingwar::host>(); }
