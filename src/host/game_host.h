// What the desktop host (main.cpp) asks of the game it runs. The host itself is the Model 1's:
// the window, sound, pads, menu, saves and settings.ini; each game adds its own through this
// (src/games/<game>/host.cpp), and a host binary links one game's.
#pragma once
#include "controls.h"
#include "local_cabinet_link.h"
#include "menu.h"
#include "../machine/model1.h"
#include "../machine/model1_romset.h"
#include "../../third_party/arcade-lan/include/arcade_lan.h"
#include <cstdio>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

using settings_map = std::map<std::string, std::string>;

struct local_multiplayer_info
{
	bool live_monitor = false;
	const char *game_help = "Game and service settings.";
	const char *shared_game_help = "Player settings are kept separately.";
	const char *cabinet_game_help = "Player and service settings.";
	const char *join_wait = "waiting for a safe point";
	const char *analog_help = "Change this player's analog axes.";
};

struct local_multiplayer_status
{
	unsigned phase = 0, role = 0, participants = 0;
};

// The host's side, for the game's
struct host_context
{
	model1 &machine;
	std::string data_dir;                    // the player's (saves, settings.ini, backgrounds/)
	std::string assets_dir;
	const bool &pc_mode;                     // PC play style (else Arcade: the cabinet's boot screen, coins)
	std::function<void()> settings_changed;  // settings.ini is to be saved
	std::function<void(menu_page)> push_page;
	std::function<menu_item()> back_item;
	std::function<void()> service_menu;      // closes our menu; the game's own test menu opens
	std::function<void(menu_page, int)> replace_page;
	std::function<void(const std::string &, std::function<bool(const SDL_Event &)>, std::function<void()>)> capture;
	std::function<void()> refresh_menu;
	bool replay = false;                     // --replay-inputs (and its recording): reproduction settings, until a reset ends them
	int link_player = 0;                     // 0 standalone, 1 first cabinet, 2 other linked cabinet, 3 receive-only monitor
	bool standalone_cabinet = false;         // force standalone instead of restoring a saved link role
};

class game_host
{
public:
	virtual ~game_host() = default;

	virtual const romset::definition &romset() const = 0;   // the MAME set the player's zip is
	virtual const control_profile &controls() const = 0;

	// Digital bindings for an analog channel, after reading the pad's analog axis.
	virtual uint8_t analog_key_input(model1 &, int channel, bool low, bool high, uint8_t value,
		bool standard_steering = false)
	{
		return low != high ? (low ? 0 : 255) : value;
	}

	// settings.ini's own keys (read before start, or shared display choices applied during play).
	virtual void load_settings(const settings_map &) {}
	virtual void save_settings(FILE *) const {}
	// Only renderer settings shared by local cabinets; excludes each player's game preferences.
	virtual settings_map shared_display_settings() const { return {}; }

	// The EEPROM a cold boot without saves starts from (a new player's, and new input recordings');
	// empty: blank, the arcade's factory defaults.
	virtual std::vector<uint8_t> new_eeprom(model1 &) { return {}; }
	// The machine is made, its code not yet recompiled (an instruction the game changes goes in it).
	virtual void start(host_context &) {}
	// Once, before the first boot: the game's pictures, the settings it reads at boot.
	virtual void before_boot() {}
	// Every restart, before boot: apply settings that patch recompiled game code.
	virtual bool before_restart() { return false; }
	// After each boot (a reset); true: boot again (a setting the game reads at boot was put back).
	virtual bool booted() { return false; }
	// Games without camera RAM can run their PC-style boot unseen, with drawing already disabled.
	virtual void skip_boot() {}

	// The chosen picture shape (width / height; 0: fit the window), before the first boot and on every
	// change. True: the running game must restart for it (a setting it reads at boot).
	virtual bool set_picture_shape(float) { return false; }
	// True: the game draws the shape itself, squeezed into its 4:3 picture for a widescreen monitor,
	// so the host stretches that picture instead of widening it.
	virtual bool picture_stretched() const { return false; }
	// Added to the Picture shape help: what the game does with a shape (ending with a space).
	virtual const char *picture_shape_help() const { return ""; }

	// The Game page's play style (PC or Arcade): only for a game that has it.
	virtual bool has_play_style() const { return false; }
	// The game's own settings for Arcade mode, kept as they are now (before a restart and at quit).
	virtual void keep_arcade_settings() {}
	virtual void play_style_changed() {}

	// Game-specific choices on the shared Display > Picture and Display > Quality pages.
	virtual std::vector<menu_item> display_picture_menu_items() { return {}; }
	virtual std::vector<menu_item> display_quality_menu_items() { return {}; }
	// Game-specific choices in Game, and main-menu pages after Game and Controls.
	virtual std::vector<menu_item> sound_menu_items() { return {}; }
	virtual std::vector<menu_item> game_menu_items() { return {}; }
	virtual std::vector<menu_item> cabinet_game_menu_items() { return game_menu_items(); }
	virtual std::vector<menu_item> shared_game_menu_items() { return {}; }
	// Copy session-wide settings from the primary cabinet; true if a restart is needed.
	virtual bool sync_shared_settings(model1 &) { return false; }
	virtual std::vector<menu_item> menu_items() { return {}; }
	virtual std::vector<menu_item> menu_items_after_controls() { return {}; }
	// The menu closed: whether the game restarts, and with its code changed (recompiled first).
	enum class restart { none, game, game_and_code };
	virtual restart menu_closed() { return restart::none; }

	// Before each game frame
	virtual void before_frame() {}
	virtual void after_frame() {}
	virtual bool hide_frame() const { return false; }
	// Whether a queued local multiplayer join can safely restart into linked play.
	virtual bool can_join_local_cabinet() const { return false; }
	// Opt in only after implementing linked roles, boot and join handling for this game.
	virtual const local_multiplayer_info *local_multiplayer() const { return nullptr; }
	// Machines outlive the session; second and monitor are independently optional, but not both absent.
	virtual std::unique_ptr<local_cabinet_link> make_local_link(model1 &, model1 *, model1 *) { return {}; }
	virtual local_multiplayer_status local_status(model1 &) const { return {}; }
	virtual bool local_boot_complete(const model1 &machine) const { return machine.past_boot(); }
	virtual bool supports_lan() const { return false; }
	virtual lan_config local_lan_config() const { return {}; }
	// No policy means a LAN peer loss ends the session rather than fabricating game data.
	virtual std::function<m1comm::memory(m1comm::memory)> lan_retirement_policy() const { return {}; }
	virtual std::vector<uint8_t> lan_settings() const { return {}; }
	virtual bool apply_lan_settings(std::span<const uint8_t>) { return false; }

	// Optional text accompanying the battery-backed save (games without one return an empty name).
	virtual const char *save_sidecar_name() const { return ""; }
	virtual std::string save_sidecar() const { return {}; }
	virtual void load_sidecar(const std::string &) {}
	virtual uint32_t save_sidecar_version() const { return 0; }

	// M1_TRACE's six columns of the game's own state (VF: the fighters' health, rounds, the fight's
	// state); zeros when the game has none
	virtual void trace_fields(unsigned (&fields)[6]) const {}

};

// The game this host binary runs (each game's host.cpp has it)
std::unique_ptr<game_host> make_game_host();
