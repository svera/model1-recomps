// Star Wars Arcade's scripted play: attract, two coins (three for PILOT & GUNNER), the mode and
// mission menus confirmed with Start (Rebel Attack, or Training), then seeded flying and firing until the shields or the time
// run out, the rankings and back to attract; optionally continue once after failure, or
// use visual Gunner aiming to complete the opening objective.
//
// The battery RAM the checks read was found by watching runs against the screen, not from the
// program's code: 0x40f008 the sequence (1-2 attract, 11-13 credited and playing, 14 game over and
// rankings, then attract again), 0x40f334 the time left in 1/30 s (TIME on screen), 0x40f410 and
// 0x40f510 the Pilot's and Gunner's scores (BCD), 0x40f100 the PILOT rankings (ten 8-byte entries:
// BCD score, a name) and 0x40f150 the PILOT & GUNNER ones (ten 12-byte entries: the team's total,
// both names). 0x40f340/0x40f342 are the signed Gunner Y/X inputs, observed against the reticle.
// 0x40f009 is the subsequence (12 offers continue with sequence 13 and an unset clock);
// 0x40f0e0 is the credit counter.
#include "../script.h"
#include "../../host/input_replay.h"
#include "../../machine/model1.h"
#include "profile.h"
#include <algorithm>
#include <cstdio>

namespace game::swa {

// the recorded human runs the "recording" and "complete" scenarios replay, from the repository's folder
constexpr const char *RECORDING = "tests/games/swa/recordings/manual-trench-to-continue.inputs",
	*COMPLETE_RECORDING = "tests/games/swa/recordings/manual-complete-game.inputs";

namespace {

constexpr uint32_t SEQUENCE = 0xf008, TIME = 0xf334, PILOT_SCORE = 0xf410, GUNNER_SCORE = 0xf510,
	PILOT_RANKINGS = 0xf100, TEAM_RANKINGS = 0xf150, GUNNER_Y = 0xf340, GUNNER_X = 0xf342,
	SUBSEQUENCE = 0xf009, CREDITS = 0xf0e0;
constexpr int RANKING_ENTRIES = 10, PILOT_RANKING_SIZE = 8, TEAM_RANKING_SIZE = 12;
constexpr uint8_t SEQ_ATTRACT = 1, SEQ_PLAYING = 13, SEQ_GAME_OVER = 14;

constexpr int STICK_X = 0, STICK_Y = 1, THROTTLE = 2, GUNNER_STICK_X = 4, GUNNER_STICK_Y = 5;
constexpr int STICK_LOW = 27, STICK_CENTRE = 127, STICK_HIGH = 227;   // IO_PROFILE's travel

// Inputs, by frame from power-on: the coins land in the attract's Star Destroyer fly-by; Start
// opens the mode menu, the stick picks PILOT (centred) or PILOT & GUNNER (back), Start confirms it
// and then the mission (Rebel Attack, the first; stick back selects Training).
constexpr int COIN_FRAMES[] = { 1300, 1340, 1380 };
constexpr int START_FRAMES[] = { 1400, 1490, 1560 };
constexpr int SELECT_FROM = 1420, SELECT_TO = 1470, FLY_FROM = 1600, PRESS = 6;
constexpr int MISSION_SELECT_FROM = 1510, MISSION_SELECT_TO = 1540;
constexpr int GUNNER_AIM_FROM = 3700, GUNNER_AIM_HOLD = 120;

class play final : public auto_play
{
public:
	play(bool gunner, bool training, bool continuing, bool progression, uint32_t seed, bool checks)
		: m_gunner(gunner), m_training(training), m_continuing(continuing), m_progression(progression),
			m_rng(seed * 2654435761u + 1), m_checks(checks) {}

	script_inputs frame(int f) override
	{
		script_inputs s;
		if (m_gunner) {
			s.analog[GUNNER_STICK_X] = s.analog[GUNNER_STICK_Y] = STICK_CENTRE;
			if (f >= GUNNER_AIM_FROM) {
				const int phase = ((f - GUNNER_AIM_FROM) / GUNNER_AIM_HOLD) % 4;
				s.analog[phase < 2 ? GUNNER_STICK_X : GUNNER_STICK_Y] = phase % 2 ? STICK_HIGH : STICK_LOW;
			}
		}
		for (int i = 0; i < (m_gunner ? 3 : 2); i++)
			if (f >= COIN_FRAMES[i] && f < COIN_FRAMES[i] + PRESS) s.pressed[0] |= COIN;
		for (int start : START_FRAMES)
			if (f >= start && f < start + PRESS) s.pressed[0] |= START;
		if (f < FLY_FROM) {
			s.analog[STICK_X] = STICK_CENTRE;
			s.analog[STICK_Y] = m_gunner && f >= SELECT_FROM && f < SELECT_TO ? STICK_HIGH : STICK_CENTRE;
			if (m_training && f >= MISSION_SELECT_FROM && f < MISSION_SELECT_TO) s.analog[STICK_Y] = STICK_HIGH;
			return s;
		}
		if (m_progression) {
			s.analog[STICK_X] = s.analog[STICK_Y] = STICK_CENTRE;
			s.analog[THROTTLE] = 28;
			s.analog[GUNNER_STICK_X] = m_visual_x;
			s.analog[GUNNER_STICK_Y] = m_visual_y;
			s.pressed[1] = PILOT_LASER | GUNNER_LASER;
			if (f % 90 < PRESS) s.pressed[1] |= PILOT_TORPEDO | GUNNER_TORPEDO;
			return s;
		}
		// flying: the stick eases towards a new random point every second or two, the throttle
		// changes now and then, the lasers fire in bursts and a torpedo every five seconds
		if (f >= m_next_turn) {
			m_target_x = STICK_LOW + rnd(STICK_HIGH - STICK_LOW + 1);
			m_target_y = STICK_LOW + rnd(STICK_HIGH - STICK_LOW + 1);
			if (rnd(3) == 0) m_throttle = 28 + rnd(101);   // FAST (28) to half
			m_next_turn = f + 60 + rnd(60);
		}
		m_x += (m_target_x - m_x) / 8;
		m_y += (m_target_y - m_y) / 8;
		s.analog[STICK_X] = m_x;
		s.analog[STICK_Y] = m_y;
		s.analog[THROTTLE] = m_throttle;
		const int t = f - FLY_FROM;
		if ((t / 8) % 2) s.pressed[1] |= PILOT_LASER;
		if (t % 300 < PRESS) s.pressed[1] |= PILOT_TORPEDO;
		if (m_gunner) {
			if ((t / 8) % 2 == 0) s.pressed[1] |= GUNNER_LASER;
			if ((t + 150) % 300 < PRESS) s.pressed[1] |= GUNNER_TORPEDO;
		}
		return s;
	}

	script_inputs frame(int f, model1 &m) override
	{
		if (m_progression && f > 3300 && (f - 1) % 4 == 0) {
			// Sample the preceding fourth frame, as the original objective probe did.
			// Blue pixels are a visual heuristic, not a complete enemy recognizer.
			int best = 1000000, target_x = 248, target_y = 191;
			for (int y = 75; y < 275; y++)
				for (int x = 105; x < 390; x++) {
					const uint32_t pixel = m.frame().pix(y * m.frame_h() / 384, x * m.frame_w() / 496);
					const unsigned r = (pixel >> 16) & 255, g = (pixel >> 8) & 255, b = pixel & 255;
					if (b < 100 || 2 * b < 3 * r || 5 * b < 6 * g) continue;
					const int distance = (x - 248) * (x - 248) + (y - 191) * (y - 191);
					if (distance < best) { best = distance; target_x = x; target_y = y; }
				}
			// Measured reticle slopes: inverted X/Y, 1.41/0.93 pixels per ADC unit.
			m_visual_x = std::clamp((127 * 141 - (target_x - 248) * 100) / 141, STICK_LOW, STICK_HIGH);
			m_visual_y = std::clamp((127 * 93 - (target_y - 191) * 100) / 93, STICK_LOW, STICK_HIGH);
		}
		auto s = frame(f);
		if (!m_continuing) return s;
		const auto &ram = m.nvram();
		const unsigned time = ram[TIME] | ram[TIME + 1] << 8;
		const bool offer = ram[SEQUENCE] == SEQ_PLAYING && ram[SUBSEQUENCE] == 12 && time == 0xffff;
		if (!m_continue_frame && f > FLY_FROM && offer) {
			m_continue_frame = f;
			m_failed_score = score(m, PILOT_SCORE);
			m_empty_offer = ram[CREDITS] == 0;
		}
		if (!m_continue_frame) return s;
		if (!m_mode_frame && f > m_continue_frame + 150 &&
			ram[SEQUENCE] == SEQ_PLAYING && ram[SUBSEQUENCE] == 1)
			m_mode_frame = f;
		if (m_mode_frame && !m_resumed && time != 0xffff && time / 30 == 180) {
			m_resumed = true;
			m_resume_time = time;
			m_reset_score = score(m, PILOT_SCORE) == 0;
			m_spent_credits = ram[CREDITS] == 0;
		}
		if (m_resumed && time != 0xffff && time < m_resume_time) m_second_flight = true;
		if (m_second_flight && offer) m_second_offer = true;
		if (!m_resumed || offer) {
			s.analog[STICK_X] = s.analog[STICK_Y] = STICK_CENTRE;
			s.pressed[1] = 0;
		}
		if (!m_resumed) {
			const int elapsed = f - m_continue_frame;
			if (elapsed >= 30 && elapsed < 30 + PRESS) s.pressed[0] |= COIN;
			if ((elapsed >= 150 && elapsed < 150 + PRESS) ||
				(m_mode_frame && f >= m_mode_frame + 180 && f < m_mode_frame + 180 + PRESS))
				s.pressed[0] |= START;
		}
		return s;
	}

	std::string check(int f, model1 &m) override
	{
		if (!m_checks) return {};
		const auto &ram = m.nvram();
		const uint8_t sequence = ram[SEQUENCE];
		const unsigned time = ram[TIME] | ram[TIME + 1] << 8;
		auto reached = [&](const char *name, bool &recorded) {
			if (!recorded) {
				printf("SWA scenario reached: %s (frame %d, time %u, pilot %x, gunner %x)\n", name, f,
					time == 0xffff ? 0 : time / 30, score(m, PILOT_SCORE), score(m, GUNNER_SCORE));
				recorded = true;
			}
		};
		if (!m_credited && f >= COIN_FRAMES[0] && sequence == SEQ_PLAYING) {
			std::copy_n(ram.data() + PILOT_RANKINGS, m_initial_rankings.size(), m_initial_rankings.begin());
			reached("credited", m_credited);
		}
		// The initial clock distinguishes Training (120 s) from Rebel Attack (180 s).
		if (m_credited && !m_flying && time / 30 == unsigned(m_training ? 120 : 180))
			reached(m_training ? "Training (120-second clock)" : "Rebel Attack (180-second clock)", m_mission);
		if (m_credited && !m_flying && time != 0xffff && time != 0) {
			if (m_last_time && time < m_last_time) reached("flight (time counting down)", m_flying);
			m_last_time = time;
		}
		if (m_flying && !m_pilot_hit && score(m, PILOT_SCORE) != 0)
			reached("pilot's first hit", m_pilot_hit);
		if (m_gunner && m_flying && !m_gunner_hit && score(m, GUNNER_SCORE) != 0)
			reached("gunner's first hit", m_gunner_hit);
		if (m_progression) {
			if (sequence == SEQ_PLAYING) {
				if (m_flying && ram[SUBSEQUENCE] == 3) m_opening_flight = true;
				if (!m_briefing && m_opening_flight && ram[SUBSEQUENCE] == 4) {
					if (!m_pilot_hit || !m_gunner_hit || time == 0xffff || time == 0)
						return "SWA progression: objective briefing without both scores and a live clock";
					m_briefing_time = time;
					reached("opening objective complete (Super Star Destroyer briefing)", m_briefing);
				}
				if (!m_next_stage && ram[SUBSEQUENCE] == 5) {
					if (!m_briefing || time != m_briefing_time + 60 * 30)
						return "SWA progression: next stage without the briefing and exact 60-second bonus";
					reached("next stage (60-second bonus)", m_next_stage);
				}
				if (m_next_stage && !m_next_flight && ram[SUBSEQUENCE] == 5 &&
					time != 0xffff && time > 0 && time < m_briefing_time + 60 * 30)
					reached("next-stage flight (time counting down)", m_next_flight);
			}
			if (!m_next_flight && m_flying && (sequence == SEQ_GAME_OVER || sequence == SEQ_ATTRACT ||
				(sequence == SEQ_PLAYING && (ram[SUBSEQUENCE] == 11 || ram[SUBSEQUENCE] == 12))))
				return "SWA progression: failed before next-stage flight";
			return {};
		}
		if (m_gunner && m_flying && !m_game_over) {
			const int x = int16_t(ram[GUNNER_X] | ram[GUNNER_X + 1] << 8);
			const int y = int16_t(ram[GUNNER_Y] | ram[GUNNER_Y + 1] << 8);
			if (x < -4000) m_aim_directions |= 1;
			if (x > 4000) m_aim_directions |= 2;
			if (y < -3000) m_aim_directions |= 4;
			if (y > 3000) m_aim_directions |= 8;
			if (m_aim_directions == 15) reached("gunner aimed in all four directions", m_aimed);
		}
		if (m_continuing && m_continue_frame) {
			if (!m_empty_offer || !m_failed_score)
				return "SWA continue: first offer lacked a scored failed attempt with zero credits";
			reached("continue offered after mission failure", m_offered);
			if (!m_resumed && ram[CREDITS] == 1) reached("one continue credit accepted", m_one_credit);
			if (m_resumed) {
				if (!m_one_credit || !m_spent_credits || !m_reset_score) {
					char text[220];
					snprintf(text, sizeof text, "SWA continue at frame %d: credit accepted %d, spent %d, score reset %d (credits %u, score %x)",
						f, m_one_credit, m_spent_credits, m_reset_score, ram[CREDITS], score(m, PILOT_SCORE));
					return text;
				}
				reached("continued attempt (credits spent, score reset)", m_restart_checked);
				if (m_second_flight) reached("continued flight (time counting down)", m_second_flying_checked);
				if (m_second_flight && time != 0xffff && score(m, PILOT_SCORE) != 0)
					reached("continued attempt's first hit", m_second_hit);
			}
			if (m_second_offer) reached("second continue offer (no more coins)", m_second_offer_checked);
			if (!m_game_over && sequence == SEQ_ATTRACT)
				return "SWA returned to attract before the continued attempt's game over";
		}
		if (m_flying && !m_game_over && sequence == SEQ_GAME_OVER) {
			if (m_continuing && !m_second_offer)
				return "SWA continue: game over before the continued attempt finished";
			reached("game over", m_game_over);
			m_final_pilot = score(m, PILOT_SCORE);
			m_final_gunner = score(m, GUNNER_SCORE);
		}
		// back in attract the name entry is over: PILOT ranks the Pilot's score, PILOT & GUNNER the
		// team's total
		if (m_game_over && !m_attract && sequence == SEQ_ATTRACT) {
			if (m_training) {
				if (!std::equal(m_initial_rankings.begin(), m_initial_rankings.end(), ram.data() + PILOT_RANKINGS))
					return "SWA Training changed the Rebel Attack rankings";
				reached("attract after Training (rankings unchanged)", m_attract);
				return {};
			}
			const uint32_t table = m_gunner ? TEAM_RANKINGS : PILOT_RANKINGS;
			const int size = m_gunner ? TEAM_RANKING_SIZE : PILOT_RANKING_SIZE;
			const uint32_t final_score = m_gunner ? bcd_add(m_final_pilot, m_final_gunner) : m_final_pilot;
			if (!ranked(m, table, size, final_score)) {
				char text[200];
				snprintf(text, sizeof text, "SWA %s rankings at frame %d lack the final score %x (top three %x, %x, %x)",
					m_gunner ? "PILOT & GUNNER" : "PILOT", f, final_score, score(m, table),
					score(m, table + size), score(m, table + 2 * size));
				return text;
			}
			reached("attract after game over", m_attract);
			reached("rankings entry", m_ranked);
		}
		if (m_game_over && sequence == SEQ_PLAYING)
			return "SWA started another game after game over";
		return {};
	}

	std::string finish() override
	{
		if (!m_checks) return {};
		if (m_progression) {
			if (!(m_credited && m_mission && m_flying && m_pilot_hit && m_gunner_hit &&
				m_opening_flight && m_briefing && m_next_stage && m_next_flight))
				return "SWA progression did not complete the opening objective and enter next-stage flight";
			return {};
		}
		if (!(m_credited && m_mission && m_flying && m_pilot_hit && m_game_over && (m_training || m_ranked) && m_attract))
			return "SWA scenario did not reach every required checkpoint";
		if (m_gunner && !m_gunner_hit)
			return "SWA gunner scenario: the Gunner never scored";
		if (m_gunner && !m_aimed)
			return "SWA gunner scenario: the Gunner did not aim in all four directions";
		if (m_continuing && !(m_restart_checked && m_second_flying_checked && m_second_hit && m_second_offer_checked))
			return "SWA continue scenario did not resume, score and finish a second attempt";
		return {};
	}

	int min_frames() const override { return m_checks ? (m_progression ? 10000 : m_continuing ? 36000 : 22000) : 0; }

private:
	static uint32_t score(model1 &m, uint32_t at)
	{
		const auto &ram = m.nvram();
		const uint32_t v = ram[at] | ram[at + 1] << 8 | ram[at + 2] << 16 | uint32_t(ram[at + 3]) << 24;
		return v == 0xffffffff ? 0 : v;
	}
	static bool ranked(model1 &m, uint32_t table, int size, uint32_t final_score)
	{
		if (!final_score) return false;
		for (int i = 0; i < RANKING_ENTRIES; i++)
			if (score(m, table + i * size) == final_score) return true;
		return false;
	}
	static uint32_t bcd_add(uint32_t a, uint32_t b)
	{
		uint32_t sum = 0, carry = 0;
		for (int shift = 0; shift < 32; shift += 4) {
			uint32_t digit = ((a >> shift) & 0xf) + ((b >> shift) & 0xf) + carry;
			carry = digit > 9;
			sum |= (carry ? digit - 10 : digit) << shift;
		}
		return sum;
	}
	int rnd(int k) { m_rng = m_rng * 1103515245 + 12345; return int((m_rng >> 16) % unsigned(k)); }

	const bool m_gunner, m_training, m_continuing, m_progression;
	uint32_t m_rng;
	const bool m_checks;
	int m_next_turn = 0, m_target_x = STICK_CENTRE, m_target_y = STICK_CENTRE, m_x = STICK_CENTRE,
		m_y = STICK_CENTRE, m_throttle = 60;
	unsigned m_last_time = 0;
	unsigned m_aim_directions = 0;
	int m_visual_x = STICK_CENTRE, m_visual_y = STICK_CENTRE;
	unsigned m_briefing_time = 0;
	bool m_opening_flight = false, m_briefing = false, m_next_stage = false, m_next_flight = false;
	int m_continue_frame = 0, m_mode_frame = 0;
	unsigned m_resume_time = 0;
	uint32_t m_failed_score = 0;
	bool m_empty_offer = false, m_resumed = false, m_reset_score = false, m_spent_credits = false,
		m_second_flight = false, m_second_offer = false, m_one_credit = false,
		m_offered = false, m_restart_checked = false, m_second_flying_checked = false, m_second_hit = false,
		m_second_offer_checked = false;
	uint32_t m_final_pilot = 0, m_final_gunner = 0;
	std::array<uint8_t, RANKING_ENTRIES * (PILOT_RANKING_SIZE + TEAM_RANKING_SIZE)> m_initial_rankings{};
	bool m_credited = false, m_mission = false, m_flying = false, m_pilot_hit = false, m_gunner_hit = false,
		m_game_over = false, m_ranked = false, m_attract = false, m_aimed = false;
};

// A human's recorded run (tests/games/swa/recordings), its cabinet inputs frame by frame; after its
// last frame, nothing pressed and the axes left where they were. For the recompile's coverage and
// the native and generated-C++ lockstep: paths only manual play reaches.
class recorded final : public auto_play
{
public:
	explicit recorded(input_replay::recording r) : m_rec(std::move(r)) {}
	script_inputs frame(int f) override
	{
		return size_t(f - 1) < m_rec.frames.size() ? m_rec.frames[size_t(f - 1)] : script_inputs{};
	}
	std::string check(int f, model1 &m) override
	{
		if (size_t(f) == m_rec.frames.size() && input_replay::ram_hash(m) != m_rec.expected_ram)
			return "SWA recording diverged: RAM hash differs from the recorded run's at its last frame";
		if (size_t(f) == m_rec.frames.size()) m_matched = true;
		return {};
	}
	std::string finish() override { return m_matched ? std::string() : "SWA recording: the run ended before the recording's last frame"; }
	int min_frames() const override { return int(m_rec.frames.size()); }

private:
	input_replay::recording m_rec;
	bool m_matched = false;
};

}

std::unique_ptr<auto_play> make_auto_play(const std::string &scenario, uint32_t seed, bool checks)
{
	if (scenario == "recording" || scenario == "complete") {
		input_replay::recording r;
		if (const auto error = input_replay::load(scenario == "complete" ? COMPLETE_RECORDING : RECORDING, ID, r); !error.empty()) {
			fprintf(stderr, "SWA recording scenario (run from the repository's folder): %s\n", error.c_str());
			return nullptr;
		}
		return std::make_unique<recorded>(std::move(r));
	}
	if (!scenario.empty() && scenario != "gunner" && scenario != "training" && scenario != "continue" &&
		scenario != "progression") return nullptr;
	return std::make_unique<play>(scenario == "gunner" || scenario == "progression", scenario == "training",
		scenario == "continue", scenario == "progression", seed, checks);
}

}
