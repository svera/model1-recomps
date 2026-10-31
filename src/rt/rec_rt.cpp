// Runtime for the recompiled code: cycle counting and interrupts exactly as the interpreter's
// run loop does them (8 cycles an instruction, interrupts taken between instructions), the
// coroutine that hands control back at the end of each slice, and a fallback to the
// interpreter for code the recompiler didn't reach.
#include "rec_rt.h"
#include "coro.h"

#define PC (*rec::pc)
#define TICK() do { if ((rec::cycles -= 8) <= 0 || rec::cpu->irq_line_up()) rec::after_insn_slow(); } while (0)
#include <algorithm>
#include <set>
#include <string>

namespace rec {

v60_device *cpu;
uint32_t *pc;
int cycles;

struct context::state
{
	v60_device *cpu = nullptr;
	int cycles = 0;
	coro_ctx host_ctx, game_ctx;
	std::vector<uint8_t> game_stack;
	std::string entries_path;
	std::set<uint32_t> logged;
	std::set<std::pair<uint32_t, uint32_t>> seen;
	const rec_entry *table = rec_table;
	size_t table_size = rec_table_size;
	std::shared_ptr<const void> code;
	size_t fallbacks = 0;
	uint32_t first_fallback = 0xffffffff;
};

static context::state default_state;
static context::state *active = &default_state;

static void select(context::state &next)
{
	if (active == &next) return;
	active->cycles = cycles;
	if (cpu) cpu->m_cycles = &active->cycles;
	active = &next;
	cpu = next.cpu;
	pc = cpu ? &cpu->pc_ref() : nullptr;
	cycles = next.cycles;
	if (cpu) cpu->m_cycles = &cycles;
}

context::context() : m(std::make_unique<state>()) {}
context::~context()
{
	if (m->cpu) m->cpu->own_cycles();
	if (active == m.get()) {
		cpu = nullptr;
		m->cpu = nullptr;
		select(default_state);
	}
}
void context::activate() { select(*m); }
void context::attach(v60_device &c, std::string_view game) { activate(); rec::attach(c, game); }
void context::run(int n) { activate(); rec::run(n); }

static void yield() { coro_switch(&active->game_ctx, &active->host_ctx); }

void after_insn_slow()
{
	// Interpreter order: after an instruction, take a pending interrupt; the slice ends when the
	// cycles run out; the next slice starts by checking for an interrupt again.
	bool entered = cpu->irq_line_up() && cpu->service_irq();
	while (cycles <= 0) {
		yield();
		if (!entered && cpu->irq_line_up())
			entered = cpu->service_irq();
	}
	if (entered) {
		uint32_t resume = 0;
		(void)resume;
		call(PC);   // the handler runs until its RETIS, which restores PC
	}
}

static bool is_call_op(uint8_t op) { return op == 0x48 || op == 0x49 || op == 0xe8 || op == 0xe9 || op == 0xf8 || op == 0xf9 || op == 0xc9; }
static bool is_ret_op(uint8_t op) { return op == 0xca || op == 0xe2 || op == 0xe3 || op == 0xfa || op == 0xfb || op == 0xea || op == 0xeb; }

static void interpret_until_return(uint32_t addr)
{
	if (active->logged.insert(addr).second) {
		// remembered for the next recompile (the Makefile hands v60recomp build/cov/<game>/)
		const char *path = getenv("REC_RUNTIME_ENTRIES");
		if (!path) path = active->entries_path.c_str();
		if (FILE *f = fopen(path, "a")) {
			fprintf(f, "entry %06x\n", addr);
			fclose(f);
		}
		if (getenv("REC_VERBOSE"))
			fprintf(stderr, "rec: interpreting %06x (not recompiled yet; noted for the next build)\n", addr);
	}
	int depth = 0;
	for (;;) {
		uint8_t op = cpu->read_op8(PC);
		uint32_t n = cpu->exec_handler();
		if (n) PC += n;
		TICK();
		if (!n) {
			if (is_call_op(op)) depth++;
			else if (is_ret_op(op) && depth-- == 0) return;
		}
	}
}

void use_table(const rec_entry *t, size_t n, std::shared_ptr<const void> owner)
{
	active->table = t;
	active->table_size = n;
	active->code = std::move(owner);
}

size_t fallback_count()
{
	return active->fallbacks;
}

uint32_t first_fallback_address()
{
	return active->first_fallback;
}

void reset_fallbacks()
{
	active->fallbacks = 0;
	active->first_fallback = 0xffffffff;
}

void call(uint32_t addr)
{
	const rec_entry *table = active->table;
	const rec_entry *e = std::lower_bound(table, table + active->table_size, addr,
		[](const rec_entry &a, uint32_t v) { return a.addr < v; });
	if (e != table + active->table_size && e->addr == addr)
		e->fn(*cpu);
	else {
		if (!active->fallbacks) active->first_fallback = addr;
		active->fallbacks++;
		interpret_until_return(addr);
	}
}

void leave(uint32_t fn, uint32_t to)
{
	if (getenv("REC_LOG_LEAVE") && active->seen.insert({fn, to}).second)
		fprintf(stderr, "rec: f_%06x jumps out to %06x\n", fn, to);
}

void bad(uint32_t addr)
{
	fprintf(stderr, "rec: reached undecodable code at %06x\n", addr);
	abort();
}

void len_mismatch(uint32_t addr, uint32_t n, uint32_t want)
{
	fprintf(stderr, "rec: instruction at %06x had length %u, the recompiler expected %u\n", addr, n, want);
	abort();
}

static void game_main(void *)
{
	for (;;)
		call(PC);
}

void attach(v60_device &c, std::string_view game)
{
	if (cpu && cpu != &c) cpu->own_cycles();
	active->entries_path = "build/cov/" + std::string(game) + "/runtime_entries.txt";
	active->cpu = &c;
	cpu = &c;
	pc = &c.pc_ref();
	cycles = 0;
	c.m_cycles = &cycles;
	reset_fallbacks();
	active->game_stack.assign(256 << 20, 0);   // deep: tail jumps between functions nest C frames
	coro_init(&active->game_ctx, active->game_stack.data(), active->game_stack.size(), game_main, nullptr);
}

void detach(v60_device &c)
{
	c.own_cycles();
	std::vector<uint8_t>().swap(active->game_stack);
	active->cpu = nullptr;
	cpu = nullptr;
	pc = nullptr;
}

void run(int n)
{
	cycles += n;   // the last slice's overrun carries, as on MAME's scheduler
	coro_switch(&active->host_ctx, &active->game_ctx);
}

} // namespace rec
