// Virtua Racing's scripted play: the tools' run (attract, coins, a race steering from side to side,
// shifting), with scenarios for the other courses, views and service menu, and the checkpoints a run
// must reach (the course selection, the grid, the race, the results).
#include "../script.h"
#include "../../machine/model1.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <map>
#include <numeric>

namespace game::vr {

namespace {

class play final : public auto_play
{
public:
	play(const std::string &scenario, bool checks)
		: m_alternate(scenario == "alternate"), m_intermediate(scenario == "intermediate"),
		  m_views(scenario == "views"), m_service(scenario == "service"), m_checks(checks) {}

	script_inputs frame(int f) override
	{
		script_inputs s;
		const int phase = f;
		if (m_service) {
			if (f >= 3000 && f < 3006) s.pressed[0] |= 0x04;
			if (f >= 6000 && f < 6006) s.pressed[1] |= 0x01;   // green confirms EXIT
			return s;
		}
		if (phase >= 3000 && phase < 3006) s.pressed[0] |= 0x01;   // coin
		if (phase >= 3060 && phase < 3066) s.pressed[0] |= 0x01;   // coin
		if (phase >= 3120 && phase < 3126) s.pressed[0] |= 0x10;   // start
		if (m_alternate && phase >= 3068 && phase < 3130) s.analog[0] = 0xe0;   // the wheel picks the other course
		if (m_intermediate && phase >= 3068 && phase < 3130)
			s.analog[0] = 0xb0;
		if (phase >= 3130) {
			const int driving_frame = phase - 3130;
			const int wheel_phase = driving_frame % (m_alternate ? 900 : 480);
			s.analog[0] = m_alternate
				? (phase < 3340 ? 0xe0 : (wheel_phase < 300 ? 0x20 : wheel_phase < 600 ? 0xe0 : 0x80))
				: (wheel_phase < 240
					? 0x20 + wheel_phase * 0xc0 / 239
					: 0xe0 - (wheel_phase - 240) * 0xc0 / 239);
			s.analog[1] = m_alternate                                   // accelerator
				? (driving_frame % 600 < 480 ? 0x7f : 0x30)
				: (driving_frame % 900 < 720 ? 0x7f : 0x30);
			s.analog[2] = m_alternate                                   // brake
				? (driving_frame % 600 >= 480 ? 0x7f : 0x30)
				: (driving_frame % 900 >= 720 ? 0x7f : 0x30);
			const int shift_period = m_alternate ? 240 : 180;
			const int shift_phase = driving_frame % shift_period;
			if (shift_phase < 6) s.pressed[1] |= 0x10;                  // shift down
			else if (shift_phase >= shift_period / 2 && shift_phase < shift_period / 2 + 6)
				s.pressed[1] |= 0x20;                                     // shift up
			if (m_intermediate && phase < 3340) s.analog[0] = 0xb0;
			if (m_views && phase >= 4000 && phase < 6700 && (phase - 4000) % 900 < 6)
				s.pressed[0] |= uint8_t(0x20 << ((phase - 4000) / 900));
			if (m_views && phase >= 6700 && phase < 6706) s.pressed[1] |= 0x01;   // VR4
		}
		return s;
	}

	std::string check(int f, model1 &m) override
	{
		if (!m_checks) return {};
		if (m_service) {
			// The test-mode dispatcher sets 0x40df00 to 0xff and EXIT clears it.
			if (!m_test_menu && f >= 3006 && m.nvram()[0xdf00] == 0xff) {
				printf("VR scenario reached: test menu (frame %d)\n", f);
				m_test_menu = true;
			}
			if (m_test_menu && !m_test_exit && f >= 6006 && m.nvram()[0xdf00] != 0xff) {
				printf("VR scenario reached: test menu exit (frame %d)\n", f);
				m_test_exit = true;
			}
			return {};
		}
		// Grid labels: Expert is red, Beginner cyan, and Medium yellow.
		auto course_label_colors = [&]() {
			std::array<int, 3> counts{};
			for (int y = 350; y < 380; y++)
				for (int x = 400; x < model1::SCREEN_W; x++) {
					const uint32_t pixel = m.frame().data()[y * model1::SCREEN_W + x];
					const int r = (pixel >> 16) & 0xff, g = (pixel >> 8) & 0xff, b = pixel & 0xff;
					if (r > 160 && g < 80 && b < 80) counts[0]++;
					if (b > 160 && r < 80 && g > 100 && g < 230) counts[1]++;
					if (r > 160 && g > 160 && b < 80) counts[2]++;
				}
			return counts;
		};
		auto reached = [&](const char *name, bool &recorded) {
			if (!recorded) {
				const auto [red, cyan, yellow] = course_label_colors();
				printf("VR scenario reached: %s (frame %d, course-label red/cyan/yellow=%d/%d/%d)\n",
					name, f, red, cyan, yellow);
				recorded = true;
			}
		};
		// FE1C7D copies the next state to 0x501138, then dispatches through FE1CA7.
		const uint8_t phase = m.work_ram()[0x1138];
		const uint32_t camera = m.camera();
		if (!m_track_select && f >= 3000 && phase == 0x10 && camera == 0x00502150)
			reached("track selection", m_track_select);
		if (m_track_select && !m_race_grid && phase == 0x12 && camera == 0x00502000) {
			reached("race grid", m_race_grid);
			const auto colors = course_label_colors();
			const int expected = m_alternate ? 0 : m_intermediate ? 2 : 1;
			if (!colors[expected] || colors[expected] <= colors[(expected + 1) % 3] ||
				colors[expected] <= colors[(expected + 2) % 3]) {
				char text[160];
				snprintf(text, sizeof text, "VR selected the wrong course at frame %d (red/cyan/yellow label pixels=%d/%d/%d)",
					f, colors[0], colors[1], colors[2]);
				return text;
			}
		}
		if (m_race_grid && !m_racing && phase == 0x14 && camera == 0x00502000)
			reached("active race", m_racing);
		if (m_racing && !m_results && phase == 0x1c)
			reached("course results", m_results);
		if (!m_alternate && !m_intermediate) {
			if (m_results && !m_retry_select && phase == 0x10 && camera == 0x00502150)
				reached("second-credit track selection", m_retry_select);
			if (m_retry_select && !m_retry_racing && phase == 0x14 && camera == 0x00502000)
				reached("second-credit race", m_retry_racing);
			if (m_retry_racing && !m_rankings && phase == 0x03 && camera == 0x005022d0)
				reached("attract rankings after game over", m_rankings);
		}
		return {};
	}

	std::string finish() override
	{
		if (m_service)
			return m_checks && !(m_test_menu && m_test_exit) ? "VR scenario did not enter and exit the test menu" : "";
		if (m_checks && !(m_track_select && m_race_grid && m_racing && m_results))
			return "VR scenario suite did not reach every required checkpoint";
		if (m_checks && !m_alternate && !m_intermediate && !(m_retry_select && m_retry_racing && m_rankings))
			return "VR scenario did not spend the second credit and return to attract rankings";
		return {};
	}

	int min_frames() const override { return m_checks ? 20000 : 0; }

private:
	bool m_alternate, m_intermediate, m_views, m_service, m_checks;
	bool m_track_select = false, m_race_grid = false, m_racing = false, m_results = false;
	bool m_test_menu = false, m_test_exit = false;
	bool m_retry_select = false, m_retry_racing = false, m_rankings = false;
};

class qualifying final : public auto_play
{
public:
	explicit qualifying(bool checks) : m_checks(checks) {}

	script_inputs frame(int f) override
	{
		script_inputs s;
		auto pulse = [f](int first) { return f >= first && f < first + 6; };
		if (f <= 5000) {
			if (pulse(3000)) s.pressed[0] |= 0x04;
			if (pulse(3100) || pulse(3120) || pulse(3440) || pulse(3540) ||
				(f >= 3200 && f < 3400 && (f - 3200) % 20 < 6) ||
				(f >= 4000 && f < 4480 && (f - 4000) % 60 < 6)) s.pressed[0] |= 0x20;
			if (pulse(3420)) s.pressed[0] |= 0x40;
			if (pulse(3140) || pulse(3500) || pulse(3580) || pulse(4560)) s.pressed[1] |= 0x01;
			return s;
		}
		s = m_start.frame(f - 3000);
		if (pulse(6060)) s.pressed[0] = 0;   // One credit; return to attract after entering initials.
		if (f >= 6130) {
			s.analog[0] = 0x80;
			s.analog[1] = 0xff;
			s.analog[2] = 0x30;
			s.pressed[1] = 0;
		}
		return s;
	}

	script_inputs frame(int f, model1 &m) override
	{
		auto s = frame(f);
		const auto phase = m.work_ram()[0x1138];
		if (phase == 0x14 && f >= 6130) {
			if (!m_road_ready) decode_road(m.coprocessor_data_rom());
			drive(f, m, s);
		}
		if (phase == 0x1e && m.read32(0x5018fc)) {
			const uint32_t confirmed = m.read32(0x501910);
			require(confirmed <= 3, "invalid name-entry cursor");
			// FC488A maps the calibrated wheel to the alphabet; FC4546 confirms with the pedal edge.
			constexpr std::array<int, 3> wheel{ 148, 68, 100 };   // SDK
			s.analog[0] = wheel[std::min(confirmed, 2u)];
			s.analog[1] = s.analog[2] = 0x30;
			if (confirmed < 3 && m_name_age % 60 >= 30 && m_name_age % 60 < 36)
				s.analog[1] = 0xff;
			m_name_age++;
		}
		return s;
	}

	std::string check(int f, model1 &m) override
	{
		if (!m_checks) return {};
		auto reached = [f](const char *name, bool &flag) {
			if (!flag) printf("VR qualifying reached: %s (frame %d)\n", name, f);
			flag = true;
		};
		if (f == 3100) {
			if (!m.eeprom_words()) return "VR qualifying requires the I/O board EEPROM";
			m_difficulty = m.eeprom_words()[16];
			if ((m_difficulty & 0x0300) != 0x0100)
				return "VR qualifying requires fresh NORMAL difficulty settings";
		}
		if (f >= 3006 && f <= 4560 && m.nvram()[0xdf00] == 0xff)
			reached("test menu", m_test_menu);
		if (m_test_menu && f == 5000) {
			if (m.eeprom_words()[16] != uint16_t(m_difficulty & ~0x0300) || m.nvram()[0xdf00] == 0xff)
				return "VR qualifying did not save the easier arcade settings and exit the test menu";
			reached("saved arcade settings and test exit", m_test_exit);
		}
		if (f >= 6000)
			if (const auto bad = m_start.check(f, m); !bad.empty()) return bad;
		const auto phase = m.work_ram()[0x1138];
		if (m_test_exit && phase == 0x10 && m.camera() == 0x00502150) m_track_select = true;
		if (m_track_select && phase == 0x12 && m.camera() == 0x00502000) m_grid = true;
		if (m_grid && phase == 0x14) reached("active race", m_racing);
		if (m_racing && phase == 0x19) {
			m_time = m.read32(0x501168);
			if (!m_time) return "VR qualifying finish has an invalid race time";
			reached("qualifying finish", m_finish);
		}
		if (m_finish && phase == 0x1c) reached("results", m_results);
		if (m_results && phase == 0x1e && m.read32(0x5018fc)) {
			reached("ranking-name entry", m_name_entry);
			const uint32_t confirmed = m.read32(0x501910);
			if (confirmed > 3 || confirmed > m_confirmed + 1)
				return "VR qualifying did not confirm the initials individually";
			constexpr uint32_t initials = 0x004b4453;   // SDK, little endian.
			const uint32_t mask = confirmed == 3 ? 0xffffff : (1u << (8 * confirmed)) - 1;
			if ((m.read32(0x501904) & mask) != (initials & mask))
				return "VR qualifying confirmed the wrong initials";
			m_confirmed = std::max(m_confirmed, confirmed);
			if (confirmed == 3) {
				const uint32_t table = m.read32(0x501918);
				if (table < 0x400000 || table > 0x40fffc)
					return "VR qualifying has an invalid ranking pointer table";
				m_record = m.read32(table);
				if (m_record < 0x400000 || m_record > 0x40ffe8 ||
					m_record != m.read32(0x5018f0) ||
					m.read32(m_record + 4) != m_time ||
					m.read32(m_record + 0x14) != initials)
					return "VR qualifying did not save the initials and finishing time in battery RAM";
				reached("SDK initials and finishing time saved", m_saved);
			}
		}
		if (m_saved && phase == 0x03 && m.camera() == 0x005022d0) {
			if (m.read32(m_record + 4) != m_time || m.read32(m_record + 0x14) != 0x004b4453)
				return "VR qualifying lost the saved ranking on returning to attract";
			reached("attract rankings with saved entry", m_rankings);
		}
		return {};
	}

	std::string finish() override
	{
		return m_checks && !(m_test_menu && m_test_exit && m_track_select && m_grid && m_racing && m_finish && m_results &&
			m_name_entry && m_confirmed == 3 && m_saved && m_rankings)
			? "VR qualifying did not finish the race, confirm SDK and return to attract with its saved ranking" : "";
	}

	int min_frames() const override { return m_checks ? 26000 : 0; }

private:
	static constexpr int quad_count = 1093;
	static constexpr float tau = 6.2831853f;
	struct point { float x = 0, z = 0; };
	struct row { point front, back; int count = 0, next = -1; };
	std::array<int, quad_count> m_root{};
	std::array<row, quad_count> m_rows{};
	bool m_road_ready = false, m_checks;
	int m_gear = 0, m_name_age = 0;
	play m_start{ "", true };
	uint16_t m_difficulty = 0;
	uint32_t m_time = 0, m_record = 0, m_confirmed = 0;
	bool m_test_menu = false, m_test_exit = false, m_racing = false, m_finish = false;
	bool m_track_select = false, m_grid = false;
	bool m_results = false, m_name_entry = false, m_saved = false, m_rankings = false;

	static void require(bool valid, const char *message)
	{
		if (!valid) {
			fprintf(stderr, "VR qualifying driver: %s\n", message);
			std::abort();
		}
	}

	static float distance(point a, point b) { return std::hypot(a.x - b.x, a.z - b.z); }
	static float tangent(const row &r) { return std::atan2(r.front.x - r.back.x, r.front.z - r.back.z); }

	void decode_road(const std::vector<uint8_t> &rom)
	{
		auto word = [&](size_t index) {
			require(index < rom.size() / 4, "track data outside coprocessor ROM");
			const size_t at = index * 4;
			return uint32_t(rom[at]) | uint32_t(rom[at + 1]) << 8 |
				uint32_t(rom[at + 2]) << 16 | uint32_t(rom[at + 3]) << 24;
		};
		const size_t base = word(0x22);   // Beginner's primary road loop, excluding verges and pit surfaces.
		std::array<std::array<point, 4>, quad_count> quads;
		std::iota(m_root.begin(), m_root.end(), 0);
		auto root = [&](int i) { while (m_root[i] != i) i = m_root[i]; return i; };
		using edge = std::array<int, 4>;
		auto key = [](point a, point b) {
			return edge{ int(std::round(a.x * 100)), int(std::round(a.z * 100)),
				int(std::round(b.x * 100)), int(std::round(b.z * 100)) };
		};
		std::map<edge, int> left;
		for (int i = 0; i < quad_count; i++) {
			for (int j = 0; j < 4; j++) {
				auto &p = quads[i][j];
				p = { std::bit_cast<float>(word(base + i * 16 + j * 3)),
					std::bit_cast<float>(word(base + i * 16 + j * 3 + 2)) };
				require(std::isfinite(p.x) && std::isfinite(p.z) &&
					std::fabs(p.x) < 100000 && std::fabs(p.z) < 100000, "invalid road coordinates");
			}
			left[key(quads[i][0], quads[i][3])] = i;
		}
		// Curves have multiple lateral quads per row; looking ahead by quad number drives off-road.
		for (int i = 0; i < quad_count; i++) {
			const auto it = left.find(key(quads[i][1], quads[i][2]));
			if (it != left.end()) m_root[root(i)] = root(it->second);
		}
		for (int i = 0; i < quad_count; i++) {
			m_root[i] = root(i);
			auto &r = m_rows[m_root[i]];
			r.count++;
			for (int j = 0; j < 2; j++) {
				r.front.x += quads[i][j].x / 2; r.front.z += quads[i][j].z / 2;
				r.back.x += quads[i][j + 2].x / 2; r.back.z += quads[i][j + 2].z / 2;
			}
		}
		for (auto &r : m_rows) if (r.count) {
			r.front.x /= r.count; r.front.z /= r.count;
			r.back.x /= r.count; r.back.z /= r.count;
		}
		for (int i = 0; i < quad_count; i++) {
			auto &r = m_rows[i];
			if (!r.count) continue;
			require(distance(r.front, r.back) > 0, "zero-length road row");
			float best = 1;
			for (int j = 0; j < quad_count; j++)
				if (i != j && m_rows[j].count && distance(r.front, m_rows[j].back) < best) {
					best = distance(r.front, m_rows[j].back);
					r.next = j;
				}
		}
		m_road_ready = true;
	}

	void drive(int f, model1 &m, script_inputs &s)
	{
		const uint32_t segment = m.read32(0x400d30);
		require(segment < quad_count, "player left the Beginner road loop");
		const point car{ std::bit_cast<float>(m.read32(0x400d1e)),
			std::bit_cast<float>(m.read32(0x400d26)) };
		require(std::isfinite(car.x) && std::isfinite(car.z), "invalid player position");
		int rowid = m_root[segment], ahead = rowid;
		float travelled = 0, curve = 0;
		const float initial = tangent(m_rows[rowid]);
		for (int k = 0; k < 30 && travelled < 60; k++) {
			const auto &r = m_rows[ahead];
			curve = std::max(curve, std::fabs(std::remainder(tangent(r) - initial, tau)));
			travelled += distance(r.front, r.back);
			// Centers do not meet at the pit fork/merge and road-width transitions.
			if (r.next < 0) break;
			ahead = r.next;
		}
		const int limit = curve > .7f ? 245 : curve > .35f ? 305 : 325;
		const uint32_t speed = m.read32(0x400d14);
		if (speed > uint32_t(limit)) {
			s.analog[1] = 0x30;
			s.analog[2] = speed > uint32_t(limit + 10) ? 0xff : 0x30;
		}
		point target = m_rows[rowid].front;
		for (int k = 0; k < 30 && distance(target, car) < 40; k++) {
			if (m_rows[rowid].next < 0) break;
			rowid = m_rows[rowid].next;
			target = m_rows[rowid].front;
		}
		if (curve < .12f) {
			const float angle = tangent(m_rows[rowid]);
			target.x -= 3 * std::cos(angle);
			target.z += 3 * std::sin(angle);
		}
		const float heading = -int16_t(m.read16(0x400d36)) * tau / 65536;
		const float error = std::remainder(std::atan2(target.x - car.x, target.z - car.z) - heading, tau);
		s.analog[0] = std::clamp(int(128 + error * 240), 32, 224);
		if (m_gear < 3 && speed > uint32_t(90 + m_gear * 45) && f % 60 == 0) {
			s.pressed[1] = 0x20;
			m_gear++;
		}
	}
};

}

std::unique_ptr<auto_play> make_auto_play(const std::string &scenario, uint32_t, bool checks)
{
	if (scenario == "qualifying") return std::make_unique<qualifying>(checks);
	if (!scenario.empty() && scenario != "alternate" && scenario != "intermediate" &&
		scenario != "views" && scenario != "service") return nullptr;
	return std::make_unique<play>(scenario, checks);
}

}
