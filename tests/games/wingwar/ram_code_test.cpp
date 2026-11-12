#include "../../../src/games/wingwar/profile.h"
#include "../../../src/games/script.h"
#include "../../../src/host/regions.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <set>
#include <tuple>
#include <cstdlib>

namespace {

class traced_machine final : public model1
{
public:
	using model1::model1;
	struct copy {
		uint64_t frame;
		uint32_t writer, destination, source, size, table;
	};
	std::vector<copy> copies;
	std::array<uint32_t, 0x1000> origins{};
	std::set<uint32_t> executed;
	size_t clears = 0, changed_after_execution = 0;
	uint32_t instruction = 0, source = 0, destination = 0, table = 0;

	void write8(uint32_t address, uint8_t value) override
	{
		observe(address, value);
		model1::write8(address, value);
	}

	void write16(uint32_t address, uint16_t value) override
	{
		if (!(address & 1)) {
			observe(address, uint8_t(value));
			observe(address + 1, uint8_t(value >> 8));
		}
		model1::write16(address, value);
	}

	void observe(uint32_t address, uint8_t value)
	{
		address &= 0xffffff;
		if (address < 0x40d000 || address >= 0x40e000) return;
		const size_t offset = address - 0x40d000;
		if (executed.count(address) && read8(address) != value) changed_after_execution++;
		if (instruction == 0xa0 || instruction == 0xa3 || instruction == 0xa6 || instruction == 0xa9) {
			assert(value == 0);
			clears++;
			origins[offset] = 0;
			return;
		}
		if (instruction != 0x2555 && instruction != 0x2583) {
			fprintf(stderr, "Unexpected Wing War RAM-code writer %06x at %06x\n", instruction, address);
			std::abort();
		}
		assert(source < 0x100000);
		assert(address == destination);
		assert(value == read8(source));
		origins[offset] = source;
		if (!copies.empty()) {
			auto &last = copies.back();
			if (last.frame == frame_number() && last.writer == instruction &&
				last.destination + last.size == address && last.source + last.size == source) {
				last.size++;
				return;
			}
		}
		copies.push_back({frame_number(), instruction, address, source, 1, table});
	}
};

}

int main(int argc, char **argv)
{
	if (argc != 2) {
		fprintf(stderr, "usage: %s regions-dir (requires wingwar.zip)\n", argv[0]);
		return 2;
	}
	traced_machine m(load_regions(argv[1], game::wingwar::ID));
	auto play = game::make_auto_play(game::wingwar::ID, "attract", 1, true);
	assert(play);
	m.reset();
	std::set<std::tuple<uint32_t, uint32_t, uint32_t>> images;
	std::set<uint32_t> tables;
	m.on_instruction = [&](uint32_t pc) {
		m.instruction = pc;
		if (pc == 0x2538) m.table = 0x3668;
		if (pc == 0x2560) {
			const uint8_t selector = m.read8(m.cpu().reg(25) + 0x7302);
			assert(selector <= 2);
			m.table = m.read32(0x365c + selector * 4);
		}
		if (pc == 0x2555 || pc == 0x2583) {
			m.source = m.cpu().reg(2);
			m.destination = m.cpu().reg(1);
		}
		if (pc < 0x40d000 || pc >= 0x40e000) return;
		const auto origin = m.origins[pc - 0x40d000];
		assert(origin && m.read8(pc) == m.read8(origin));
		m.executed.insert(pc);
	};
	for (int f = 1; f <= play->min_frames(); f++) {
		const auto in = play->frame(f);
		for (size_t c = 0; c < in.analog.size(); c++)
			if (in.analog[c] >= 0) assert(m.set_analog_input(c, uint8_t(in.analog[c])));
		for (int p = 0; p < 3; p++) m.m_inputs[p] = uint8_t(~in.pressed[p]);
		m.run_frame();
		const auto error = play->check(f, m);
		if (!error.empty()) { fprintf(stderr, "%s\n", error.c_str()); return 1; }
	}
	const auto error = play->finish();
	if (!error.empty()) { fprintf(stderr, "%s\n", error.c_str()); return 1; }
	for (const auto &copy : m.copies) {
		images.emplace(copy.destination, copy.source, copy.size);
		tables.insert(copy.table);
		bool found = false;
		for (int i = 0; i < 8; i++) {
			const uint32_t row = copy.table + i * 12;
			if (m.read32(row) == copy.destination && m.read32(row + 4) == copy.source &&
				m.read32(row + 8) == copy.source + copy.size) found = true;
		}
		assert(found && (m.read32(copy.table + 8 * 12) & 0x80000000));
		printf("frame %llu writer %06x table %06x: ROM %06x..%06x -> RAM %06x..%06x (%u bytes)\n",
			(unsigned long long)copy.frame, copy.writer, copy.table, copy.source, copy.source + copy.size,
			copy.destination, copy.destination + copy.size, copy.size);
	}
	assert(m.executed.size() == 232);
	assert(tables.size() == 3 && images.size() == 17 && m.copies.size() == 72);
	assert(m.clears == 0x1000 && m.changed_after_execution > 0);
	printf("Wing War RAM-code trace passed: %zu executed RAM PCs, %zu distinct ROM-copy mappings, "
		"%zu changed writes to previously executed opcode addresses; no other writers\n",
		m.executed.size(), images.size(), m.changed_after_execution);
}
