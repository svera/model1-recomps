// The launch-time loader against tools/romset.py: every region built from the player's zip in C++
// must match the one the Python tool writes to build/<game>/ (both need the player's own ROMs).
//     romset_test <game> roms/<game>.zip [roms/model1io.zip] build/<game>
// The games are the ones linked in (src/games/*/romset.cpp), found by name.
#include "../src/machine/model1_romset.h"
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>
#include <zlib.h>

static bool read_file(const std::string &path, std::vector<uint8_t> &out)
{
	FILE *f = fopen(path.c_str(), "rb");
	if (!f) return false;
	fseek(f, 0, SEEK_END);
	out.resize(size_t(ftell(f)));
	fseek(f, 0, SEEK_SET);
	const bool ok = fread(out.data(), 1, out.size(), f) == out.size();
	fclose(f);
	return ok;
}

static bool swa_layout(const model1::regions &r)
{
	if (r.maincpu.size() != 0x2000000 || r.sndcpu.size() != 0xc0000 ||
		r.polygons.size() != 0xc00000 || r.copro_data.size() != 0x200000 ||
		r.tgp_program.size() != 0x2000 || r.mpegcpu.size() != 0x20000 || r.mpeg.size() != 0x800000) {
		fprintf(stderr, "swa: incorrect region sizes\n");
		return false;
	}
	auto crc = [](const std::vector<uint8_t> &data, size_t offset, size_t size, uint32_t expected) {
		return uint32_t(crc32(0, data.data() + offset, uInt(size))) == expected;
	};
	for (size_t offset : { 0x000000, 0x080000, 0xf80000 })
		if (!crc(r.maincpu, offset, 0x80000, 0x9e112425)) {
			fprintf(stderr, "swa: program mirror at %06zx is missing or wrong\n", offset);
			return false;
		}
	for (size_t offset : { 0x00000, 0x80000 }) {
		std::vector<uint8_t> unswapped(0x20000);
		for (size_t i = 0; i < unswapped.size(); i++) unswapped[i] = r.sndcpu[offset + (i ^ 1)];
		if (!crc(unswapped, 0, unswapped.size(), 0x7da18cf7)) {
			fprintf(stderr, "swa: byte-swapped sound reload at %05zx is missing or wrong\n", offset);
			return false;
		}
	}
	if (!std::all_of(r.sndcpu.begin() + 0x20000, r.sndcpu.begin() + 0x80000, [](uint8_t b) { return b == 0; }) ||
		!std::all_of(r.sndcpu.begin() + 0xa0000, r.sndcpu.end(), [](uint8_t b) { return b == 0; }) ||
		!std::all_of(r.mpeg.begin() + 0x400000, r.mpeg.end(), [](uint8_t b) { return b == 0; }) ||
		!crc(r.tgp_program, 0, 0x2000, 0xc5ddb8fc) ||
		!crc(r.mpegcpu, 0, 0x20000, 0xf4ee84a4) ||
		!crc(r.mpeg, 0, 0x200000, 0x3175b0be) ||
		!crc(r.mpeg, 0x200000, 0x200000, 0x3114d748)) {
		fprintf(stderr, "swa: region fill, TGP program or DSB data is wrong\n");
		return false;
	}
	puts("swa: all three program copies, byte-swapped sound reload, TGP and DSB data verified");
	return true;
}

int main(int argc, char **argv)
{
	if (argc < 4) { fprintf(stderr, "usage: %s game game.zip [model1io.zip] regions-dir\n", argv[0]); return 2; }
	const romset::definition *found = romset::find(argv[1]);
	if (!found) { fprintf(stderr, "%s: no such game in this test\n", argv[1]); return 2; }
	const romset::definition &set = *found;
	std::vector<std::string> zips(argv + 2, argv + argc - 1);
	const std::string dir = argv[argc - 1];
	const romset::result r = romset::load(set, zips);
	if (!r.ok) { fprintf(stderr, "%s: %s\n", argv[1], r.error.c_str()); return 1; }
	if (set.title.name == "swa" && !swa_layout(r.regions)) return 1;
	if (r.regions.title != set.title) { fprintf(stderr, "%s: wrong title\n", argv[1]); return 1; }
	if (!romset::is_game_set(set, zips[0])) { fprintf(stderr, "%s: %s not recognised\n", argv[1], zips[0].c_str()); return 1; }
	for (const romset::definition *other : romset::registered())
		if (other != &set && romset::is_game_set(*other, zips[0])) { fprintf(stderr, "%s: taken for %s\n", argv[1], other->zip); return 1; }
	struct { const char *file; const std::vector<uint8_t> &got; } regions[] = {
		{ "maincpu", r.regions.maincpu }, { "copro_tables", r.regions.copro_tables }, { "copro_data", r.regions.copro_data },
		{ "polygons", r.regions.polygons }, { "sndcpu", r.regions.sndcpu }, { "pcm1", r.regions.pcm1 },
		{ "pcm2", r.regions.pcm2 }, { "tgp_program", r.regions.tgp_program }, { "ioboard", r.regions.ioboard },
		{ "mpegcpu", r.regions.mpegcpu }, { "mpeg", r.regions.mpeg },
	};
	int bad = 0, checked = 0;
	for (const auto &reg : regions) {
		std::vector<uint8_t> want;
		if (!read_file(dir + "/" + reg.file + ".bin", want)) {
			if (!reg.got.empty()) { fprintf(stderr, "%s: %s loaded, but %s/%s.bin is missing\n", argv[1], reg.file, dir.c_str(), reg.file); bad++; }
			continue;
		}
		checked++;
		if (want != reg.got) { fprintf(stderr, "%s: %s differs (%zu bytes vs %zu)\n", argv[1], reg.file, reg.got.size(), want.size()); bad++; }
	}
	if (bad) return 1;
	printf("%s: %d regions match %s\n", argv[1], checked, dir.c_str());
	return 0;
}
