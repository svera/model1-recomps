#include "assignments.h"
#include "../../machine/model1.h"
#include <cstring>

namespace game::wingwar {
namespace {

uint16_t crc(const uint8_t *data)
{
	uint32_t r = 0xdeb00;
	for (int i = 0; i <= 0x3a; i++) {
		if (i < 0x3a) r = (r & 0xffffff00) | data[i];
		for (int bit = 0; bit < 8; bit++)
			r = (r & 0x80000000) ? (r << 1) ^ 0x10210000 : r << 1;
	}
	return uint16_t(r >> 16);
}

uint8_t *block(model1 &m)
{
	auto *s = m.nvram().data() + 0xf800;
	if (memcmp(s, "S32AF0", 6) || crc(s + 0xa) != uint16_t(s[8] | s[9] << 8)) return nullptr;
	return s;
}

}

int assignment_value(model1 &m, size_t index)
{
	const auto *s = block(m);
	if (!s || index >= ASSIGNMENTS.size()) return -1;
	const auto &a = ASSIGNMENTS[index];
	return s[a.offset] < a.count ? s[a.offset] : -1;
}

bool set_assignments(model1 &m, const std::array<int, 7> &values)
{
	auto *s = block(m);
	if (!s) return false;
	for (size_t i = 0; i < values.size(); i++)
		if (values[i] < 0 || values[i] >= ASSIGNMENTS[i].count) return false;
	const uint32_t counter = uint32_t(s[0xa]) | uint32_t(s[0xb]) << 8 |
		uint32_t(s[0xc]) << 16 | uint32_t(s[0xd]) << 24;
	if (counter >= 0x2710) return false;
	for (int i = 0; i < 4; i++) s[0xa + i] = uint8_t((counter + 1) >> (8 * i));
	for (size_t i = 0; i < values.size(); i++) s[ASSIGNMENTS[i].offset] = uint8_t(values[i]);
	const auto sum = crc(s + 0xa);
	s[8] = uint8_t(sum);
	s[9] = uint8_t(sum >> 8);
	memcpy(s + 0x44, s + 8, 0x3c);
	if (auto *words = m.eeprom_words())
		for (size_t i = 0; i < 0x40; i++) words[i] = uint16_t(s[i * 2] | s[i * 2 + 1] << 8);
	else memcpy(m.eeprom(), s, 0x80);
	return true;
}

}
