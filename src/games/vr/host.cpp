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

	uint8_t analog_key_input(model1 &machine, int channel, bool low, bool high, uint8_t value,
		bool exponential_steering = false) override
	{
		if (channel != 0) return game_host::analog_key_input(machine, channel, low, high, value);
		const bool entering_initials = machine.work_ram()[0x1138] == 0x1e && machine.read32(0x5018fc) != 0;
		const bool selecting_course = in_course_selection(machine);
		using screen = menu_steering::screen;
		return menu_wheel.update(entering_initials ? screen::initials : selecting_course ? screen::course : screen::none,
			low, high, value, exponential_steering);
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
		if (it != settings.end()) {
			if (it->second == "arcade" || it->second == "extended") extended = it->second == "extended";
			else fprintf(stderr, "VR: invalid draw_distance '%s'; expected arcade or extended\n", it->second.c_str());
		}
		if (ctx) {
			set_extended_draw_distance(ctx->machine, extended);
			set_lod(ctx->machine, lod);
		}
	}

	settings_map shared_display_settings() const override
	{
		return {{"draw_distance", extended ? "extended" : "arcade"}, {"lod", lod ? "on" : "off"}};
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
		default_blue_pending = context.link_player == 2 &&
			machine_setting_value(context.machine, machine_setting::car_color) < 0;
		if (!enable_standalone_car_color(context.machine)) {
			fprintf(stderr, "VR: standalone car-color support could not be initialized\n");
			std::abort();
		}
		set_extended_draw_distance(context.machine, extended);
		set_lod(context.machine, lod);
	}

	std::vector<menu_item> display_menu_items() override
	{
		return { { "Draw distance", "Full shows distant scenery and cars in player and Live Monitor views. Arcade keeps the original visibility limits. Full can substantially lower performance. Applies to the next display list.",
			[this] { return extended ? "Full" : "Arcade"; },
			[this](int d) {
				extended = (int(extended) + d + 2) % 2;
				set_extended_draw_distance(ctx->machine, extended);
				ctx->settings_changed();
			} },
			{ "LOD", "Off keeps the highest-detail models within the arcade's visibility range. On uses the arcade's simpler distant models. Full draw distance adds cars beyond that range using their distant models. Scenery keeps its full detail in either mode. Off can lower performance.",
			[this] { return lod ? "On" : "Off"; },
			[this](int d) {
				lod = (int(lod) + d + 2) % 2;
				set_lod(ctx->machine, lod);
				ctx->settings_changed();
			} } };
	}

	std::vector<menu_item> game_menu_items() override
	{
		return build_game_menu_items(true, true);
	}

	std::vector<menu_item> cabinet_game_menu_items() override
	{
		return build_game_menu_items(false, true);
	}

	std::vector<menu_item> shared_game_menu_items() override
	{
		return build_game_menu_items(true, false);
	}

	bool sync_shared_settings(model1 &primary) override
	{
		bool changed = false;
		const int language = language_value(primary);
		if (language < 0)
			fprintf(stderr, "VR: cannot synchronize language before valid EEPROM settings are available\n");
		else changed = set_language(ctx->machine, language);
		for (const auto setting : {machine_setting::course_select, machine_setting::race_type}) {
			const int value = machine_setting_value(primary, setting);
			if (value < 0) {
				fprintf(stderr, "VR: cannot synchronize shared setting %zu before valid EEPROM settings are available\n",
					size_t(setting));
				continue;
			}
			changed = set_machine_setting(ctx->machine, setting, value) || changed;
		}
		if (changed) ctx->settings_changed();
		return changed;
	}

private:
	std::vector<menu_item> build_game_menu_items(bool shared, bool cabinet)
	{
		if (shared && !language_before) {
			language_before = language_value(ctx->machine);
			language_choice = *language_before;
		}
		std::vector<menu_item> items;
		if (shared) items.push_back({ "Language", "The game's own text for all players and the Live Monitor: Japanese or English. Changing it restarts the game when the menu closes.",
			[this] {
				return std::string(language_choice < 0 ? "-" : language_choice ? "English" : "Japanese");
			},
			[this](int d) {
				if (language_choice < 0) {
					fprintf(stderr, "VR: cannot change language before the game initializes its EEPROM settings\n");
					return;
				}
				language_choice = ((language_choice + d) % 2 + 2) % 2;
			} });
		if (cabinet) items.push_back({ "Service menu",
				"The arcade's own test menu: machine settings, input and sound tests, coin settings and bookkeeping. Use 9 to move and F2 to select. Choose EXIT to return.",
				{}, {}, [this] { ctx->service_menu(); } });
		if (!machine_before) {
			std::array<int, 4> values;
			for (size_t i = 0; i < values.size(); i++)
				values[i] = machine_setting_value(ctx->machine, machine_setting(i));
			machine_before = values;
			machine_choices = values;
		}
		static const std::array<const char *, 4> names{ "Car color", "Race type", "Course select", "Difficulty" };
		static const std::array<const char *, 4> help{
			"Your car's color in a race, including standalone play. Demo cars keep their arcade colors. Changing it restarts the game when the menu closes.",
			"The arcade's Normal or Grand Prix race mode for both players. Applies to linked races; solo races are unchanged. "
			"Changing it restarts the game when the menu closes.",
			"Majority vote: you choose the course before each race (linked cabinets take the most votes). A course here is raced every time, "
			"skipping the course choice for the gears only. Changing it restarts the game when the menu closes.",
			"How fast the arcade's automatic difficulty rises: the more the game is played, the less starting time races get, from 65 seconds down to "
			"55 (Easy) or 50, sooner on harder settings. Changing it restarts the game when the menu closes."
		};
		static const std::array<std::vector<const char *>, 4> choices{ {
			{ "Red", "Blue", "Yellow", "Green", "Black", "Pink", "Sky blue", "Orange" },
			{ "Normal", "Grand Prix" },
			{ "Majority vote", "Beginner", "Medium", "Expert" },
			{ "Easy", "Normal", "Hard", "Hardest" }
		} };
		for (size_t i = 0; i < names.size(); i++)
			if (((i == size_t(machine_setting::course_select) || i == size_t(machine_setting::race_type)) ? shared : cabinet) &&
				(i != size_t(machine_setting::race_type) || ctx->link_player != 0))
				items.insert(cabinet ? items.end() - 1 : items.end(), { names[i], help[i],
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

public:
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
		return in_attract_mode(ctx->machine) || in_course_selection(ctx->machine);
	}

	bool booted() override
	{
		language_before.reset();
		machine_before.reset();
		const bool monitor_changed = set_monitor_type(ctx->machine, 0);
		const bool link_changed = ctx->standalone_cabinet ? set_standalone(ctx->machine) :
			ctx->link_player == 3 ? set_live_monitor(ctx->machine) :
			ctx->link_player && set_link_player(ctx->machine, ctx->link_player);
		bool color_changed = false;
		if (default_blue_pending && machine_setting_value(ctx->machine, machine_setting::car_color) >= 0) {
			color_changed = set_machine_setting(ctx->machine, machine_setting::car_color, 1);
			default_blue_pending = false;
			if (color_changed) ctx->settings_changed();
		}
		const bool machine_changed = monitor_changed || link_changed || color_changed;
		const int now = coin_setting(ctx->machine);
		if (now < 0) return machine_changed;
		if (ctx->pc_mode) {
			if (now != FREE_PLAY && now != arcade_coin) {
				arcade_coin = now;
				ctx->settings_changed();
			}
			return set_coin_setting(ctx->machine, FREE_PLAY) || machine_changed;
		}
		return set_coin_setting(ctx->machine, arcade_coin >= 0 ? arcade_coin : now == FREE_PLAY ? 0 : now) || machine_changed;
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
	bool default_blue_pending = false;
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
