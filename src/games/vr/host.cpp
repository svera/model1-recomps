// Virtua Racing in the desktop host (src/host/game_host.h): its set and controls; the rest is the
// shared host's.
#include "../../host/game_host.h"
#include "controls.h"
#include "gameplay.h"
#include "romset.h"
#include <optional>
#include <charconv>
#include <cstdlib>

namespace game::vr {
namespace {

class host final : public game_host
{
public:
	const romset::definition &romset() const override { return ROMSET; }
	const control_profile &controls() const override { return CONTROLS; }

	uint8_t analog_key_input(model1 &machine, int channel, bool low, bool high, uint8_t value) override
	{
		if (channel != 0) return game_host::analog_key_input(machine, channel, low, high, value);
		const bool entering_initials = machine.work_ram()[0x1138] == 0x1e && machine.read32(0x5018fc) != 0;
		const bool selecting_course = machine.work_ram()[0x1138] == 0x10 && machine.camera() == 0x502150;
		return menu_wheel.update(entering_initials || selecting_course, low, high, value);
	}

	void load_settings(const settings_map &settings) override
	{
		if (const auto it = settings.find("lod"); it != settings.end()) {
			if (it->second == "on" || it->second == "off") lod = it->second == "on";
			else fprintf(stderr, "VR: invalid lod '%s'; expected on or off\n", it->second.c_str());
		}
		if (const auto coins = settings.find("arcade_coins"); coins != settings.end()) {
			int value;
			const auto &text = coins->second;
			const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
			if (result.ec == std::errc{} && result.ptr == text.data() + text.size() && value >= 0 && value <= FREE_PLAY)
				arcade_coin = value;
			else fprintf(stderr, "VR: invalid arcade_coins '%s'; expected 0 through %d\n", text.c_str(), FREE_PLAY);
		}
		const auto it = settings.find("draw_distance");
		if (it == settings.end()) return;
		if (it->second == "arcade" || it->second == "extended") extended = it->second == "extended";
		else fprintf(stderr, "VR: invalid draw_distance '%s'; expected arcade or extended\n", it->second.c_str());
	}

	void save_settings(FILE *f) const override
	{
		fprintf(f, "draw_distance = %s\n", extended ? "extended" : "arcade");
		fprintf(f, "lod = %s\n", lod ? "on" : "off");
		if (arcade_coin >= 0) fprintf(f, "arcade_coins = %d\n", arcade_coin);
	}

	void start(host_context &context) override
	{
		ctx = &context;
		if (!enable_standalone_car_color(context.machine)) {
			fprintf(stderr, "VR: standalone car-color support could not be initialized\n");
			std::abort();
		}
		set_extended_draw_distance(context.machine, extended);
		set_lod(context.machine, lod);
	}

	std::vector<menu_item> display_menu_items() override
	{
		return { { "Draw distance", "Full adds distant static scenery from the full 256-cell course in the renderer, without changing arcade car logic or timing. This can substantially lower performance. Applies to the next scenery list.",
			[this] { return extended ? "Full" : "Arcade"; },
			[this](int d) {
				extended = (int(extended) + d + 2) % 2;
				set_extended_draw_distance(ctx->machine, extended);
				ctx->settings_changed();
			} },
			{ "LOD", "Off keeps the highest-detail models at every distance. On uses the arcade's simpler distant models. Scenery keeps its full detail in either mode; Draw distance controls how much scenery is visible. Off can lower performance.",
			[this] { return lod ? "On" : "Off"; },
			[this](int d) {
				lod = (int(lod) + d + 2) % 2;
				set_lod(ctx->machine, lod);
				ctx->settings_changed();
			} } };
	}

	std::vector<menu_item> game_menu_items() override
	{
		if (!language_before) {
			language_before = language_value(ctx->machine);
			language_choice = *language_before;
		}
		std::vector<menu_item> items{ { "Language", "The game's own text: Japanese or English. Changing it restarts the game when the menu closes.",
			[this] {
				return std::string(language_choice < 0 ? "-" : language_choice ? "English" : "Japanese");
			},
			[this](int d) {
				if (language_choice < 0) {
					fprintf(stderr, "VR: cannot change language before the game initializes its EEPROM settings\n");
					return;
				}
				language_choice = ((language_choice + d) % 2 + 2) % 2;
			} },
			{ "Service menu",
				"The arcade's own test menu: machine settings, input and sound tests, coin settings and bookkeeping. Use 9 to move and F2 to select. Choose EXIT to return.",
				{}, {}, [this] { ctx->service_menu(); } } };
		if (!machine_before) {
			std::array<int, 4> values;
			for (size_t i = 0; i < values.size(); i++)
				values[i] = machine_setting_value(ctx->machine, machine_setting(i));
			machine_before = values;
			machine_choices = values;
		}
		// (no Race mode: only linked cabinets use it, so a standalone race is the same either way)
		static const std::array<const char *, 4> names{ "Car color", nullptr, "Course select", "Difficulty" };
		static const std::array<const char *, 4> help{
			"Your car's color in a race, including standalone play. Demo cars keep their arcade colors. Changing it restarts the game when the menu closes.",
			nullptr,
			"Majority vote: you choose the course before each race (linked cabinets take the most votes). A course here is raced every time, "
			"skipping the course choice for the gears only. Changing it restarts the game when the menu closes.",
			"How fast the arcade's automatic difficulty rises: the more the game is played, the less starting time races get, from 65 seconds down to "
			"55 (Easy) or 50, sooner on harder settings. Changing it restarts the game when the menu closes."
		};
		static const std::array<std::vector<const char *>, 4> choices{ {
			{ "Red", "Blue", "Yellow", "Green", "Black", "Pink", "Sky blue", "Orange" },
			{},
			{ "Majority vote", "Beginner", "Medium", "Expert" },
			{ "Easy", "Normal", "Hard", "Hardest" }
		} };
		for (size_t i = 0; i < names.size(); i++)
			if (names[i]) items.insert(items.end() - 1, { names[i], help[i],
				[this, i] {
					const int value = machine_choices[i];
					return std::string(value < 0 ? "-" : choices[i][value]);
				},
				[this, i](int d) {
					auto &value = machine_choices[i];
					if (value < 0) {
						fprintf(stderr, "VR: cannot change %s before valid EEPROM settings are available\n", names[i]);
						return;
					}
					const int count = int(choices[i].size());
					value = ((value + d) % count + count) % count;
				} });
		return items;
	}

	void keep_arcade_settings() override
	{
		apply_settings();
		const int now = coin_setting(ctx->machine);
		if (!ctx->pc_mode && now >= 0 && now != arcade_coin) {
			arcade_coin = now;
			ctx->settings_changed();
		}
	}

	bool has_play_style() const override { return true; }

	const char *picture_shape_help() const override
	{
		return "Wide shapes widen the view without stretching the HUD; off-center HUD elements follow the screen edges. ";
	}

	void before_frame() override
	{
		bool pressed = false;
		for (int port : { 1, 2 }) {
			pressed |= !(ctx->machine.m_inputs[port] & CAMERA_CYCLE_INPUT);
			ctx->machine.m_inputs[port] |= CAMERA_CYCLE_INPUT;
		}
		cameras.update(ctx->machine, pressed);
	}

	bool can_join_local_cabinet() const override
	{
		return in_attract_mode(ctx->machine);
	}

	bool booted() override
	{
		language_before.reset();
		machine_before.reset();
		const bool monitor_changed = set_monitor_type(ctx->machine, 0);
		const bool link_changed = ctx->standalone_cabinet ? set_standalone(ctx->machine) :
			ctx->link_player && set_link_player(ctx->machine, ctx->link_player);
		const int now = coin_setting(ctx->machine);
		if (now < 0) return monitor_changed || link_changed;
		if (ctx->pc_mode) {
			if (now != FREE_PLAY && now != arcade_coin) {
				arcade_coin = now;
				ctx->settings_changed();
			}
			return set_coin_setting(ctx->machine, FREE_PLAY) || monitor_changed || link_changed;
		}
		return set_coin_setting(ctx->machine, arcade_coin >= 0 ? arcade_coin : now == FREE_PLAY ? 0 : now) || monitor_changed || link_changed;
	}

	restart menu_closed() override
	{
		return apply_settings() ? restart::game : restart::none;
	}

private:
	menu_steering menu_wheel;
	bool apply_settings()
	{
		bool changed = apply_language();
		if (machine_before) {
			for (size_t i = 0; i < machine_choices.size(); i++) {
				if (machine_choices[i] == (*machine_before)[i]) continue;
				if (set_machine_setting(ctx->machine, machine_setting(i), machine_choices[i])) {
					changed = true;
					ctx->settings_changed();
				} else fprintf(stderr, "VR: could not apply machine setting %zu to EEPROM\n", i);
			}
			machine_before.reset();
		}
		return changed;
	}

	bool apply_language()
	{
		const bool changed = language_before && *language_before != language_choice;
		language_before.reset();
		if (!changed) return false;
		if (!set_language(ctx->machine, language_choice)) {
			fprintf(stderr, "VR: could not apply language to the game's EEPROM settings\n");
			return false;
		}
		ctx->settings_changed();
		return true;
	}

	host_context *ctx = nullptr;
	bool extended = true;
	bool lod = false;
	camera_cycle cameras;
	int arcade_coin = -1;
	std::optional<int> language_before;
	int language_choice = -1;
	std::optional<std::array<int, 4>> machine_before;
	std::array<int, 4> machine_choices{};
};

}
}

std::unique_ptr<game_host> make_game_host() { return std::make_unique<game::vr::host>(); }
