// Virtua Racing in the desktop host (src/host/game_host.h): its set and controls; the rest is the
// shared host's.
#include "../../host/game_host.h"
#include "../../host/race_replay.h"
#include "controls.h"
#include "gameplay.h"
#include "link.h"
#include "romset.h"
#include "../../machine/linked_pair.h"
#include <optional>
#include <charconv>
#include <cstdlib>
#include <new>

namespace game::vr {
namespace {

class local_link final : public local_cabinet_link
{
public:
	local_link(model1 &first, model1 *second, model1 *monitor)
		: pair(first, second, monitor), machines{&first, second, monitor} {}
	local_link(std::span<model1 *const> players, model1 *monitor)
		: pair(std::vector<model1 *>(players.begin(), players.end()), monitor)
	{
		std::copy(players.begin(), players.end(), machines.begin());
		machines[4] = monitor;
	}
	void run_frame() override { pair.run_frame(); }
	bool retire(model1 &machine) override { return pair.retire(machine, retire_local_snapshot); }
	local_link_status status(unsigned cabinet) const override
	{
		if (cabinet >= machines.size() || !machines[cabinet] || !machines[cabinet]->link()) return {};
		const auto &board = *machines[cabinet]->link();
		return {board.read(2), board.read(3), board.ticks()};
	}
private:
	linked_pair pair;
	std::array<model1 *, 5> machines{};
};

class host final : public game_host
{
public:
	const romset::definition &romset() const override { return ROMSET; }
	const control_profile &controls() const override { return CONTROLS; }
	const local_multiplayer_info *local_multiplayer() const override
	{
		static const local_multiplayer_info info{
			true, "Race type, course, language, car color, difficulty and service settings.",
			"Language, race type and course selection are shared; player settings are kept separately.",
			"Car color, difficulty and the arcade service menu.", "waiting for attract mode or course selection",
			"Change this player's steering and pedal axes.", true, 4
		};
		return &info;
	}
	std::unique_ptr<local_cabinet_link> make_local_link(model1 &first, model1 *second, model1 *monitor) override
	{
		return std::make_unique<local_link>(first, second, monitor);
	}
	std::unique_ptr<local_cabinet_link> make_local_link(std::span<model1 *const> players, model1 *monitor) override
	{
		return std::make_unique<local_link>(players, monitor);
	}
	local_multiplayer_status local_status(model1 &machine) const override
	{
		return {machine.work_ram()[0x1138], machine.read8(0x40dc8c), machine.read8(0x40d826)};
	}
	bool supports_lan() const override { return true; }
	lan_config local_lan_config() const override { return {{'M', '1', 'V', 'R'}, 3, 19794, 0, 0, 0, LAN_LINK, 0, 4}; }
	std::function<m1comm::memory(m1comm::memory)> lan_retirement_policy() const override { return retire_lan_snapshot; }
	std::vector<uint8_t> lan_settings() const override
	{
		const int language = language_value(ctx->machine);
		const int race = machine_setting_value(ctx->machine, machine_setting::race_type);
		const int course = machine_setting_value(ctx->machine, machine_setting::course_select);
		if (language < 0 || race < 0 || course < 0) return {};
		return {uint8_t(language), uint8_t(race), uint8_t(course)};
	}
	bool apply_lan_settings(std::span<const uint8_t> values) override
	{
		if (values.size() != 3 || values[0] > 1 || values[1] > 1 || values[2] > 3) {
			fprintf(stderr, "VR LAN: invalid shared race settings\n");
			return false;
		}
		set_language(ctx->machine, values[0]);
		set_machine_setting(ctx->machine, machine_setting::race_type, values[1]);
		set_machine_setting(ctx->machine, machine_setting::course_select, values[2]);
		return lan_settings() == std::vector<uint8_t>(values.begin(), values.end());
	}

	uint8_t analog_key_input(model1 &machine, int channel, bool low, bool high, uint8_t value,
		bool standard_steering = false) override
	{
		if (channel != 0) return game_host::analog_key_input(machine, channel, low, high, value);
		const bool entering_initials = machine.work_ram()[0x1138] == 0x1e && machine.read32(0x5018fc) != 0;
		const bool selecting_course = in_course_selection(machine);
		using screen = menu_steering::screen;
		const auto frame = machine.frame_number();
		const float seconds = steering_frame && *steering_frame == frame ? 0 : menu_steering::FRAME_SECONDS;
		steering_frame = frame;
		// The same integer km/h telemetry used by the qualifying driver.
		const float speed = float(machine.read32(0x400d14));
		return menu_wheel.update(entering_initials ? screen::initials : selecting_course ? screen::course : screen::none,
			low, high, value, standard_steering, speed, seconds);
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

	std::vector<menu_item> display_quality_menu_items() override
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
		bool pressed = false, rear = false;
		for (int port : { 1, 2 }) {
			pressed |= !(ctx->machine.m_inputs[port] & CAMERA_CYCLE_INPUT);
			rear |= !(ctx->machine.m_inputs[port] & CAMERA_REAR_INPUT);
			ctx->machine.m_inputs[port] |= CAMERA_CYCLE_INPUT | CAMERA_REAR_INPUT;
		}
		const uint8_t phase = ctx->machine.work_ram()[0x1138];
		ctx->machine.set_rear_view(rear && (phase == 0x12 || phase == 0x14) && ctx->link_player != 3);
		cameras.update(ctx->machine, pressed);
		capture_race_start();
	}

	void after_frame() override
	{
		finish_recorded_frame();
		// 0x13 is a brief transition between the grid countdown and racing.
		const uint8_t phase = ctx ? ctx->machine.work_ram()[0x1138] : 0;
		if (!ctx || ctx->link_player != 0 || phase < 0x12 || phase > 0x14) {
			race_snapshot.reset();
			race_gameplay.reset();
		}
	}

	void capture_race_start()
	{
		const uint8_t phase = ctx ? ctx->machine.work_ram()[0x1138] : 0;
		if (!ctx || ctx->link_player != 0 || phase < 0x12 || phase > 0x14) {
			race_snapshot.reset();
			race_gameplay.reset();
			return;
		}
		if (phase != 0x12 || race_snapshot) return;
		auto state = std::dynamic_pointer_cast<gameplay_state>(ctx->machine.game_state());
		model1::simulation_state snapshot;
		if (state && ctx->machine.save_simulation_state(snapshot)) {
			race_snapshot.emplace(std::move(snapshot));
			race_gameplay = *state;
		}
	}

	bool supports_quick_restart_race() const override { return true; }

	bool quick_restart_race() override
	{
		if (!ctx || ctx->link_player != 0 || !race_snapshot || !race_gameplay)
			return false;
		const uint8_t phase = ctx->machine.work_ram()[0x1138];
		if (phase < 0x12 || phase > 0x14) return false;
		auto state = std::dynamic_pointer_cast<gameplay_state>(ctx->machine.game_state());
		if (!state || !ctx->machine.load_simulation_state(*race_snapshot)) return false;
		clear_recording();
		const bool current_extended = state->extended_draw_distance;
		const bool current_lod = state->lod;
		const bool current_standalone_color = state->standalone_car_color;
		*state = *race_gameplay;
		state->extended_draw_distance = current_extended;
		state->lod = current_lod;
		state->standalone_car_color = current_standalone_color;
		menu_wheel = {};
		steering_frame.reset();
		cameras = {};
		ctx->machine.set_rear_view(false);
		if (ctx->restart_cpu_runtime) ctx->restart_cpu_runtime();
		ctx->machine.smooth_cut();
		return true;
	}

	bool can_join_local_cabinet() const override
	{
		return in_attract_mode(ctx->machine) || in_course_selection(ctx->machine);
	}

	bool booted() override
	{
		clear_recording();
		race_snapshot.reset();
		race_gameplay.reset();
		menu_wheel = {};
		steering_frame.reset();
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
	void clear_recording()
	{
		recorder.reset();
		recording_snapshot.reset();
		recording_gameplay.reset();
		recording_complete = false;
		recording_failed = false;
		recording_error.clear();
		recording_phase = 0xff;
	}

	void fail_recording(std::string error)
	{
		recorder.reset();
		recording_snapshot.reset();
		recording_gameplay.reset();
		recording_complete = false;
		recording_failed = true;
		recording_error = std::move(error);
		fprintf(stderr, "VR replay: %s\n", recording_error.c_str());
	}

	void begin_race_record_frame() override
	{
		if (!ctx || !ctx->race_replay_enabled) return;
		const uint8_t phase = ctx->machine.work_ram()[0x1138];
		const bool race_start = phase == 0x12 && recording_phase != 0x12;
		recording_phase = phase;
		if (race_start && (!recorder || recording_complete || recording_failed)) {
			clear_recording();
			recording_phase = phase;
			try {
				auto state = std::dynamic_pointer_cast<gameplay_state>(ctx->machine.game_state());
				model1::simulation_state snapshot;
				if (!state || !ctx->machine.save_simulation_state(snapshot, true)) {
					fail_recording("Could not capture the race replay checkpoint.");
					return;
				}
				recording_gameplay = *state;
				recording_snapshot.emplace(std::move(snapshot));
				recorder = std::make_unique<race_replay::race_recorder>(ctx->machine);
				recording_error = recorder->start();
			} catch (const std::bad_alloc &) {
				fail_recording("Could not allocate the race replay recording.");
				return;
			}
			if (!recording_error.empty()) {
				fail_recording(std::move(recording_error));
				return;
			}
		}
		if (!recorder || recording_complete || recording_failed) return;
		recording_error = recorder->begin_frame();
		if (!recording_error.empty()) {
			fail_recording(std::move(recording_error));
		}
	}

	void finish_recorded_frame()
	{
		if (!recorder || recording_failed) return;
		if (recording_complete) {
			if (ctx->machine.work_ram()[0x1138] == 0x12 && recording_phase != 0x12)
				clear_recording();
			return;
		}
		recording_error = recorder->end_frame();
		if (!recording_error.empty()) {
			fail_recording(std::move(recording_error));
			return;
		}
		const uint8_t phase = ctx->machine.work_ram()[0x1138];
		if (phase == 0x1c) {
			recorder->stop();
			recording_complete = true;
		} else if (phase == 0x03 || phase == 0x07 || phase == 0x10) {
			clear_recording();
		}
	}

	std::shared_ptr<const race_replay::race_recording> race_recording(bool completed) const override
	{
		if (!recorder || recording_failed || (completed && !recording_complete) ||
			(!completed && recording_complete) || recorder->data().frames.empty()) return {};
		return recorder->share();
	}

	bool restore_race_recording(model1 &machine) const override
	{
		if (!recording_snapshot || !recording_gameplay) return false;
		auto state = std::dynamic_pointer_cast<gameplay_state>(machine.game_state());
		if (recording_snapshot->link && !machine.link())
			machine.enable_link({}, recording_snapshot->link_external_clock);
		if (!state || !machine.load_simulation_state(*recording_snapshot, true)) return false;
		*state = *recording_gameplay;
		return true;
	}

	const std::string &race_recording_error() const override { return recording_error; }

	menu_steering menu_wheel;
	std::optional<uint64_t> steering_frame;
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
	std::optional<model1::simulation_state> race_snapshot;
	std::optional<gameplay_state> race_gameplay;
	std::unique_ptr<race_replay::race_recorder> recorder;
	std::optional<model1::simulation_state> recording_snapshot;
	std::optional<gameplay_state> recording_gameplay;
	bool recording_complete = false, recording_failed = false;
	uint8_t recording_phase = 0xff;
	std::string recording_error;
};

}
}

std::unique_ptr<game_host> make_game_host() { return std::make_unique<game::vr::host>(); }
