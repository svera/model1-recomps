// Star Wars Arcade's Gunner assist: the Gunner's stick and laser worked out from the game's RAM each
// frame, as a second player would, for one player to fly with both seats firing. It only sets the
// cabinet's inputs (the Gunner stick's X/Y and laser), so a recording of the play replays without it.
//
// From the game's RAM, found against the camera-space positions its drawing computes (an
// interpreter trace of a phase-3 flight): 0x40ed00 ends the list of shootable objects at 0x40ed04
// (16-bit pointers into 0x400000); an object's world position is three floats at +0x0c. The camera
// for the coming frame is at 0x40f700 (three floats), its rotation at 0x40f570 (nine floats, a
// column at a time). The view's centre (248, 191) and zoom (200) and the reticle's measured
// travel (1.41 and 0.93 pixels per stick step, inverted) give the stick for a point on screen.
// Static objects (the surface's guns and towers) are left to the Pilot: the assist shoots what moves.
#pragma once
#include "../../machine/model1.h"
#include "profile.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <unordered_map>

namespace game::swa {

class gunner_assist
{
public:
	static constexpr int STICK_X = 4, STICK_Y = 5;

	// Sets the Gunner's stick and laser for the frame about to run; false: nothing to aim at (the
	// Gunner's controls left as the player has them).
	bool aim(model1 &m)
	{
		auto word = [&](uint32_t a) { return m.peek16(a); };
		auto real = [&](uint32_t a) { return std::bit_cast<float>(uint32_t(word(a)) | uint32_t(word(a + 2)) << 16); };
		const uint32_t end = 0x400000 | word(0x40ed00);
		const bool flying = m.nvram()[0xf008] == 13 && (m.nvram()[0xf009] == 3 || m.nvram()[0xf009] == 5);
		m_frame++;
		if (!flying || end < 0x40ed04 || end > 0x40ed80 || (end & 1)) {
			m_seen.clear();
			return false;
		}
		const float eye[3] = { real(0x40f700), real(0x40f704), real(0x40f708) };
		float rot[9];
		for (int k = 0; k < 9; k++) rot[k] = real(0x40f570 + 4 * k);
		float best = 1e30f, bx = 0, by = 0;
		for (uint32_t p = 0x40ed04; p < end; p += 2) {
			const uint32_t o = 0x400000 | word(p);
			const float w[3] = { real(o + 12), real(o + 16), real(o + 20) };
			seen &s = m_seen[o];
			const bool moved = s.frame && (w[0] != s.x || w[1] != s.y || w[2] != s.z);
			if (moved) s.moving_until = m_frame + 60;
			s.x = w[0]; s.y = w[1]; s.z = w[2]; s.frame = m_frame;
			if (s.moving_until < m_frame) continue;
			const float d[3] = { w[0] - eye[0], w[1] - eye[1], w[2] - eye[2] };
			float c[3];
			for (int i = 0; i < 3; i++) c[i] = rot[i] * d[0] + rot[3 + i] * d[1] + rot[6 + i] * d[2];
			if (!std::isfinite(c[2]) || c[2] < 500 || c[2] > 20000) continue;
			const float x = c[0] / c[2], y = c[1] / c[2];
			if (std::fabs(x) > 0.7f || std::fabs(y) > 0.4f) continue;
			const float score = c[2] + (x * x + y * y) * 10000;
			if (score < best) { best = score; bx = x; by = y; }
		}
		if (m_frame % 600 == 0)
			for (auto it = m_seen.begin(); it != m_seen.end();)
				it = it->second.frame + 600 < m_frame ? m_seen.erase(it) : std::next(it);
		if (best == 1e30f) return false;
		m.set_analog_input(STICK_X, uint8_t(std::clamp(int(std::lround(127 - bx * (200 / 1.41f))), 27, 227)));
		m.set_analog_input(STICK_Y, uint8_t(std::clamp(int(std::lround(127 + by * (200 / 0.93f))), 27, 227)));
		m.m_inputs[1] &= uint8_t(~GUNNER_LASER);   // (active low)
		return true;
	}

private:
	struct seen { float x = 0, y = 0, z = 0; uint64_t frame = 0, moving_until = 0; };
	std::unordered_map<uint32_t, seen> m_seen;
	uint64_t m_frame = 0;
};

}
