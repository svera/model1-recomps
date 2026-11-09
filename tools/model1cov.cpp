// Coverage: runs a Model 1 title on the interpreter with its scripted play (src/games/script.h) and
// records every executed instruction address, plus the call targets and indirect jumps seen.
// Output stays in build/.
//   model1cov [--game vf|vr] [--scenario NAME] <regions dir> <frames> <out.txt>   (M1_SEED: VF's moves)
#include "../src/host/regions.h"
#include "../src/games/script.h"
#include "../src/host/input_replay.h"
#include "tool_args.h"
#include <map>
#include <set>

int main(int argc, char **argv)
{
	tool_args opt;
	if (!opt.take(argc, argv)) return 2;
	std::string replay_path;
	int positional = 1;
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--replay-inputs")) {
			if (i + 1 >= argc) { fprintf(stderr, "--replay-inputs needs a file\n"); return 2; }
			replay_path = argv[++i];
		} else argv[positional++] = argv[i];
	}
	argc = positional;
	const std::string dir = argc > 1 ? argv[1] : opt.default_dir();
	const game::id title = opt.game_for(dir);
	int frames = argc > 2 ? atoi(argv[2]) : 20000;
	const std::string out = argc > 3 ? argv[3] : "build/" + std::string(title.name) + "_cov.txt";
	const uint32_t seed = getenv("M1_SEED") ? uint32_t(atoi(getenv("M1_SEED"))) : 12345;
	input_replay::recording replay;
	if (!replay_path.empty()) {
		if (!opt.scenario.empty()) { fprintf(stderr, "Input replay cannot be combined with --scenario\n"); return 2; }
		if (const auto error = input_replay::load(replay_path, title, replay); !error.empty()) {
			fprintf(stderr, "%s\n", error.c_str()); return 2;
		}
		if (argc > 2 && frames != int(replay.frames.size())) {
			fprintf(stderr, "Replay coverage must run all %zu recorded frames\n", replay.frames.size()); return 2;
		}
		frames = int(replay.frames.size());
	}
	auto play = replay_path.empty() ? game::make_auto_play(title, opt.scenario, seed, false) : nullptr;
	if (replay_path.empty() && !play) { fprintf(stderr, "%s has no scenario \"%s\"\n", game::get(title).display_name.data(), opt.scenario.c_str()); return 2; }
	model1 m(load_regions(dir, title));
	input_replay::start(m, replay);
	m.reset();
	std::map<uint32_t, uint64_t> pcs;
	std::map<uint8_t, uint64_t> ops;
	std::set<std::pair<uint32_t, uint32_t>> jumps;   // (instruction, target) when it set PC
	std::set<std::pair<uint32_t, uint32_t>> irqs;    // (vector, handler)
	uint32_t prev = 0xffffffff;
	bool irq_between = false;
	m.cpu().on_irq = [&](int v, uint32_t, uint32_t to) { irqs.insert({uint32_t(v), to}); irq_between = true; };
	m.on_instruction = [&](uint32_t pc) {
		if (prev != 0xffffffff && !irq_between) {
			// sequential if pc follows prev; the interpreter told us nothing about length, so
			// record every change of flow and let the recompiler drop the sequential ones
			jumps.insert({prev, pc});
		}
		irq_between = false;
		prev = pc;
		pcs[pc]++;
		ops[m.read8(pc)]++;
	};
	for (int f = 1; f <= frames; f++) {
		const game::script_inputs in = play ? play->frame(f, m) : replay.frames[f - 1];
		for (size_t c = 0; c < in.analog.size(); c++)
			if (in.analog[c] >= 0 && !m.set_analog_input(c, uint8_t(in.analog[c]))) {
				fprintf(stderr, "%s coverage requires the Model 1 I/O board ROM (model1io.zip)\n", game::get(title).display_name.data());
				return 1;
			}
		for (int p = 0; p < 3; p++) m.m_inputs[p] = uint8_t(~in.pressed[p]);
		m.run_frame();
	}
	if (!replay_path.empty() && input_replay::ram_hash(m) != replay.expected_ram) {
		fprintf(stderr, "Input replay RAM hash mismatch; no coverage file written\n"); return 1;
	}
	printf("TGP calls:");
	for (auto &[fn, n] : m.m_tgp_calls) printf(" %02x:%llu", fn, (unsigned long long)n);
	printf("\nTGP missing:");
	for (auto &[fn, n] : m.m_tgp_missing) printf(" %02x:%llu", fn, (unsigned long long)n);
	printf("\nTGP result underflows:");
	for (auto &[pc, n] : m.m_tgp_underflow) printf(" %06x:%llu", pc, (unsigned long long)n);
	printf("\n");
	FILE *f = fopen(out.c_str(), "w");
	if (!f) { fprintf(stderr, "Cannot create coverage file %s\n", out.c_str()); return 1; }
	for (const auto &r : game::get(title).recompiler_ram)
		fprintf(f, "ram %06x %06x\n", r[0], r[1]);
	for (auto &[pc, n] : pcs) fprintf(f, "pc %06x %llu\n", pc, (unsigned long long)n);
	for (auto &[a, b] : jumps) fprintf(f, "edge %06x %06x\n", a, b);
	for (auto &[v, h] : irqs) fprintf(f, "irq %x %06x\n", v, h);
	const bool failed = ferror(f);
	if (fclose(f) || failed) { fprintf(stderr, "Cannot write coverage file %s\n", out.c_str()); return 1; }
	printf("%zu unique instruction addresses\nopcodes used: %zu\n", pcs.size(), ops.size());
	std::multimap<uint64_t, uint8_t> byn;
	for (auto &[o, n] : ops) byn.insert({n, o});
	int k = 0;
	for (auto it = byn.rbegin(); it != byn.rend() && k < 300; ++it, ++k)
		printf("%02x:%llu%s", it->second, (unsigned long long)it->first, (k % 8) == 7 ? "\n" : "  ");
	printf("\n");
}
