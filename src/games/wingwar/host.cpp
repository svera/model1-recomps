#include "../../host/game_host.h"
#include "controls.h"
#include "gameplay.h"
#include "romset.h"

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

private:
	host_context *ctx = nullptr;
	bool lod = false;
	bool extended = true;
};

}
}

std::unique_ptr<game_host> make_game_host() { return std::make_unique<game::wingwar::host>(); }
