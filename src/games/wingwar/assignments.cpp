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

template<class Edit>
bool edit_record(model1 &m, Edit edit)
{
	auto *s = block(m);
	if (!s) return false;
	std::array<uint8_t, 0x80> candidate;
	memcpy(candidate.data(), s, candidate.size());
	if (!edit(candidate.data())) return false;
	const uint32_t counter = uint32_t(s[0xa]) | uint32_t(s[0xb]) << 8 |
		uint32_t(s[0xc]) << 16 | uint32_t(s[0xd]) << 24;
	if (counter >= 0x2710) return false;
	auto *next = candidate.data();
	for (int i = 0; i < 4; i++) next[0xa + i] = uint8_t((counter + 1) >> (8 * i));
	const auto sum = crc(next + 0xa);
	next[8] = uint8_t(sum);
	next[9] = uint8_t(sum >> 8);
	memcpy(next + 0x44, next + 8, 0x3c);
	memcpy(s, next, candidate.size());
	if (auto *words = m.eeprom_words())
		for (size_t i = 0; i < 0x40; i++) words[i] = uint16_t(s[i * 2] | s[i * 2 + 1] << 8);
	else memcpy(m.eeprom(), s, 0x80);
	return true;
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
	for (size_t i = 0; i < values.size(); i++)
		if (values[i] < 0 || values[i] >= ASSIGNMENTS[i].count) return false;
	return edit_record(m, [&](uint8_t *s) {
		for (size_t i = 0; i < values.size(); i++) s[ASSIGNMENTS[i].offset] = uint8_t(values[i]);
		return true;
	});
}

bool valid_coin_setting(int64_t value)
{
	if (value < 0 || value > 0xffffffffffffll || (value & 0xff) > FREE_PLAY) return false;
	for (int i = 2; i < 5; i++) if (((value >> (8 * i)) & 0xff) == 0) return false;
	return true;
}

int64_t coin_setting(model1 &m)
{
	const auto *s = block(m);
	if (!s) return -1;
	int64_t value = 0;
	for (int i = 0; i < 6; i++) value |= int64_t(s[0xe + i]) << (8 * i);
	return valid_coin_setting(value) ? value : -1;
}

bool set_coin_setting(model1 &m, int64_t value)
{
	if (!valid_coin_setting(value)) return false;
	return edit_record(m, [&](uint8_t *s) {
		bool changed = false;
		for (int i = 0; i < 6; i++) {
			const uint8_t byte = uint8_t(value >> (8 * i));
			changed |= s[0xe + i] != byte;
			s[0xe + i] = byte;
		}
		return changed;
	});
}

std::vector<uint8_t> new_eeprom(model1 &m)
{
	constexpr size_t DEFAULTS = 0x3dab4;   // (the ROM's own record, the block's first 0x44 bytes)
	const auto &rom = m.program_rom();
	if (rom.size() < DEFAULTS + 0x44 || memcmp(rom.data() + DEFAULTS, "S32AF0", 6)) return {};
	std::vector<uint8_t> s(model1::EEPROM_SIZE);
	memcpy(s.data(), rom.data() + DEFAULTS, 0x44);
	s[ASSIGNMENTS[0].offset] = 0;
	s[0xa] = 1;   // (the save count the ROM's own first save gives)
	const auto sum = crc(s.data() + 0xa);
	s[8] = uint8_t(sum);
	s[9] = uint8_t(sum >> 8);
	memcpy(s.data() + 0x44, s.data() + 8, 0x3c);
	return s;
}

int64_t free_play_coin_setting(int64_t value)
{
	// The service menu selects entry 25 and flag bit 7, leaving the credit ratios intact.
	return (value & ~int64_t(0xc0ff)) | 0x8000 | FREE_PLAY;
}

}
