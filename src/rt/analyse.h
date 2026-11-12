// Finds the V60 program's code and splits it into functions: the first step of the recompile,
// done by tools/recomp/v60recomp (for the C++ build) and by the game itself at start-up (for the
// native code it makes on the player's machine).
//
// From the ROM alone only the code reached by direct calls and branches is found; VF reaches most
// of its code through jump tables and pointers. The hints fill that in: where those indirect jumps
// and calls lead (addresses only, recorded from test runs), plus the interrupt entries.
#pragma once
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace rec {

struct code_range
{
	uint32_t first, last;
};

enum class insn_kind { normal, branch, jump, call, ret, bad };

struct insn
{
	uint32_t addr = 0, len = 0;
	uint8_t op = 0;
	insn_kind kind = insn_kind::normal;
	std::string text;                  // the disassembly (kept only when asked for)
	std::vector<uint32_t> targets;     // static targets
	std::vector<uint32_t> hinted;      // where an indirect jump went in the test runs
	bool indirect = false;             // the target is only known at run time
};

struct function
{
	uint32_t entry = 0;
	std::vector<uint32_t> body;        // instruction addresses: the entry first, then in address order
};

struct hints
{
	std::set<uint32_t> entries;                     // reached at run time
	std::map<uint32_t, std::set<uint32_t>> edges;   // indirect jump/call at a -> where it went
	std::set<uint32_t> covered;                     // instructions seen running (optional)
	std::vector<code_range> ram;                   // mutable executable ranges, exclusive end

	// The hint file: "entry A", "edge A B", "irq L A", "pc A N", and "ram A B" (hex); others ignored.
	bool read(const std::string &path);
};

struct program
{
	std::map<uint32_t, insn> insns;
	std::vector<function> functions;
	std::map<uint32_t, std::set<uint32_t>> edges;   // the hints' real indirect jumps and calls
	std::vector<code_range> ram;
};

// mem: the main CPU's address space as the recompiler sees it (16 MB: 24-bit addresses).
// Empty ranges use the ROM windows; explicit ranges restrict discovery to those windows.
program analyse(const std::vector<uint8_t> &mem, const hints &h, bool keep_text = false,
	const std::vector<code_range> &ranges = {});

// Writes the listing tools/recomp/emit.py reads.
void write_listing(const program &p, const char *path);

// Writes the address map: the smallest hints that find the same program (every function's entry
// and every indirect jump and call's targets). Addresses only, no bytes from the ROM.
void write_map(const program &p, const char *path);

} // namespace rec
