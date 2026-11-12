#pragma once

#include "../game.h"
#include <unordered_set>

namespace game::wingwar {

// The 0x1f result feeds Wing War's model-tier table; the 0x1a results feed visibility checks.
inline constexpr uint32_t LOD_RESULT_READ = 0x02fc8e;
inline constexpr uint32_t INVINCIBILITY = 1;
inline constexpr uint32_t INFINITE_MISSILES = 2;
inline constexpr uint32_t HIT_EVENT_READ = 0x01f142;
inline constexpr uint32_t PROJECTILE_PITCH_READ = 0x01fb9c;
inline constexpr uint32_t PROJECTILE_ROLL_READ = 0x01fba1;
inline constexpr uint32_t MISSILE_STOCK_READ = 0x01fd6f;

struct gameplay_state : game::gameplay_state
{
	bool lod = false;
	bool extended_draw_distance = true;
	struct scenery_list {
		bool started = false, extended = false;
		std::optional<uint32_t> matrix;
		uint32_t anchor = 0;
		std::unordered_set<uint32_t> submitted;
	};
	std::array<scenery_list, 2> scenery;
	struct camera_sample {
		std::optional<std::array<float, 12>> matrix;
		uint64_t frame = 0;
	};
	std::array<camera_sample, 2> cameras;
	struct credits_points {
		std::vector<std::array<float, 6>> points; // world position, then where the camera puts it
		uint64_t frame = 0;
	};
	std::array<credits_points, 2> credits;
};

// Change view: a host-only bit in the player byte, consumed before the frame and recorded inputs.
inline constexpr uint8_t VIEW_CYCLE_INPUT = 0x80;
inline constexpr uint32_t CURRENT_VIEW = 0x40f380;   // 0-3: the cabinet's View 1-4

// Presses the cabinet's next view button for a few frames on each Change view press.
class view_cycle
{
public:
	void update(model1 &machine, bool pressed);
private:
	bool m_pressed = false;
	int m_frames = 0, m_view = 0;
};

gameplay_callbacks create_gameplay(model1 &machine);
void set_lod(model1 &machine, bool enabled);
void set_extended_draw_distance(model1 &machine, bool enabled);
uint32_t lod_result(bool enabled, uint32_t pc, uint32_t value);

}
