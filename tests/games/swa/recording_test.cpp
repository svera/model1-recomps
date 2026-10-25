// A human's recorded Star Wars Arcade run, replayed through the cabinet inputs only: the route
// probe's input prefix to the Super Star Destroyer trench, then manual play that completes the
// trench objective, reaches the next stage, takes two moving-collision hits (0x0c7f33) and is shot
// down to the continue offer; then continues, which restarts that stage with full shield, and is shot
// down again. The recording holds no game state, so it reproduces only if emulation does.
// Every game-state and shield change from the trench handoff on must match exactly on the interpreter
// (silent, the arcade's models) and the native recompile (with sound, LOD off as players get it),
// with no interpreter fallback. At the handoff points the recordings were made at, the interpreter's
// RAM checksums must match exactly, and native code's game RAM the interpreter's: LOD off draws more
// models (Star Destroyers near), which takes the CPU longer, so a frame can end at another point of
// the game's update; only the stack and the display list's end then differ by the checkpoints.
#include "../../../src/games/swa/gameplay.h"
#include "../../../src/games/swa/profile.h"
#include "../../../src/host/input_replay.h"
#include "../../../src/host/regions.h"
#include "../../../src/rt/native.h"
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

using namespace game::swa;

namespace {

constexpr uint32_t SEQUENCE = 0x40f008, SUBSEQUENCE = 0x40f009, SHIELD = 0x40f772;
constexpr size_t TRACE_FROM = 16600;

struct checkpoint { size_t frames; uint64_t hash; const char *what; };
// (frame index whose run made the change, sequence, subsequence, shield)
struct state { size_t frame; unsigned seq, sub, shield; bool operator==(const state &) const = default; };

// RAM less the stack and the display list's end, which depend on where in the update the frame ended
constexpr uint32_t RAM_A = 0x400000, DISPLAY_LIST_END = 0x40f068, STACK = 0x40fc00;

uint64_t game_ram_hash(model1 &m)
{
	uint64_t h = 1469598103934665603ull;
	auto add = [&](uint8_t b) { h = (h ^ b) * 1099511628211ull; };
	for (uint32_t a = RAM_A; a < STACK; a++)
		if (a != DISPLAY_LIST_END && a != DISPLAY_LIST_END + 1) add(m.nvram()[a - RAM_A]);
	for (uint8_t b : m.work_ram()) add(b);
	return h;
}

struct recorded_run {
	const char *path;
	std::vector<checkpoint> checkpoints;   // the last: the recording's own end
	std::vector<state> expected;
	const char *summary;
};

const recorded_run TRENCH_TO_CONTINUE = {
	"tests/games/swa/recordings/manual-trench-to-continue.inputs",
	{
		{ 16600, 0xc16a83c62483b8dbull, "trench handoff (probe route)" },
		{ 21635, 0x2a75b44aa4d36d73ull, "next stage reached (manual)" },
		{ 24755, 0x58d26030f4c3e5cbull, "continue offer (manual)" },
		{ 38123, 0xf12b425006ef5fe3ull, "continued next stage to the next offer (manual)" },
	},
	{
		{ 16618, 13, 5, 576 },     // moving-collision hit (0x0c7f33) just after the handoff
		{ 17610, 13, 5, 512 },
		{ 17774, 13, 5, 448 },
		{ 17956, 13, 5, 384 },
		{ 17961, 13, 6, 384 },     // trench objective complete
		{ 18203, 13, 8, 384 },
		{ 19385, 13, 9, 384 },
		{ 19394, 13, 1, 384 },     // next stage: briefing
		{ 20418, 13, 2, 384 },
		{ 22016, 13, 3, 384 },     // next-stage flight
		{ 22330, 13, 3, 1 },       // moving-collision hit (0x0c7f33, 0x100)
		{ 24174, 13, 3, 0 },       // second one
		{ 24498, 13, 3, 65535 },   // shot down
		{ 24499, 13, 11, 65535 },
		{ 24644, 13, 12, 65535 },  // continue offer
		{ 25434, 13, 1, 65535 },   // coin and Start: continued
		{ 26438, 13, 9, 65535 },
		{ 26440, 13, 9, 1792 },    // full shield, score reset
		{ 26461, 13, 1, 1792 },    // the same stage's briefing again
		{ 27485, 13, 2, 1792 },
		{ 29083, 13, 3, 1792 },    // its flight
		{ 29553, 13, 3, 1536 },    // eight moving-collision hits (0x0c7f33, 0x100 each but the last)
		{ 30333, 13, 3, 1280 },
		{ 31157, 13, 3, 1024 },
		{ 35487, 13, 3, 768 },
		{ 35557, 13, 3, 512 },
		{ 36081, 13, 3, 256 },
		{ 36439, 13, 3, 1 },
		{ 37517, 13, 3, 0 },
		{ 37841, 13, 3, 65535 },   // shot down
		{ 37842, 13, 11, 65535 },
		{ 37987, 13, 12, 65535 },  // the next continue offer
	},
	"trench objective, next stage, continue and both offers",
};

const recorded_run COMPLETE_GAME = {
	"tests/games/swa/recordings/manual-complete-game.inputs",
	{
		{ 16600, 0xc16a83c62483b8dbull, "trench handoff (probe route)" },
		{ 21635, 0x2a75b44aa4d36d73ull, "next stage reached (manual)" },
		{ 24755, 0x58d26030f4c3e5cbull, "continue offer (manual)" },
		{ 44637, 0x036ee06c0ec08506ull, "Death Star continue offer (manual, Gunner assist)" },
		{ 50523, 0xc5b24292ae90c8c2ull, "Death Star tunnel, continued (manual)" },
		{ 57549, 0x1e390d36a3331dbeull, "the ending (manual)" },
	},
	{
		{ 16618, 13, 5, 576 },      // moving-collision hit (0x0c7f33) just after the handoff
		{ 17610, 13, 5, 512 },
		{ 17774, 13, 5, 448 },
		{ 17956, 13, 5, 384 },
		{ 17961, 13, 6, 384 },      // trench objective complete
		{ 18203, 13, 8, 384 },
		{ 19385, 13, 9, 384 },
		{ 19394, 13, 1, 384 },      // next stage: briefing
		{ 20418, 13, 2, 384 },
		{ 22016, 13, 3, 384 },      // next-stage flight
		{ 22330, 13, 3, 1 },
		{ 24174, 13, 3, 0 },
		{ 24498, 13, 3, 65535 },    // shot down
		{ 24499, 13, 11, 65535 },
		{ 24644, 13, 12, 65535 },   // continue offer
		{ 25048, 13, 1, 65535 },    // coin and Start: continued (PILOT & GUNNER, Gunner assist)
		{ 25668, 13, 9, 65535 },
		{ 25670, 13, 9, 1792 },     // full shield, score reset
		{ 25691, 13, 1, 1792 },
		{ 26715, 13, 2, 1792 },
		{ 28313, 13, 3, 1792 },     // its flight: 24 TIE fighters in 200 s
		{ 31872, 13, 4, 1792 },     // objective complete: time bonus
		{ 32356, 13, 5, 1792 },     // the Death Star surface trench
		{ 34679, 13, 5, 1664 },     // fixed hazards (0x80 each)
		{ 34868, 13, 5, 1536 },
		{ 35452, 13, 6, 1536 },     // surface trench complete
		{ 35902, 13, 7, 1536 },     // the tunnel
		{ 37329, 13, 7, 1472 },     // wall and fin contacts (0x40 each, one a second while wedged)
		{ 37389, 13, 7, 1408 },
		{ 38155, 13, 7, 1344 },
		{ 38225, 13, 7, 1280 },
		{ 39229, 13, 7, 1216 },
		{ 39315, 13, 7, 1152 },
		{ 40393, 13, 7, 1088 },
		{ 40499, 13, 7, 1024 },
		{ 40563, 13, 7, 960 },
		{ 41685, 13, 7, 896 },
		{ 41745, 13, 7, 832 },
		{ 42857, 13, 7, 768 },
		{ 43321, 13, 7, 512 },      // shots from the rings at its end (0x100)
		{ 43671, 13, 7, 256 },
		{ 43873, 13, 7, 1 },
		{ 44065, 13, 7, 0 },
		{ 44389, 13, 7, 65535 },    // shot down
		{ 44390, 13, 11, 65535 },
		{ 44535, 13, 12, 65535 },   // continue offer
		{ 44921, 13, 1, 65535 },    // coin and Start: continued
		{ 45503, 13, 9, 65535 },
		{ 45505, 13, 9, 1792 },     // full shield; the Death Star stage again
		{ 45520, 13, 2, 1792 },
		{ 45582, 13, 3, 1792 },
		{ 45583, 13, 4, 1792 },
		{ 46059, 13, 5, 1792 },     // its surface trench
		{ 47108, 13, 5, 1536 },
		{ 48577, 13, 5, 1408 },
		{ 49163, 13, 6, 1408 },
		{ 49611, 13, 7, 1408 },     // its tunnel
		{ 51818, 13, 7, 1344 },     // contacts again
		{ 51920, 13, 7, 1280 },
		{ 51980, 13, 7, 1216 },
		{ 52040, 13, 7, 1152 },
		{ 52100, 13, 7, 1088 },
		{ 52160, 13, 7, 1024 },
		{ 52488, 13, 7, 960 },
		{ 52672, 13, 7, 896 },
		{ 53404, 13, 7, 832 },
		{ 53464, 13, 7, 768 },
		{ 53524, 13, 7, 704 },
		{ 53584, 13, 7, 640 },
		{ 53686, 13, 7, 576 },
		{ 54561, 13, 8, 576 },      // reactor destroyed
		{ 56742, 13, 9, 576 },      // escape done: the ending
	},
	"the whole game, through two continues to the ending",
};

// native: game RAM hashes the interpreter reached at the checkpoints (filled by its run)
std::vector<state> replay(const std::string &dir, const recorded_run &run, const input_replay::recording &rec, bool native,
	std::vector<uint64_t> &game_hashes)
{
	model1 m(load_regions(dir, ID));
	if (native) {
		m.enable_sound();
		rec::hints map;
		assert(map.read("assets/games/swa/swa_map.txt"));
		rec::attach(m.cpu(), "swa");
		assert(rec::build_native(m.program_rom(), map));
		m.run_cpu = [](int n) { rec::run(n); };
	}
	m.reset();
	if (!native) m.run_cpu = [&](int c) { m.cpu().run(c); };
	m.set_drawing(false);
	set_lod(m, !native);
	const auto lod_state = std::static_pointer_cast<game::swa::gameplay_state>(m.game_state());
	const char *mode = native ? "native" : "interpreter";
	std::vector<state> changes;
	state last{};
	size_t next_checkpoint = 0;
	for (size_t f = 0; f < rec.frames.size(); f++) {
		const auto error = input_replay::apply(m, rec.frames[f]);
		if (!error.empty()) { fprintf(stderr, "frame %zu: %s\n", f, error.c_str()); assert(false); }
		m.run_frame();
		m.m_audio_out.clear();
		const state now{ f, m.read8(SEQUENCE), m.read8(SUBSEQUENCE), m.read16(SHIELD) };
		if (f >= TRACE_FROM && (now.seq != last.seq || now.sub != last.sub || now.shield != last.shield)) changes.push_back(now);
		last = now;
		if (next_checkpoint < run.checkpoints.size() && f + 1 == run.checkpoints[next_checkpoint].frames) {
			const auto &c = run.checkpoints[next_checkpoint++];
			const uint64_t game = game_ram_hash(m);
			if (!native) {
				const uint64_t h = input_replay::ram_hash(m);
				if (h != c.hash) {
					fprintf(stderr, "SWA recording (%s): %s after %zu frames: RAM hash %016llx, expected %016llx\n", mode, c.what, c.frames,
						(unsigned long long)h, (unsigned long long)c.hash);
					assert(false);
				}
				game_hashes.push_back(game);
			} else if (game != game_hashes[next_checkpoint - 1]) {
				fprintf(stderr, "SWA recording (%s): %s after %zu frames: game RAM differs from the interpreter's\n", mode, c.what, c.frames);
				assert(false);
			}
			printf("SWA recording (%s): %s at %zu frames matches\n", mode, c.what, c.frames);
		}
	}
	assert(next_checkpoint == run.checkpoints.size());
	if (native) {
		assert(lod_state->nearest > 0 && lod_state->destroyers_near > 0);
		printf("SWA recording (native, LOD off): %llu detail results replaced by the nearest model's, %llu Star Destroyers drawn near\n",
			(unsigned long long)lod_state->nearest, (unsigned long long)lod_state->destroyers_near);
	} else assert(lod_state->nearest == 0 && lod_state->destroyers_near == 0);
	return changes;
}

void check(const recorded_run &run, const std::vector<state> &changes, const char *mode)
{
	if (changes == run.expected) return;
	fprintf(stderr, "SWA recording (%s): %s: game-state changes differ\n", mode, run.path);
	for (const auto &s : changes) fprintf(stderr, "  { %zu, %u, %u, %u },\n", s.frame, s.seq, s.sub, s.shield);
	assert(false);
}

bool verify(const char *dir, const recorded_run &run)
{
	input_replay::recording rec;
	if (const auto error = input_replay::load(run.path, ID, rec); !error.empty()) { fprintf(stderr, "%s\n", error.c_str()); return false; }
	assert(rec.frames.size() == run.checkpoints.back().frames && rec.expected_ram == run.checkpoints.back().hash);
	std::vector<uint64_t> game_hashes;
	check(run, replay(dir, run, rec, false, game_hashes), "interpreter");
	check(run, replay(dir, run, rec, true, game_hashes), "native");
	if (rec::fallback_count()) {
		fprintf(stderr, "SWA recording: native code fell back to the interpreter %zu times, first at %06x (regenerate assets/games/swa/swa_map.txt)\n",
			rec::fallback_count(), rec::first_fallback_address());
		return false;
	}
	printf("SWA recording: %s reproduce on interpreter and native\n", run.summary);
	return true;
}

}

int main(int argc, char **argv)
{
	if (argc < 2) { fprintf(stderr, "usage: %s <swa regions dir>\n", argv[0]); return 2; }
	for (const auto *run : { &TRENCH_TO_CONTINUE, &COMPLETE_GAME })
		if (!verify(argv[1], *run)) return 1;
	return 0;
}
