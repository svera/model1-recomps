#pragma once
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <istream>
#include <sstream>
#include <string>
#include <vector>

namespace freecam {

struct pose
{
	float position[3] = {};
	float yaw = 0, pitch = 0;
};

class path
{
public:
	struct event { double time; std::string action; bool done = false; };

	bool load(std::istream &input)
	{
		keys.clear();
		events.clear();
		error.clear();
		double previous = 0;
		std::string line;
		for (int line_number = 1; std::getline(input, line); line_number++) {
			if (const size_t comment = line.find('#'); comment != std::string::npos) line.resize(comment);
			std::istringstream words(line);
			std::string command;
			if (!(words >> command)) continue;
			auto fail = [&] {
				error = "line " + std::to_string(line_number) + ": not understood";
				return false;
			};
			if (command == "notoast") continue;
			if (command == "at") {
				std::string when, action, extra;
				if (!(words >> when >> action) || (words >> extra)) return fail();
				double t;
				if (!parse_time(when, previous, t) ||
					(action != "on" && action != "off" && action != "pause" && action != "unpause" && action != "quit"))
					return fail();
				previous = t;
				events.push_back({ t, action });
				continue;
			}
			if (command != "key") return fail();
			std::string when, where;
			if (!(words >> when >> where)) return fail();
			double t;
			if (!parse_time(when, previous, t)) return fail();
			pose p;
			if (where == "pos") {
				if (!(words >> p.position[0] >> p.position[1] >> p.position[2])) return fail();
			} else if (where == "rel") {
				float radius, azimuth, height;
				if (!(words >> radius >> azimuth >> height) || radius < 0) return fail();
				const float az = azimuth * 0.017453292519943295f;
				p.position[0] = radius * std::sin(az);
				p.position[1] = height;
				p.position[2] = -radius * std::cos(az);
			} else if (where != "game") return fail();
			std::string look;
			if (!(words >> look)) return fail();
			if (look == "look") {
				float yaw, pitch;
				if (!(words >> yaw >> pitch)) return fail();
				p.yaw = yaw * 0.017453292519943295f;
				p.pitch = pitch * 0.017453292519943295f;
			} else if (look == "lookat") {
				std::string target;
				if (!(words >> target) || target != "game") return fail();
				p.yaw = std::atan2(-p.position[0], -p.position[2]);
				p.pitch = std::atan2(-p.position[1], std::hypot(p.position[0], p.position[2]));
			} else return fail();
			std::string extra;
			if (words >> extra) return fail();
			for (float value : p.position) if (!std::isfinite(value)) return fail();
			if (!std::isfinite(p.yaw) || !std::isfinite(p.pitch)) return fail();
			if (!keys.empty() && t <= keys.back().time) return fail();
			previous = t;
			keys.push_back({ t, p });
		}
		if (input.bad()) { error = "couldn't read path"; return false; }
		if (keys.empty() && events.empty()) { error = "path is empty"; return false; }
		return true;
	}

	bool at(double time, pose &out) const
	{
		if (keys.empty() || time < keys.front().time) return false;
		size_t i = 0;
		while (i + 1 < keys.size() && time >= keys[i + 1].time) ++i;
		if (i + 1 == keys.size()) { out = keys[i].value; return true; }
		const key &a = keys[i], &b = keys[i + 1];
		const float u = float((time - a.time) / (b.time - a.time));
		const float u2 = u * u, u3 = u2 * u;
		const key &before = i ? keys[i - 1] : a;
		const key &after = i + 2 < keys.size() ? keys[i + 2] : b;
		for (int axis = 0; axis < 3; ++axis) {
			const float p0 = a.value.position[axis], p1 = b.value.position[axis];
			const float m0 = i ? (p1 - before.value.position[axis]) * float((b.time - a.time) / (b.time - before.time)) : 0;
			const float m1 = i + 2 < keys.size() ? (after.value.position[axis] - p0) * float((b.time - a.time) / (after.time - a.time)) : 0;
			out.position[axis] = (2 * u3 - 3 * u2 + 1) * p0 + (u3 - 2 * u2 + u) * m0 +
				(-2 * u3 + 3 * u2) * p1 + (u3 - u2) * m1;
		}
		const float eased = u * u * u * (u * (u * 6 - 15) + 10);
		out.yaw = a.value.yaw + std::remainder(b.value.yaw - a.value.yaw, 6.283185307179586f) * eased;
		out.pitch = a.value.pitch + (b.value.pitch - a.value.pitch) * eased;
		return true;
	}

	template<class F>
	void events_through(double time, F &&apply)
	{
		for (event &e : events)
			if (!e.done && e.time <= time) { e.done = true; apply(e.action); }
	}

	const std::string &last_error() const { return error; }

private:
	struct key { double time; pose value; };
	std::vector<key> keys;
	std::vector<event> events;
	std::string error;

	static bool parse_time(const std::string &text, double previous, double &out)
	{
		char *end = nullptr;
		const double value = std::strtod(text.c_str(), &end);
		if (end == text.c_str() || *end || !std::isfinite(value)) return false;
		out = text[0] == '+' ? previous + value : value;
		return std::isfinite(out) && out >= previous && out >= 0;
	}
};

} // namespace freecam
