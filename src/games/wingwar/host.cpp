#include "../../host/game_host.h"
#include "controls.h"
#include "gameplay.h"
#include "link.h"
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
	const local_multiplayer_info *local_multiplayer() const override
	{
		static const local_multiplayer_info info{
			false, "Game and service settings.", "Game settings shared by both cabinets.",
			"Cabinet game and service settings.", "waiting for the demonstration or game selection",
			"Change this player's flight controls."
		};
		return &info;
	}
	std::unique_ptr<local_cabinet_link> make_local_link(model1 &first, model1 *second, model1 *monitor) override
	{
		if (!second || monitor) {
			fprintf(stderr, "Wing War: local link requires two cabinets and no monitor\n");
			return {};
		}
		return std::make_unique<local_link>(first, *second);
	}
	local_multiplayer_status local_status(model1 &machine) const override
	{
		const auto *board = machine.link();
		return {machine.read8(0x40f006), board ? board->read(1) : 0u,
			board ? board->read(3) : 0u, machine.read8(0x40f007)};
	}
	void begin_local_boot() override { local_boot_busy = false; }
	bool local_boot_complete(model1 &machine) const override
	{
		const bool busy = machine.read8(0x40f004) != 0;
		local_boot_busy |= busy;
		return local_boot_busy && !busy && machine.read8(0x40f006) != 0;
	}
	bool can_join_local_cabinet() const override
	{
		if (!ctx) return false;
		const auto phase = ctx->machine.read8(0x40f006);
		return phase == 2 || (phase == 0x0c && ctx->machine.read8(0x40f007) == 3);
	}
	bool sync_shared_settings(model1 &primary) override
	{
		std::array<int, 7> values;
		for (size_t i = 0; i < values.size(); i++) {
			values[i] = assignment_value(primary, i);
			if (values[i] < 0) {
				fprintf(stderr, "Wing War: cannot synchronize invalid game assignments\n");
				return false;
			}
		}
		bool changed = false;
		for (size_t i = 0; i < values.size(); i++)
			changed |= assignment_value(ctx->machine, i) != values[i];
		if (!changed) return false;
		if (!set_assignments(ctx->machine, values)) {
			fprintf(stderr, "Wing War: cannot synchronize game assignments\n");
			std::abort();
		}
		return true;
	}

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
		bool changed = false;
		if (ctx->standalone_cabinet || ctx->link_player) {
			const int target = ctx->standalone_cabinet ? 0 : ctx->link_player;
			const int current = link_role(ctx->machine);
			if (current == -2) {
				fprintf(stderr, "Wing War: invalid cabinet link settings\n");
				std::abort();
			}
			if (current >= 0 && current != target) {
				if (!set_link_role(ctx->machine, target)) {
					fprintf(stderr, "Wing War: could not configure cabinet link role %d\n", target);
					std::abort();
				}
				changed = true;
			}
		}
		return apply_coin_setting() || changed;
	}

	std::vector<menu_item> display_quality_menu_items() override
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
	std::vector<menu_item> shared_game_menu_items() override
	{
		auto items = game_menu_items();
		items.pop_back();
		return items;
	}
	std::vector<menu_item> cabinet_game_menu_items() override
	{
		auto items = game_menu_items();
		return {std::move(items.back())};
	}

	std::vector<menu_item> sound_menu_items() override
	{
		load_choices();
		return {assignment_item(6)};
	}

	std::vector<menu_item> menu_items_after_controls() override
	{
		return { { "Cheats", "Optional invincibility and infinite missiles.", {}, {},
			[this] {
				menu_page page{"Cheats", "", {}};
				page.items.push_back({ "Invincibility",
					"On prevents damage and hit-induced spinning for your aircraft only; CPU opponents are unchanged. "
					"Releases an active hit reaction without changing normal flight controls, "
					"restore lost health or stop the stage timer. "
					"Recorded per frame for deterministic replay. Off by default; resets when the game restarts.",
					[this] { return ctx->machine.cheats() & INVINCIBILITY ? "On" : "Off"; },
					[this](int) {
						if (!ctx->machine.set_cheats(ctx->machine.cheats() ^ INVINCIBILITY)) {
							fprintf(stderr, "Wing War: invincibility is unavailable\n");
							std::abort();
						}
					} });
				page.items.push_back({ "Infinite missiles",
					"On replenishes your aircraft's missile magazine, even if empty. CPU opponents are unchanged. "
					"Normal launch and reload timing still apply. Off leaves the remaining missiles available. "
					"Recorded per frame; off by default and resets when the game restarts.",
					[this] { return ctx->machine.cheats() & INFINITE_MISSILES ? "On" : "Off"; },
					[this](int) {
						if (!ctx->machine.set_cheats(ctx->machine.cheats() ^ INFINITE_MISSILES)) {
							fprintf(stderr, "Wing War: infinite missiles are unavailable\n");
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

	void before_frame() override
	{
		const bool pressed = !(ctx->machine.m_inputs[1] & VIEW_CYCLE_INPUT);
		ctx->machine.m_inputs[1] |= VIEW_CYCLE_INPUT;
		views.update(ctx->machine, pressed);
	}

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
	view_cycle views;
	mutable bool local_boot_busy = false;
};

}
}

std::unique_ptr<game_host> make_game_host() { return std::make_unique<game::wingwar::host>(); }
