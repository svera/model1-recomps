// What the desktop host (main.cpp) asks of the game it runs. The host itself is the Model 1's:
// the window, sound, pads, menu, saves and settings.ini; each game adds its own through this
// (src/games/<game>/host.cpp), and a host binary links one game's.
#pragma once
#include "controls.h"
#include "menu.h"
#include "../machine/model1.h"
#include "../machine/model1_romset.h"
#include <cstdio>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

using settings_map = std::map<std::string, std::string>;

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
	bool replay = false;                     // --replay-inputs (and its recording): reproduction settings
	int link_player = 0;                     // experimental VR cabinet: 1 master, 2 slave
	bool standalone_cabinet = false;         // force standalone instead of restoring a saved link role
};

class game_host
{
public:
	virtual ~game_host() = default;

	virtual const romset::definition &romset() const = 0;   // the MAME set the player's zip is
	virtual const control_profile &controls() const = 0;

	// Digital bindings for an analog channel, after reading the pad's analog axis.
	virtual uint8_t analog_key_input(model1 &, int channel, bool low, bool high, uint8_t value)
	{
		return low != high ? (low ? 0 : 255) : value;
	}

	// settings.ini's own keys (read before start)
	virtual void load_settings(const settings_map &) {}
	virtual void save_settings(FILE *) const {}

	// The machine is made, its code not yet recompiled (an instruction the game changes goes in it).
	virtual void start(host_context &) {}
	// Once, before the first boot: the game's pictures, the settings it reads at boot.
	virtual void before_boot() {}
	// Every restart, before boot: apply settings that patch recompiled game code.
	virtual bool before_restart() { return false; }
	// After each boot (a reset); true: boot again (a setting the game reads at boot was put back).
	virtual bool booted() { return false; }

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

	// Game-specific choices in Display and Game, and main-menu pages after Game and Controls.
	virtual std::vector<menu_item> display_menu_items() { return {}; }
	virtual std::vector<menu_item> game_menu_items() { return {}; }
	virtual std::vector<menu_item> menu_items() { return {}; }
	virtual std::vector<menu_item> menu_items_after_controls() { return {}; }
	// The menu closed: whether the game restarts, and with its code changed (recompiled first).
	enum class restart { none, game, game_and_code };
	virtual restart menu_closed() { return restart::none; }

	// Before each game frame
	virtual void before_frame() {}
	virtual void after_frame() {}
	virtual bool hide_frame() const { return false; }
	// A queued local multiplayer join may reboot only from an idle attract screen.
	virtual bool can_join_local_cabinet() const { return false; }

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
