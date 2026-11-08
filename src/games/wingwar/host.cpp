#include "../../host/game_host.h"
#include "controls.h"
#include "gameplay.h"
#include "romset.h"
#include "assignments.h"

namespace game::wingwar {
namespace {

class host final : public game_host
{
public:
	const romset::definition &romset() const override { return ROMSET; }
	const control_profile &controls() const override { return CONTROLS; }

	void load_settings(const settings_map &settings) override
	{
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
	}

	void start(host_context &context) override
	{
		ctx = &context;
		assignments_changed = false;
		if (context.replay) lod = false;
		set_extended_draw_distance(context.machine, extended);
		set_lod(context.machine, lod);
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

	restart menu_closed() override
	{
		if (!assignments_changed) return restart::none;
		assignments_changed = false;
		if (ctx->replay || !set_assignments(ctx->machine, choices)) {
			fprintf(stderr, "Wing War: failed to save game assignments; replay is active, cabinet settings are invalid, or the save counter is exhausted\n");
			return restart::none;
		}
		return restart::game;
	}

private:
	void load_choices()
	{
		if (!assignments_changed)
			for (size_t i = 0; i < ASSIGNMENTS.size(); i++) choices[i] = assignment_value(ctx->machine, i);
	}

	menu_item assignment_item(size_t i)
	{
		return {i == 0 ? "Game difficulty (1 easy - 8 hard)" : ASSIGNMENTS[i].name,
				i == 0
					? "1 is the easiest; 8 is the hardest. Higher levels make the game harder. Changes apply when this menu closes and restart the game. Input recording/replay keeps cabinet assignments unchanged."
					: "The arcade's Game Assignments setting. Changes apply when this menu closes and restart the game. Input recording/replay keeps cabinet assignments unchanged.",
				[this, i] {
					if (choices[i] < 0) return std::string("Not ready");
					if (i == 6) return std::string(choices[i] ? "Off" : "On");
					return std::to_string(ASSIGNMENTS[i].first + choices[i] * ASSIGNMENTS[i].step);
				},
				[this, i](int direction) {
					if (ctx->replay || choices[i] < 0) {
						fprintf(stderr, "Wing War: cannot change game assignments during replay or before settings load\n");
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
};

}
}

std::unique_ptr<game_host> make_game_host() { return std::make_unique<game::wingwar::host>(); }
