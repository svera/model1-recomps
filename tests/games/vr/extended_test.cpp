#include "../../../src/games/vr/gameplay.h"
#include "../../../src/games/vr/romset.h"
#include "../../../src/machine/model1_romset.h"
#include "../../../src/rt/native.h"
#include <array>
#include <cstdio>
#include <cstdlib>

static void require(bool condition, const char *message, int frame, int course, bool reverse)
{
	if (!condition) {
		fprintf(stderr, "Extended course %d%s frame %d: %s\n", course, reverse ? " reverse" : "", frame, message);
		std::exit(1);
	}
}

static void race(const model1::regions &regions, int course, bool reverse)
{
	model1 interpreted{model1::regions(regions)}, native{model1::regions(regions)};
	interpreted.set_drawing(false);
	native.set_drawing(false);
	rec::hints map;
	require(map.read("assets/games/vr/vr_map.txt"), "cannot read native map", 0, course, reverse);
	rec::attach(native.cpu(), "vr-extended-test");
	require(rec::build_native(native.program_rom(), map), "cannot build native code", 0, course, reverse);
	native.run_cpu = [](int cycles) { rec::run(cycles); };
	interpreted.reset();
	native.reset();
	int selection_age = 0, direction_age = 0, racing = 0;
	constexpr std::array<int, 10> wheels{61, 76, 91, 106, 120, 135, 150, 165, 180, 195};
	for (int frame = 1; frame <= 6500 && racing < 120; frame++) {
		const bool selection = interpreted.work_ram()[0x1138] == 0x10;
		const bool direction = interpreted.read8(0x502295) == 1;
		if (selection && !direction) selection_age++;
		if (direction) direction_age++;
		int wheel = 128, accelerator = 0x30;
		if (selection && !direction) {
			wheel = wheels[course];
			if (selection_age >= 180) accelerator = 0xff;
		} else if (direction) {
			wheel = reverse && direction_age >= 90 ? 200 : 128;
			if (direction_age >= 180) accelerator = 0xff;
		} else if (interpreted.work_ram()[0x1138] == 0x14) accelerator = 0xff;
		const bool coin = frame >= 1000 && frame < 1010;
		const bool start = frame >= 1120 && frame < 1130;
		for (model1 *m : {&interpreted, &native}) {
			m->m_inputs[0] = uint8_t(~((coin ? 1 : 0) | (start ? 0x10 : 0)));
			m->m_inputs[1] = m->m_inputs[2] = 0xff;
			require(m->set_analog_input(0, uint8_t(wheel)) &&
				m->set_analog_input(1, uint8_t(accelerator)) &&
				m->set_analog_input(2, 0x30), "missing I/O board", frame, course, reverse);
		}
		interpreted.run_frame();
		native.run_frame();
		for (int r = 0; r < 33; r++)
			require(interpreted.cpu().reg(r) == native.cpu().reg(r), "CPU registers differ", frame, course, reverse);
		require(interpreted.cpu().psw() == native.cpu().psw(), "CPU status differs", frame, course, reverse);
		require(interpreted.work_ram() == native.work_ram(), "work RAM differs", frame, course, reverse);
		require(interpreted.nvram() == native.nvram(), "battery RAM differs", frame, course, reverse);
		if (interpreted.work_ram()[0x1138] == 0x14) {
			require(interpreted.read8(0x501940) == course, "wrong selected course", frame, course, reverse);
			require(interpreted.read8(0x502294) == unsigned(reverse), "wrong direction", frame, course, reverse);
			racing++;
		}
	}
	require(racing == 120, "race did not start", 6500, course, reverse);
	printf("Extended course %d %s: 120 race frames match interpreter (%zu interpreter fallbacks)\n",
		course, reverse ? "reverse" : "normal", rec::fallback_count());
	rec::detach(native.cpu());
}

int main(int argc, char **argv)
{
	if (argc < 3 || argc > 4) {
		fprintf(stderr, "usage: %s vr.zip model1io.zip [course]\n", argv[0]);
		return 2;
	}
	const auto loaded = romset::load(game::vr::ROMSET, {argv[1], argv[2]});
	if (!loaded.ok || loaded.regions.save_subdirectory != "extended") {
		fprintf(stderr, "Extended set: %s\n", loaded.error.c_str());
		return 1;
	}
	const int first = argc == 4 ? std::atoi(argv[3]) : 0;
	const int last = argc == 4 ? first + 1 : 10;
	if (first < 0 || last > 10) return 2;
	for (int course = first; course < last; course++)
		for (bool reverse : {false, true}) race(loaded.regions, course, reverse);
}
