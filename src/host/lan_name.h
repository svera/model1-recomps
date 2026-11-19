#pragma once
#include "../../third_party/arcade-lan/include/arcade_lan.h"
#include <string>
#include <string_view>
#include <utility>

inline bool valid_lan_name(std::string_view name)
{
	if (name.empty() || name.size() > LAN_NAME_LENGTH || name.front() == ' ' || name.back() == ' ') return false;
	for (char c : name)
		if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-' || c == ' ')) return false;
	return true;
}

struct lan_name_editor
{
	std::string text;
	bool selected = true;

	bool append(std::string_view input)
	{
		std::string next = selected ? "" : text;
		for (char c : input) {
			if (c >= 'a' && c <= 'z') c = char(c - 'a' + 'A');
			if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-' || c == ' ') ||
				next.size() >= LAN_NAME_LENGTH) return false;
			next += c;
		}
		text = std::move(next);
		selected = false;
		return true;
	}

	void backspace()
	{
		if (selected) text.clear();
		else if (!text.empty()) text.pop_back();
		selected = false;
	}
};
