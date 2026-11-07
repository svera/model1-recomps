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
		if (const auto it = settings.find("lod"); it != settings.end()) {
			if (it->second == "on" || it->second == "off") lod = it->second == "on";
			else fprintf(stderr, "Wing War: invalid lod '%s'; expected on or off\n", it->second.c_str());
		}
	}

	void save_settings(FILE *f) const override
	{
		fprintf(f, "lod = %s\n", lod ? "on" : "off");
	}

	void start(host_context &context) override
	{
		ctx = &context;
		set_lod(context.machine, lod);
	}

	std::vector<menu_item> display_menu_items() override
	{
		return { { "LOD", "Off keeps the highest-detail models at every distance. On uses the arcade's simpler distant models. Off can lower performance.",
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
};

}
}

std::unique_ptr<game_host> make_game_host() { return std::make_unique<game::wingwar::host>(); }
