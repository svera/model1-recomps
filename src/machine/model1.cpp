// Sega Model 1 glue: memory map, IRQ controller, timers, ROM banking and the per-frame loop.
// Follows MAME's src/mame/sega/model1.cpp (BSD-3-Clause, copyright-holders: Olivier Galibert).
#include <algorithm>
#include <chrono>
#include <ctime>
#include <cstring>
#include <optional>
#include <unordered_map>
#include "model1.h"

namespace {
constexpr int CPU_CLOCK = 16000000;          // 32 MHz XTAL / 2
constexpr int H_TOTAL = 656, V_TOTAL = 424;  // 16 MHz pixel clock: 57.52 Hz
constexpr int VBLANK_LINE = 384;
constexpr int CYCLES_PER_LINE = H_TOTAL;     // CPU and pixel clocks are both 16 MHz
}

model1::model1(regions &&r)
	: m_regions(std::move(r))
	, m_maincpu(*this, 0xfffffff0)
	, m_ram_a(0x10000, 0xff)                 // MAME: NVRAM default all 1s (VF's rankings need it)
	, m_ram_b(0x40000, 0)
	, m_display_list0_ram(0x8000), m_display_list1_ram(0x8000)
	, m_paletteram16_ram(0x2000), m_color_xlat_ram(0x6000)
{
	m_video = &game::get_video(m_regions.title);
	m_profile = &game::get(m_regions.title);
	m_display_list0 = m_display_list0_ram.data();
	m_display_list1 = m_display_list1_ram.data();
	m_paletteram16 = m_paletteram16_ram.data();
	m_color_xlat = m_color_xlat_ram.data();
	m_analog_inputs = game::get_io_profile(m_regions.title).analog_neutral;
	m_poly_rom = reinterpret_cast<uint32_t *>(m_regions.polygons.data());
	m_copro_tables = reinterpret_cast<uint32_t *>(m_regions.copro_tables.data());
	m_copro_ram_data = std::make_unique<u32[]>(0x8000);
	m_maincpu.irq_vector = [this] { return irq_callback(); };
	m_gameplay = game::make_gameplay(m_regions.title, *this);
	auto tgp_hle = game::make_tgp_hle(m_regions.title, *this);
	m_tgp_hle_reset = std::move(tgp_hle.reset);
	m_copro_fifo_in_q.m_on_push = std::move(tgp_hle.fifo_push);
	m_bitmap.allocate(SCREEN_W, SCREEN_H);
	video_start();
	if (!m_regions.tgp_program.empty() && !getenv("M1_TGP_SIM"))
		use_tgp_program(m_regions.tgp_program);
	if (!m_regions.ioboard2.empty() && !getenv("M1_IO_STANDIN"))
		m_ioboard2 = std::make_unique<model1io2>(m_regions.ioboard2, m_dpram);
	else if (!m_regions.ioboard.empty() && !getenv("M1_IO_STANDIN"))
		m_ioboard = std::make_unique<model1io>(m_regions.ioboard, m_dpram);
}

void model1::reset()
{
	m_scanline = 0;
	if (m_comm) m_comm->reset();
	m_tgp_cycles = 0;
	m_cpu_owed = 0;
	for (auto &c : m_layer_cache) c.frame = ~0ull;   // a new game: nothing drawn is current
	for (auto &history : m_tile_slides) history.clear();
	m_shown = m_unshown = false;                      // nor shown
	m_tiles_updated = ~0ull;
	m_list_written[0] = m_list_written[1] = ~0ull;
	m_list_extra[0].clear();
	m_list_extra[1].clear();
	m_logic[0] = m_logic[1] = m_logic_prev = logic_frame{};
	for (int buffer = 0; buffer < 2; buffer++) {
		m_list_owner[buffer].clear();
		m_list_rear_target[buffer].reset();
		m_list_has_camera[buffer] = false;
		m_list_visibility[buffer].clear();
		m_list_visibility_frame[buffer] = ~0ull;
	}
	m_still_carried = -1;
	m_cam_move_next = 0;
	for (auto &move : m_cam_move) move = {};
	if (m_gameplay.reset) m_gameplay.reset();
	m_bank_base = 0x1000000;
	m_maincpu.set_irq_line(CLEAR_LINE);
	copro_reset();
	if (m_tgp_hle_reset) m_tgp_hle_reset();
	m_acc = 0;
	m_mat_stack_pos = 0;
	memset(m_cmat, 0, sizeof(m_cmat));
	m_cmat[0] = m_cmat[4] = m_cmat[8] = 1.0f;
	m_copro_fifo_in_q.clear();
	m_copro_fifo_out_q.clear();
	lle_reset();
	m_irq_status = 0;
	m_last_irq = 0;
	m_irq_mask = 0xff;
	m_uart_txbuf = m_uart_shift = m_uart_rx = -1;
	m_uart_shift_left = m_uart_rx_left = 0;
	if (m_audio) m_audio->reset();
	if (m_ioboard) {
		std::fill(std::begin(m_dpram), std::end(m_dpram), 0);
		m_ioboard->reset();
	}
	if (m_ioboard2) {
		std::fill(std::begin(m_dpram), std::end(m_dpram), 0);
		m_ioboard2->reset();
	}
	m_maincpu.reset();
}

bool model1::save_simulation_state(simulation_state &saved) const
{
	if (m_comm || m_ioboard2) return false;
	saved.cpu = m_maincpu.save_state();
	saved.frame = m_frame;
	saved.scanline = m_scanline;
	saved.tgp_cycles = m_tgp_cycles;
	saved.cpu_owed = m_cpu_owed;
	saved.ram_a = m_ram_a;
	saved.ram_b = m_ram_b;
	saved.display_list0 = m_display_list0_ram;
	saved.display_list1 = m_display_list1_ram;
	saved.palette = m_paletteram16_ram;
	saved.colour_xlat = m_color_xlat_ram;
	saved.tile_memory = m_tiles.save_memory();
	std::copy(std::begin(m_pens), std::end(m_pens), saved.pens.begin());
	std::copy(std::begin(m_dpram), std::end(m_dpram), saved.dpram.begin());
	std::copy(std::begin(m_inputs), std::end(m_inputs), saved.inputs.begin());
	std::copy(m_frame_inputs.begin(), m_frame_inputs.end(), saved.frame_inputs.begin());
	saved.analog_inputs = m_analog_inputs;
	saved.lamps = m_lamps;
	saved.bank_base = m_bank_base;
	std::copy(std::begin(m_listctl), std::end(m_listctl), saved.listctl.begin());
	saved.uart_txbuf = m_uart_txbuf;
	saved.uart_shift = m_uart_shift;
	saved.uart_rx = m_uart_rx;
	saved.uart_shift_left = m_uart_shift_left;
	saved.uart_rx_left = m_uart_rx_left;
	saved.io_command = m_io_command;
	saved.io_ack = m_io_ack;
	std::copy(std::begin(m_timer_period), std::end(m_timer_period), saved.timer_period.begin());
	std::copy(std::begin(m_timer_val), std::end(m_timer_val), saved.timer_val.begin());
	std::copy(std::begin(m_timer_remain), std::end(m_timer_remain), saved.timer_remain.begin());
	saved.timer_mode = m_timer_mode;
	saved.irq_status = m_irq_status;
	saved.irq_mask = m_irq_mask;
	saved.last_irq = m_last_irq;
	saved.copro_bases = {m_copro_sincos_base, m_copro_inv_base, m_copro_isqrt_base};
	std::copy(std::begin(m_copro_atan_base), std::end(m_copro_atan_base), saved.copro_atan_base.begin());
	saved.copro_data_base = m_copro_data_base;
	saved.copro_ram_adr = m_copro_ram_adr;
	saved.copro_fifo_in = m_copro_fifo_in_q.save_state();
	saved.copro_fifo_out = m_copro_fifo_out_q.save_state();
	saved.v60_copro_fifo_r = m_v60_copro_fifo_r;
	saved.v60_copro_fifo_w = m_v60_copro_fifo_w;
	saved.pushpc = m_pushpc;
	std::copy(std::begin(m_cmat), std::end(m_cmat), saved.cmat.begin());
	std::memcpy(saved.mat_stack.data(), m_mat_stack, sizeof m_mat_stack);
	std::memcpy(saved.mat_vector.data(), m_mat_vector, sizeof m_mat_vector);
	saved.mat_stack_pos = m_mat_stack_pos;
	saved.acc = m_acc;
	saved.tgp_int_px = m_tgp_int_px;
	saved.tgp_int_py = m_tgp_int_py;
	saved.tgp_int_pz = m_tgp_int_pz;
	saved.tgp_int_adr = m_tgp_int_adr;
	saved.v60_copro_ram_adr = m_v60_copro_ram_adr;
	std::copy(std::begin(m_v60_copro_ram_latch), std::end(m_v60_copro_ram_latch),
		std::begin(saved.v60_copro_ram_latch));
	saved.copro_hle_ram_scan_adr = m_copro_hle_ram_scan_adr;
	saved.ccount = m_ccount;
	saved.copro_ram.assign(m_copro_ram_data.get(), m_copro_ram_data.get() + 0x8000);
	saved.tgp_ram.assign(m_tgp_ram.get(), m_tgp_ram.get() + 0xc0000);
	saved.poly_ram.assign(m_poly_ram.get(), m_poly_ram.get() + 0x400000);
	saved.lle = save_lle_state();
	if (m_ioboard) saved.io_board = m_ioboard->save_state();
	else saved.io_board.reset();
	return true;
}

bool model1::load_simulation_state(const simulation_state &saved)
{
	if (m_comm || m_ioboard2 || bool(m_lle) != bool(saved.lle) ||
		bool(m_ioboard) != bool(saved.io_board) ||
		saved.ram_a.size() != m_ram_a.size() || saved.ram_b.size() != m_ram_b.size() ||
		saved.display_list0.size() != m_display_list0_ram.size() ||
		saved.display_list1.size() != m_display_list1_ram.size() ||
		saved.palette.size() != m_paletteram16_ram.size() ||
		saved.colour_xlat.size() != m_color_xlat_ram.size() ||
		saved.tile_memory.chars.size() != 0x40000 || saved.tile_memory.tiles.size() != 0x8000 ||
		saved.tgp_ram.size() != 0xc0000 || saved.poly_ram.size() != 0x400000 ||
		saved.copro_ram.size() != 0x8000) return false;

	std::copy(saved.ram_a.begin(), saved.ram_a.end(), m_ram_a.begin());
	std::copy(saved.ram_b.begin(), saved.ram_b.end(), m_ram_b.begin());
	std::copy(saved.display_list0.begin(), saved.display_list0.end(), m_display_list0_ram.begin());
	std::copy(saved.display_list1.begin(), saved.display_list1.end(), m_display_list1_ram.begin());
	std::copy(saved.palette.begin(), saved.palette.end(), m_paletteram16_ram.begin());
	std::copy(saved.colour_xlat.begin(), saved.colour_xlat.end(), m_color_xlat_ram.begin());
	if (!m_tiles.restore_memory(saved.tile_memory)) return false;
	std::copy(saved.pens.begin(), saved.pens.end(), std::begin(m_pens));
	std::copy(saved.dpram.begin(), saved.dpram.end(), std::begin(m_dpram));
	std::copy(saved.inputs.begin(), saved.inputs.end(), std::begin(m_inputs));
	std::copy(saved.frame_inputs.begin(), saved.frame_inputs.end(), m_frame_inputs.begin());
	m_analog_inputs = saved.analog_inputs;
	m_lamps = saved.lamps;
	m_frame = saved.frame;
	m_scanline = saved.scanline;
	m_tgp_cycles = saved.tgp_cycles;
	m_cpu_owed = saved.cpu_owed;
	m_maincpu.load_state(saved.cpu);
	m_bank_base = saved.bank_base;
	std::copy(saved.listctl.begin(), saved.listctl.end(), std::begin(m_listctl));
	m_display_list_current = m_listctl[0] & 0x40 ? m_display_list1 : m_display_list0;
	m_uart_txbuf = saved.uart_txbuf;
	m_uart_shift = saved.uart_shift;
	m_uart_rx = saved.uart_rx;
	m_uart_shift_left = saved.uart_shift_left;
	m_uart_rx_left = saved.uart_rx_left;
	m_io_command = saved.io_command;
	m_io_ack = saved.io_ack;
	std::copy(saved.timer_period.begin(), saved.timer_period.end(), std::begin(m_timer_period));
	std::copy(saved.timer_val.begin(), saved.timer_val.end(), std::begin(m_timer_val));
	std::copy(saved.timer_remain.begin(), saved.timer_remain.end(), std::begin(m_timer_remain));
	m_timer_mode = saved.timer_mode;
	m_irq_status = saved.irq_status;
	m_irq_mask = saved.irq_mask;
	m_last_irq = saved.last_irq;
	m_copro_sincos_base = saved.copro_bases[0];
	m_copro_inv_base = saved.copro_bases[1];
	m_copro_isqrt_base = saved.copro_bases[2];
	m_copro_data_base = saved.copro_data_base;
	std::copy(saved.copro_atan_base.begin(), saved.copro_atan_base.end(), std::begin(m_copro_atan_base));
	m_copro_ram_adr = saved.copro_ram_adr;
	m_copro_fifo_in_q.load_state(saved.copro_fifo_in);
	m_copro_fifo_out_q.load_state(saved.copro_fifo_out);
	m_v60_copro_fifo_r = saved.v60_copro_fifo_r;
	m_v60_copro_fifo_w = saved.v60_copro_fifo_w;
	m_pushpc = saved.pushpc;
	std::copy(saved.cmat.begin(), saved.cmat.end(), std::begin(m_cmat));
	std::memcpy(m_mat_stack, saved.mat_stack.data(), sizeof m_mat_stack);
	std::memcpy(m_mat_vector, saved.mat_vector.data(), sizeof m_mat_vector);
	m_mat_stack_pos = saved.mat_stack_pos;
	m_acc = saved.acc;
	m_tgp_int_px = saved.tgp_int_px;
	m_tgp_int_py = saved.tgp_int_py;
	m_tgp_int_pz = saved.tgp_int_pz;
	m_tgp_int_adr = saved.tgp_int_adr;
	m_v60_copro_ram_adr = saved.v60_copro_ram_adr;
	std::copy(std::begin(saved.v60_copro_ram_latch), std::end(saved.v60_copro_ram_latch),
		std::begin(m_v60_copro_ram_latch));
	m_copro_hle_ram_scan_adr = saved.copro_hle_ram_scan_adr;
	m_ccount = saved.ccount;
	std::copy(saved.copro_ram.begin(), saved.copro_ram.end(), m_copro_ram_data.get());
	std::copy(saved.tgp_ram.begin(), saved.tgp_ram.end(), m_tgp_ram.get());
	std::copy(saved.poly_ram.begin(), saved.poly_ram.end(), m_poly_ram.get());
	if (!restore_lle_state(saved.lle)) return false;
	if (m_ioboard) m_ioboard->load_state(*saved.io_board);
	if (m_audio) m_audio->reset();
	m_audio_out.clear();

	m_shown = m_unshown = false;
	m_render_done = false;
	m_glist = nullptr;
	m_picture_buffer = 0;
	m_tiles_updated = ~0ull;
	m_list_written[0] = m_list_written[1] = ~0ull;
	m_list_extra[0].clear();
	m_list_extra[1].clear();
	for (int buffer = 0; buffer < 2; buffer++) {
		m_list_owner[buffer].clear();
		m_list_rear_target[buffer].reset();
		m_list_has_camera[buffer] = false;
		m_list_visibility[buffer].clear();
		m_list_visibility_frame[buffer] = ~0ull;
	}
	for (auto &cache : m_layer_cache) cache.frame = ~0ull;
	smooth_cut();
	return true;
}

void model1::copro_reset()
{
	m_v60_copro_ram_adr = 0;
	m_copro_ram_adr = 0;
	m_copro_sincos_base = 0;
	m_copro_inv_base = 0;
	m_copro_isqrt_base = 0;
	std::fill(std::begin(m_copro_atan_base), std::end(m_copro_atan_base), 0);
	std::fill(std::begin(m_v60_copro_ram_latch), std::end(m_v60_copro_ram_latch), 0);
	memset(m_copro_ram_data.get(), 0, 0x8000 * 4);
}

// ---- frame loop ----

#if defined(__ANDROID__) && defined(M1_ALWAYS_FRAME_TIMINGS)
int64_t model1::simulation_sample::now()
{
	timespec ts{};
	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
		if (valid) perror("simulation phase clock");
		valid = false;
	}
	return int64_t(ts.tv_sec) * 1000000000 + ts.tv_nsec;
}
#endif

void model1::run_frame()
{
#if defined(__ANDROID__) && defined(M1_ALWAYS_FRAME_TIMINGS)
	const bool trace_frame = m_regions.title.name == "vr" && ATrace_isEnabled();
	const bool trace_simulation = trace_frame && trace_simulation_phases;
	simulation_sample sample;
	const uint64_t frame = m_frame, tgp_cycles = m_tgp_cycles;
	m_simulation_sample = trace_simulation ? &sample : nullptr;
#endif
	do { run_scanline(); } while (m_scanline);
#if defined(__ANDROID__) && defined(M1_ALWAYS_FRAME_TIMINGS)
	m_simulation_sample = nullptr;
	if (trace_simulation && sample.valid) {
		char name[128];
		snprintf(name, sizeof name, "M1 sim wall ns f=%llu v60=%lld tgp=%lld io=%lld snd=%lld calls=%d cycles=%llu",
			(unsigned long long)frame, (long long)(sample.v60 - sample.tgp), (long long)sample.tgp,
			(long long)sample.io, (long long)sample.audio, sample.tgp_calls,
			(unsigned long long)(m_tgp_cycles - tgp_cycles));
		android_frame_trace trace(name);
	} else if (trace_frame && !trace_simulation) {
		char name[48];
		snprintf(name, sizeof name, "M1 sim frame=%llu", (unsigned long long)frame);
		android_frame_trace trace(name);
	}
#endif
}

void model1::run_scanline()
{
	if (!m_scanline) {
		std::copy_n(m_inputs, 3, m_frame_inputs.begin());
		if (m_gameplay.frame_begin) m_gameplay.frame_begin();
		// the picture on screen was drawn from the state this frame is about to change: keep that
		if (m_smooth && m_shown && m_shown_frame == m_frame) {
			android_frame_trace trace("M1 keep still");
			keep_still();
		}
	}
	const int line = m_scanline;
	if (line == VBLANK_LINE) {
		if (m_comm && !m_comm_external_clock) m_comm->tick();
		if (!BIT(m_irq_mask, 1))
			irq_raise(1);
		// vblank starts: the display lists are scanned and swapped
		android_frame_trace trace("M1 simulation vblank");
		screen_vblank_model1(1);
	}
	m_cpu_owed += CYCLES_PER_LINE * 8.0 / m_profile->cpu_cycles_per_instruction;
	const int cpu_cycles = int(m_cpu_owed);
	m_cpu_owed -= cpu_cycles;
#if defined(__ANDROID__) && defined(M1_ALWAYS_FRAME_TIMINGS)
	auto *sample = m_simulation_sample;
	const int64_t begin_cpu = sample ? sample->now() : 0;
#endif
	if (run_cpu)
		run_cpu(cpu_cycles);
	else {
		m_maincpu.on_exec = on_instruction;
		m_maincpu.run(cpu_cycles);
	}
#if defined(__ANDROID__) && defined(M1_ALWAYS_FRAME_TIMINGS)
	const int64_t begin_io = sample ? sample->now() : 0;
	if (sample) sample->v60 += begin_io - begin_cpu;
#endif
	timer_tick(CYCLES_PER_LINE);
	io_board_tick(CYCLES_PER_LINE);
	uart_tick(CYCLES_PER_LINE);
#if defined(__ANDROID__) && defined(M1_ALWAYS_FRAME_TIMINGS)
	const int64_t end_io = sample ? sample->now() : 0;
	if (sample) sample->io += end_io - begin_io;
#endif
	if (m_audio)
		m_audio->run(CYCLES_PER_LINE * 1e6 / CPU_CLOCK, m_audio_out);
#if defined(__ANDROID__) && defined(M1_ALWAYS_FRAME_TIMINGS)
	if (sample && m_audio) sample->audio += sample->now() - end_io;
#endif
	if (on_line) on_line(line);
	if (++m_scanline < V_TOTAL) return;
	m_scanline = 0;
	if (m_gameplay.frame_end) m_gameplay.frame_end();
	std::copy(m_frame_inputs.begin(), m_frame_inputs.end(), m_inputs);
	// MAME draws the screen when vblank ends (VIDEO_UPDATE_AFTER_VBLANK): the 2D layers are
	// as the vblank handler left them
	if (m_draw) {
		android_frame_trace trace("M1 simulation frame capture");
		if (m_smooth) record_frame();     // drawn later, for the moment shown (render_smooth)
		else capture_arcade_frame();
	}
	m_frame++;
}

// ---- IRQs (MAME: irq_raise, irq_callback, irq_control_w, irq_mask_w) ----

void model1::irq_raise(int level)
{
	m_irq_status |= (1 << level);
	m_maincpu.set_irq_line(ASSERT_LINE);
}

u8 model1::irq_callback()
{
	for (int i = 0; i < 8; i++)
		if (BIT(m_irq_status, i)) {
			m_last_irq = i;
			break;
		}
	return m_last_irq;
}

void model1::irq_control_w(u8 data)
{
	switch (data) {
	case 0x10:
		m_irq_status = 0;
		m_maincpu.set_irq_line(CLEAR_LINE);
		break;
	case 0x20:
		m_irq_status &= ~(1 << m_last_irq);
		if (m_irq_status == 0)
			m_maincpu.set_irq_line(CLEAR_LINE);
		break;
	}
}

u8 model1::irq_mask_r() { return m_irq_mask; }

void model1::irq_mask_w(u8 data)
{
	m_irq_mask = data;
	sound_ready();
}

// MAME's sound_ready_w: the UART ready to send or holding a byte raises level 3 (VF's sound
// queue pump) unless it's masked.
void model1::sound_ready()
{
	if ((m_uart_txbuf < 0 || m_uart_rx >= 0) && !BIT(m_irq_mask, 3))
		irq_raise(3);
}

void model1::uart_write(uint8_t v)
{
	m_uart_txbuf = v;
	if (m_uart_shift < 0) {           // the shift register takes it at once
		m_uart_shift = m_uart_txbuf;
		m_uart_txbuf = -1;
		m_uart_shift_left = UART_BYTE_CYCLES;
	}
	sound_ready();
}

uint8_t model1::uart_status()
{
	uint8_t st = 0x80;                                    // DSR
	if (m_uart_txbuf < 0) st |= 0x01;                     // TxRDY
	if (m_uart_rx >= 0) st |= 0x02;                       // RxRDY
	if (m_uart_txbuf < 0 && m_uart_shift < 0) st |= 0x04; // TxEMPTY
	return st;
}

uint8_t model1::uart_read()
{
	uint8_t v = m_uart_rx >= 0 ? uint8_t(m_uart_rx) : 0;
	m_uart_rx = -1;
	return v;
}

void model1::uart_tick(int cycles)
{
	if (m_uart_shift >= 0 && (m_uart_shift_left -= cycles) <= 0) {
		if (m_audio) m_audio->rx_byte(uint8_t(m_uart_shift));
		m_uart_shift = -1;
		if (m_uart_txbuf >= 0) {
			m_uart_shift = m_uart_txbuf;
			m_uart_txbuf = -1;
			m_uart_shift_left += UART_BYTE_CYCLES;
			sound_ready();
		}
	}
	// replies from the sound board, at the same rate
	if (m_uart_rx_left > 0) m_uart_rx_left -= cycles;
	if (m_audio && m_audio->tx_pending() && m_uart_rx < 0 && m_uart_rx_left <= 0) {
		m_uart_rx = m_audio->tx_byte();
		m_uart_rx_left = UART_BYTE_CYCLES;
		sound_ready();
	}
}

void model1::enable_sound()
{
	m_audio = std::make_unique<m1audio>(m_regions.sndcpu, m_regions.pcm1, m_regions.pcm2);
	if (!m_regions.mpegcpu.empty() && !m_regions.mpeg.empty())
		m_audio->attach_dsb(m_regions.mpegcpu, m_regions.mpeg);
	m_audio->reset();
}

void model1::set_volumes(float music, float effects)
{
	if (m_audio) m_audio->set_volumes(music, effects);
}

bool model1::has_analog_input(int channel) const
{
	const auto &profile = game::get_io_profile(m_regions.title);
	return channel >= 0 && channel < int(profile.analog_controls.size()) &&
		   !profile.analog_controls[channel].empty() && (m_ioboard || m_ioboard2 || profile.standin_analog);
}

bool model1::set_analog_input(int channel, uint8_t value)
{
	if (!has_analog_input(channel)) return false;
	m_analog_inputs[channel] = value;
	if (m_ioboard) m_ioboard->analog_inputs[channel] = value;
	if (m_ioboard2) m_ioboard2->analog_inputs[channel] = value;
	return true;
}

// ---- timers (MAME: 0x800 CPU clocks per count; VF leaves them stopped) ----

u16 model1::timer_r(offs_t offset)
{
	if (m_timer_period[offset])
		m_timer_val[offset] = uint16_t(m_timer_remain[offset] / 0x800);
	return m_timer_val[offset];
}

void model1::timer_period_w(offs_t offset, u16 data)
{
	m_timer_period[offset] = data;
	m_timer_remain[offset] = int64_t(0x800) * data;
}

void model1::timer_tick(int cycles)
{
	for (int t = 0; t < 2; t++) {
		if (!m_timer_period[t])
			continue;
		m_timer_remain[t] -= cycles;
		while (m_timer_remain[t] <= 0) {
			if (!BIT(m_irq_mask, 0))
				irq_raise(0);
			m_timer_remain[t] += int64_t(0x800) * m_timer_period[t];
		}
	}
}

void model1::bank_w(offs_t, u16 data, u16 mem_mask)
{
	if ((mem_mask & 0xff) && (data & 0xf) == 1)
		m_bank_base = 0x1000000 + 0x100000 * ((data >> 4) & 0x7);
}

// ---- I/O board stand-in ----
// Byte offsets in the dual-port RAM (V60 address = 0xc00000 + 2 * offset), from MAME 0.190:
//   00-07 analog inputs (unconfigured in the stand-in), 08-0a digital inputs, 0f lamps,
//   20 command to the board (cleared when done; 3 = EEPROM write), 21 status (0x40),
//   100-17f the EEPROM image, kept in the shared RAM.

uint8_t model1::io_board_r(int index)
{
	m_maincpu.adjust_icount(-1);    // MAME: a wait state on every dual-port RAM read
	if (m_ioboard || m_ioboard2) return m_dpram[index];
	if (index < 0x08) {
		const auto &profile = game::get_io_profile(m_regions.title);
		return profile.standin_analog && !profile.analog_controls[index].empty() ? m_analog_inputs[index] : 0x00;
	}
	if (index < 0x0b) return m_inputs[index - 0x08];
	if (index < 0x10) return index == 0x0f ? m_lamps : 0xff;
	if (index == 0x20) return m_io_command;
	if (index == 0x21) return 0x40;
	return m_dpram[index];
}

void model1::io_board_w(int index, uint8_t data)
{
	if (m_ioboard || m_ioboard2) { m_dpram[index] = data; return; }
	if (index == 0x0f) { m_lamps = data; return; }
	if (index == 0x20) {
		m_io_command = data;
		// MAME's guess: 10 us, or 100 ms for an EEPROM write
		m_io_ack = data == 3 ? CPU_CLOCK / 10 : CPU_CLOCK / 100000;
		return;
	}
	m_dpram[index] = data;
}

void model1::io_board_tick(int cycles)
{
	if (m_ioboard) {
		std::copy(std::begin(m_inputs), std::end(m_inputs), m_ioboard->inputs);
		std::copy(m_analog_inputs.begin(), m_analog_inputs.end(), std::begin(m_ioboard->analog_inputs));
		m_ioboard->run(cycles * 1e6 / CPU_CLOCK);
		m_lamps = m_ioboard->outputs;
		return;
	}
	if (m_ioboard2) {
		std::copy(std::begin(m_inputs), std::end(m_inputs), m_ioboard2->inputs);
		std::copy(m_analog_inputs.begin(), m_analog_inputs.end(), std::begin(m_ioboard2->analog_inputs));
		m_ioboard2->run(cycles * 1e6 / CPU_CLOCK);
		m_lamps = m_ioboard2->outputs;
		return;
	}
	if (m_io_command && (m_io_ack -= cycles) <= 0)
		m_io_command = 0;
}

// ---- TGP status ----

u16 model1::fifoin_status_r() { return 0xffff; }

// ---- memory map (MAME: model1_mem / model1_io) ----

uint16_t model1::bus_read16(uint32_t a)
{
	a &= 0xfffffe;
	const uint8_t *rom = m_regions.maincpu.data();
	auto rd = [](const uint8_t *p) { return uint16_t(p[0] | (p[1] << 8)); };
	switch (a >> 20) {
	case 0x0: return rd(rom + a);                                        // ROMA (unpopulated)
	case 0x1: return rd(rom + m_bank_base + (a & 0xfffff));             // ROMO, banked data
	case 0x2: return rd(rom + a);                                        // ROMX
	case 0xf: return a >= 0xf80000 ? rd(rom + a) : 0xffff;              // ROM0
	}
	if (a >= 0x400000 && a < 0x410000) return rd(&m_ram_a[a - 0x400000]);
	if (a >= 0x500000 && a < 0x540000) return rd(&m_ram_b[a - 0x500000]);
	if (a >= 0x600000 && a < 0x610000) return m_display_list0[(a - 0x600000) >> 1];
	if (a >= 0x610000 && a < 0x620000) return m_display_list1[(a - 0x610000) >> 1];
	if (a >= 0x680000 && a < 0x680004) return model1_listctl_r((a - 0x680000) >> 1);
	if (a >= 0x700000 && a < 0x710000) return m_tiles.tile_r((a - 0x700000) >> 1);
	if (a >= 0x780000 && a < 0x800000) return m_tiles.char_r((a - 0x780000) >> 1);
	if (a >= 0x900000 && a < 0x904000) return m_paletteram16[(a - 0x900000) >> 1];
	if (a >= 0x910000 && a < 0x91c000) return m_color_xlat[(a - 0x910000) >> 1];
	if (m_comm) {
		if (a >= 0xb00000 && a < 0xb01000) return m_comm->read(a - 0xb00000) | (uint16_t(m_comm->read(a - 0xb00000 + 1)) << 8);
		if (a == 0xb01000) return 0xff00 | m_comm->cn();
		if (a == 0xb01002) return 0xff00 | m_comm->fg();
	}
	if (a >= 0xc00000 && a < 0xc01000) return 0xff00 | io_board_r(((a - 0xc00000) >> 1) & 0x7ff);
	if (a >= 0xc40000 && a < 0xc40004) return 0xff00 | ((a & 2) ? uart_status() : uart_read());
	if (m_lle && a >= 0xd00000 && a < 0xde0000) return lle_v60_r(a);
	if (a >= 0xd00000 && a < 0xd20000) return v60_copro_ram_adr_r(0, 0xffff);
	if (a >= 0xd20000 && a < 0xd40000) return v60_copro_ram_r((a >> 1) & 1, 0xffff);
	if (a >= 0xd80000 && a < 0xda0000) return v60_copro_fifo_r((a >> 1) & 1, 0xffff);
	if (a >= 0xdc0000 && a < 0xde0000) return fifoin_status_r();
	if (a == 0xe00002) return m_irq_mask;
	if (a >= 0xe0000c && a < 0xe00010) return timer_r((a >> 1) & 1);
	return 0xffff;
}

void model1::bus_write16(uint32_t a, uint16_t v, uint16_t mask)
{
	a &= 0xfffffe;
	static uint32_t watch_lo = 1, watch_hi = 0;      // debug: M1_WATCHW=addr or lo-hi (hex)
	static const bool watch_init = [] {
		if (const char *w = getenv("M1_WATCHW")) {
			char *end;
			watch_lo = watch_hi = strtoul(w, &end, 16);
			if (*end == '-') watch_hi = strtoul(end + 1, nullptr, 16);
		}
		return true;
	}();
	(void)watch_init;
	if (a >= watch_lo && a <= watch_hi)
		fprintf(stderr, "frame %llu: write %06x = %04x/%04x pc %06x\n", (unsigned long long)m_frame, a, v, mask, m_maincpu.pc());
	auto wr = [&](uint8_t *p) {
		if (mask & 0x00ff) p[0] = uint8_t(v);
		if (mask & 0xff00) p[1] = uint8_t(v >> 8);
	};
	auto combine = [&](uint16_t &d) { d = (d & ~mask) | (v & mask); };
	if (a >= 0x400000 && a < 0x410000) { wr(&m_ram_a[a - 0x400000]); return; }
	if (a >= 0x500000 && a < 0x540000) {
		wr(&m_ram_b[a - 0x500000]);
		if (a == m_gameplay.work_ram_watch && m_gameplay.work_ram_written) m_gameplay.work_ram_written(mask);
		return;
	}
	if (a >= 0x600000 && a < 0x610000) { combine(m_display_list0[(a - 0x600000) >> 1]); m_list_written[0] = m_frame; list_written(0, (a - 0x600000) >> 1); return; }
	if (a >= 0x610000 && a < 0x620000) { combine(m_display_list1[(a - 0x610000) >> 1]); m_list_written[1] = m_frame; list_written(1, (a - 0x610000) >> 1); return; }
	if (a >= 0x680000 && a < 0x680004) { model1_listctl_w((a - 0x680000) >> 1, v, mask); return; }
	if (a >= 0x700000 && a < 0x710000) {
		m_tiles.tile_w((a - 0x700000) >> 1, v, mask);
		if (a == 0x70a004 || a == 0x70a00c) sky_scroll_written(a);   // the sky's scroll (model1_interp.cpp)
		return;
	}
	if (a >= 0x780000 && a < 0x800000) { m_tiles.char_w((a - 0x780000) >> 1, v, mask); return; }
	if (a >= 0x900000 && a < 0x904000) { model1_paletteram_w((a - 0x900000) >> 1, v, mask); return; }
	if (a >= 0x910000 && a < 0x91c000) { combine(m_color_xlat[(a - 0x910000) >> 1]); return; }
	if (m_comm) {
		if (a >= 0xb00000 && a < 0xb01000) {
			if (mask & 0xff) m_comm->write(a - 0xb00000, uint8_t(v));
			if (mask & 0xff00) m_comm->write(a - 0xb00000 + 1, uint8_t(v >> 8));
			return;
		}
		if (a == 0xb01000) { if (mask & 0xff) m_comm->set_cn(uint8_t(v)); return; }
		if (a == 0xb01002) { if (mask & 0xff) m_comm->set_fg(uint8_t(v)); return; }
	}
	if (a >= 0xc00000 && a < 0xc01000) { if (mask & 0xff) io_board_w(((a - 0xc00000) >> 1) & 0x7ff, uint8_t(v)); return; }
	if (a == 0xc40000) { if (mask & 0xff) uart_write(uint8_t(v)); return; }
	if (a == 0xc40002) return;                                   // i8251 mode/command
	if (m_lle && a >= 0xd00000 && a < 0xde0000) { lle_v60_w(a, v, mask); return; }
	if (a >= 0xd00000 && a < 0xd20000) { v60_copro_ram_adr_w(0, v, mask); return; }
	if (a >= 0xd20000 && a < 0xd40000) { v60_copro_ram_w((a >> 1) & 1, v, mask); return; }
	if (a >= 0xd80000 && a < 0xda0000) { v60_copro_fifo_w((a >> 1) & 1, v, mask); return; }
	if (a == 0xe00000) { if (mask & 0xff) irq_control_w(uint8_t(v)); return; }
	if (a == 0xe00002) { if (mask & 0xff) irq_mask_w(uint8_t(v)); return; }
	if (a == 0xe00004) { bank_w(0, v, mask); return; }
	if (a == 0xe00006) { combine(m_timer_mode); return; }
	if (a >= 0xe00008 && a < 0xe0000c) { timer_period_w((a >> 1) & 1, v); return; }
}

uint8_t model1::read8(uint32_t a)
{
	const uint8_t value = uint8_t(bus_read16(a) >> ((a & 1) * 8));
	if (m_gameplay.read_value) {
		const uint32_t pc = m_maincpu.pc();
		if (pc >= m_gameplay.read_pc_first && pc <= m_gameplay.read_pc_last) return uint8_t(m_gameplay.read_value(pc, a, value));
	}
	return value;
}
void model1::write8(uint32_t a, uint8_t v) { bus_write16(a, uint16_t(v) << ((a & 1) * 8), (a & 1) ? 0xff00 : 0x00ff); }

uint16_t model1::read16(uint32_t a)
{
	if (a & 1) return read8(a) | (read8(a + 1) << 8);
	const uint16_t value = bus_read16(a);
	if (m_gameplay.read_value) {
		const uint32_t pc = m_maincpu.pc();
		if (pc >= m_gameplay.read_pc_first && pc <= m_gameplay.read_pc_last) return m_gameplay.read_value(pc, a, value);
	}
	return value;
}

void model1::write16(uint32_t a, uint16_t v)
{
	if (a & 1) { write8(a, uint8_t(v)); write8(a + 1, uint8_t(v >> 8)); return; }
	bus_write16(a, v, 0xffff);
}

uint32_t model1::read32(uint32_t a)
{
	const uint32_t value = read16(a) | (uint32_t(read16(a + 2)) << 16);
	if (a >= m_gameplay.read32_watch && a <= std::max(m_gameplay.read32_watch, m_gameplay.read32_watch_end) &&
		m_gameplay.read32_value)
		return m_gameplay.read32_value(m_maincpu.pc(), a, value);
	return value;
}
void model1::write32(uint32_t a, uint32_t v) { write16(a, uint16_t(v)); write16(a + 2, uint16_t(v >> 16)); }

uint16_t model1::io_read16(uint32_t a) { return bus_read16(a); }
void model1::io_write16(uint32_t a, uint16_t v) { bus_write16(a, v, 0xffff); }

// ---- render scale ----

void model1::set_render_scale(int scale)
{
	m_scale = std::clamp(scale, 1, 12);       // up to 6x, or less with antialiasing on top
	s_moire_blend = m_scale > 1;
	s_hd_raster = m_scale > 1 && !getenv("M1_MAME_RASTER");
	resize_frame();
}

void model1::set_aspect(float aspect)
{
	// Full-width, half-height stacked views need twice the host composition's aspect.
	m_aspect = std::clamp(aspect, 4.0f / 3.0f, 64.0f / 9.0f);
	// the arcade's pixels are slightly wide: 496x384 fills a 4:3 tube
	float width = SCREEN_H * m_aspect * float(SCREEN_W) / (SCREEN_H * 4.0f / 3.0f);
	m_ext = std::max(0, int(std::lround((width - SCREEN_W) / 2)));
	resize_frame();
}

void model1::set_free_cam(bool on)
{
	m_free_cam = on;
	free_cam_reset();
}

void model1::free_cam_reset()
{
	m_free_cam_pose = {};
	m_free_cam_matrix = { 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0 };
}

void model1::free_cam_move(float forward, float right, float up, float yaw, float pitch)
{
	constexpr float TAU = 6.283185307179586f;
	m_free_cam_pose.yaw = std::remainder(m_free_cam_pose.yaw + yaw, TAU);
	m_free_cam_pose.pitch = std::clamp(m_free_cam_pose.pitch + pitch, -1.55334f, 1.55334f);
	const float sy = std::sin(m_free_cam_pose.yaw), cy = std::cos(m_free_cam_pose.yaw);
	const float sp = std::sin(m_free_cam_pose.pitch), cp = std::cos(m_free_cam_pose.pitch);
	m_free_cam_pose.position[0] += forward * sy * cp + right * cy;
	m_free_cam_pose.position[1] += forward * sp + up;
	m_free_cam_pose.position[2] += forward * cy * cp - right * sy;
	set_free_cam_pose(m_free_cam_pose);
}

void model1::set_free_cam_pose(const free_cam_pose &pose)
{
	for (float p : pose.position) if (!std::isfinite(p)) return;
	if (!std::isfinite(pose.yaw) || !std::isfinite(pose.pitch)) return;
	m_free_cam_pose = pose;
	m_free_cam_pose.yaw = std::remainder(m_free_cam_pose.yaw, 6.283185307179586f);
	m_free_cam_pose.pitch = std::clamp(m_free_cam_pose.pitch, -1.55334f, 1.55334f);
	const float cy = std::cos(m_free_cam_pose.yaw), sy = std::sin(m_free_cam_pose.yaw);
	const float cp = std::cos(m_free_cam_pose.pitch), sp = std::sin(m_free_cam_pose.pitch);
	m_free_cam_matrix = {
		cy, 0, -sy,
		-sp * sy, cp, -sp * cy,
		cp * sy, sp, cp * cy,
		0, 0, 0
	};
	for (int row = 0; row < 3; row++)
		m_free_cam_matrix[9 + row] = -m_free_cam_matrix[row * 3] * m_free_cam_pose.position[0]
			- m_free_cam_matrix[row * 3 + 1] * m_free_cam_pose.position[1]
			- m_free_cam_matrix[row * 3 + 2] * m_free_cam_pose.position[2];
}

void model1::resize_frame()
{
	const int w = (SCREEN_W + 2 * m_ext) * m_scale, h = SCREEN_H * m_scale;
	if (m_bitmap.width() != w || m_bitmap.height() != h) recheck_gpu3d();
	// the view's clip is kept in render pixels: undo the old widening before changing it
	view_t *v = m_view.get();
	float gx1 = v->lx1, gx2 = v->lx2;
	if (v->ext > 0 && v->lx1 < 0) { gx1 += v->ext; gx2 -= v->ext; }
	m_bitmap.allocate((SCREEN_W + 2 * m_ext) * m_scale, SCREEN_H * m_scale);
	m_bitmap_whole = true;                    // (not KEY yet, for the GPU's parts)
	m_tile_tmp.allocate(SCREEN_W + 2 * m_ext, SCREEN_H);
	m_l4_frame = ~0ull;                       // (made again empty: layer 4 drawn into it again, a
	                                          // stopped game's too, or a 4:3 screen's painted sides go black)
	m_tiles.m_ox = 0;
	v->scale = float(m_scale);
	v->hd_raster = s_hd_raster;
	v->ext = float(m_ext);
	v->set_viewport(v->xc, v->yc, gx1, gx2, v->ly1, v->ly2);
	// the above-HUD pass's buffers: made when first needed (never in VF; they'd be two frames' worth)
	m_overlay_stride = m_bitmap.width();
	m_overlay_block.reset();
	m_overlay_hud_snapshot.reset();
}

void model1::redraw()
{
	const auto render_done = m_render_done;     // emulation state: drawing again mustn't touch it
	const int quads = m_last_quads;
	if (!m_smooth) {
		m_redrawing = true;
		screen_update(m_bitmap, m_bitmap.cliprect());   // the frame drawn as it ran: nothing has moved since
		m_redrawing = false;
	} else if (!m_shown)
		m_bitmap.fill(0);                               // nothing shown since the start or a reset
	else {
		// Smooth motion: the moment shown, which is a game frame behind the newest list, and when
		// frames ran on without a new picture shown (a held scene change, the hold after a camera
		// cut), older still: then drawn from what was kept as the next frame started.
		const bool old = m_shown_frame != m_frame && m_still.frame == m_shown_frame;
		if (old) swap_still();
		m_redrawing = true;
		render_smooth(m_shown_now);
		m_redrawing = false;
		if (old) swap_still();
	}
	m_unshown = false;
	m_frame_parts = false;
	m_render_done = render_done;
	m_last_quads = quads;
}

bool model1::past_boot() const
{
	// a game with no camera RAM (SWA) holds a Test press made in its boot until the boot's over
	if (!m_profile->camera_ram) return true;
	return camera() != 0;
}

static inline bool transparent_tile_row(const uint32_t *p)
{
	constexpr uint32_t KEY = 0xff000000;
	return !((p[0] ^ KEY) | (p[1] ^ KEY) | (p[2] ^ KEY) | (p[3] ^ KEY) |
		(p[4] ^ KEY) | (p[5] ^ KEY) | (p[6] ^ KEY) | (p[7] ^ KEY));
}

// A layer at the arcade's resolution (and the widescreen sides), with each row's drawn span,
// so the scaling skips what's empty (the HUD and text layers are mostly empty).
model1::layer_revision model1::current_layer_revision(int layer) const
{
	return {m_tiles.layer_revision(layer), m_tile_palette_revision, m_ram_b[0x1138],
		m_gameplay.layer_sides_revision ? m_gameplay.layer_sides_revision(layer) : 0};
}

bool model1::layer_layout_changed(int layer, int flags, double slide_time) const
{
	const layer_cache &c = m_layer_cache[layer & 7];
	return c.frame == ~0ull || c.flags != flags || c.ext != m_ext || c.wide != m_wide_frame ||
		c.px.width() != SCREEN_W + 2 * m_ext || c.slide_time != slide_time ||
		(m_tile_slide_interpolating && m_tile_slide_layer == layer);
}

bool model1::layer_needs_redraw(int layer, int flags, double slide_time, const layer_revision &revision) const
{
	if (layer_layout_changed(layer, flags, slide_time)) return true;
	// Wing War's repeating starfield check uses another layer's map; leave those
	// layers and narrow screens on the per-frame path until their inputs are tracked.
	const bool wingwar_reuse = title().name == "wingwar" && m_wide_frame && layer != 4 && layer != 5;
	if (title().name == "vr" || (title().name == "swa" && m_gameplay.layer_sides_revision) || wingwar_reuse) {
		layer_revision previous = m_layer_revision[layer & 7];
		// SWA's disabled layer 6 contains no palette-dependent pixels or side treatment.
		if (title().name == "swa" && layer == 6 && (m_tiles.tile_r(0x5007) & 0x8000))
			previous.palette = revision.palette;
		return previous != revision;
	}
	return m_layer_cache[layer & 7].frame != m_frame;
}

static void scan_layer_row(const bitmap_rgb32 &px, int y, int16_t &x0, int16_t &x1)
{
	constexpr uint32_t KEY = 0xff000000;
	const uint32_t *r = &px.pix(y);
	const int tw = px.width();
	int a = 0, b = tw;
	while (a + 8 <= tw && transparent_tile_row(r + a)) a += 8;
	while (a < tw && r[a] == KEY) a++;
	if (a == tw) { x0 = x1 = 0; return; }
	while (b - 8 >= a && transparent_tile_row(r + b - 8)) b -= 8;
	while (b > a && r[b - 1] == KEY) b--;
	x0 = int16_t(a); x1 = int16_t(b);
}

model1::layer_cache &model1::cached_layer(int layer, int flags)
{
	constexpr uint32_t KEY = 0xff000000;
	layer_cache &c = m_layer_cache[layer & 7];
	const int tw = SCREEN_W + 2 * m_ext;
	std::array<float, SCREEN_H> slide_rows;
	const bool smooth_slide = smooth_tile_slide(layer, slide_rows);
	const double slide_time = smooth_slide ? m_draw_time : -1;
	const layer_revision revision = current_layer_revision(layer);
	if (layer_needs_redraw(layer, flags, slide_time, revision)) {
		static constexpr const char *rebuild_names[] = {
			"M1 tile rebuild 0", "M1 tile rebuild 1", "M1 tile rebuild 2", "M1 tile rebuild 3",
			"M1 tile rebuild 4", "M1 tile rebuild 5", "M1 tile rebuild 6", "M1 tile rebuild 7"
		};
		android_frame_trace rebuild(rebuild_names[layer & 7]);
		hud_plan *plan = nullptr;
#if defined(__ANDROID__) || defined(__APPLE__)
#if defined(__ANDROID__) && defined(M1_ALWAYS_FRAME_TIMINGS)
		const bool tracing = ATrace_isEnabled();
#else
		constexpr bool tracing = false;
#endif
		if (layer == 1 && title().name == "vr" && (hud_partial_updates || tracing)) {
			const auto &previous = m_layer_revision[1];
			const auto changes = m_tiles.layer1_changes_since(previous.tiles);
			const bool layout = layer_layout_changed(layer, flags, slide_time);
			if (tracing) {
				char cause[128];
				snprintf(cause, sizeof cause, "M1 layer 1 inv n=%d direct=%d gfx=%d pair=%d scroll=%d line=%d mask=%d full=%d pal=%d mode=%d layout=%d",
					changes.entries, changes.direct, changes.graphics, changes.paired_map, changes.scroll,
					changes.line, changes.mask, changes.full_decode,
					previous.palette != revision.palette, previous.mode != revision.mode, layout);
				android_frame_trace invalidation(cause);
			}
			if (hud_partial_updates) {
				plan = &m_hud_plan;
				// VR's side hook handles only layer 4. Layer 1 must use the ordinary masked draw.
				const bool supported = flags == 0 && !c.sides && !m_video->repeat_curtains &&
					!(m_video->centered_scroll_layers & (1u << layer)) &&
					m_tiles.layer1_band_drawing_supported();
				if (!layout && supported && changes.entries && !changes.graphics &&
					!changes.paired_map && !changes.scroll && !changes.line && !changes.mask && !changes.full_decode &&
					previous.palette == revision.palette && previous.mode == revision.mode) {
					const auto bands = m_tiles.layer1_dirty_bands(previous.tiles, SCREEN_H / 8);
					if (rebuild_hud_bands(c, bands)) {
						find_curtains(c, layer, flags);
						c.frame = m_frame;
						c.gen++;
						m_layer_revision[1] = revision;
						return c;
					}
				} else if (!supported && tracing) {
					char cause[96];
					snprintf(cause, sizeof cause, "M1 layer 1 partial unsupported vs=%04x flags=%x sides=%d curtains=%d",
						unsigned(m_tiles.tile_r(0x5004)), unsigned(flags), c.sides, m_video->repeat_curtains);
					android_frame_trace fallback(cause);
				}
			}
		}
#endif
		if (c.px.width() != tw) {
			c.px.allocate(tw, SCREEN_H);
			c.px.fill(KEY);
		} else if (title().name == "vr" && !(c.flags & segas24_tile::DRAW_OPAQUE) &&
			!(flags & segas24_tile::DRAW_OPAQUE)) {
			// Outside the previous row spans the bitmap is already transparent.
			for (int y = 0; y < SCREEN_H; y++)
				if (c.x0[y] < c.x1[y])
					std::fill_n(&c.px.pix(y, c.x0[y]), c.x1[y] - c.x0[y], KEY);
		} else {
			c.px.fill(KEY);
		}
		if (!m_layers_together) m_tiles.m_ox = m_ext;     // (else set for them all)
		const auto &slides = m_video->wide_tile_slides;
		const auto slide = std::find_if(slides.begin(), slides.end(), [&](const auto &s) {
			return s.layer == layer && s.mode == m_ram_b[0x1138] && s.distance > 0;
		});
		const bool sliding = (m_ext || smooth_slide) && m_wide_frame && slide != slides.end() &&
			(m_tiles.tile_r(0x5000 + (layer >> 1)) & 0x8000) &&
			(m_tiles.tile_r(0x5004 + ((layer >> 1) & 2)) & 0x6000) == 0x4000;
		const bool repeating = repeat_tile_layer(layer);
		{
			android_frame_trace draw(layer == 1 ? "M1 layer 1 tile drawing" : nullptr);
			if (sliding) m_tiles.draw_wide(c.px, layer, flags, m_ext, slide->distance,
				smooth_slide ? slide_rows.data() : nullptr);
			else if (repeating || (extend_backdrops() && (flags & segas24_tile::DRAW_OPAQUE)))
				m_tiles.draw_wide(c.px, layer, flags, m_ext);
			else {
				// (where each pixel came from, for find_curtains' patterns)
				const bool pens = m_video->repeat_curtains && m_ext && m_wide_frame && !(flags & segas24_tile::DRAW_OPAQUE);
				if (pens) c.coord.assign(size_t(tw) * SCREEN_H, ~0u);
				else c.coord.clear();
				segas24_tile::m_coord = pens ? c.coord.data() : nullptr;
				m_tiles.draw(c.px, c.px.cliprect(), layer, 0, flags);
				segas24_tile::m_coord = nullptr;
			}
		}
		if (!m_layers_together) m_tiles.m_ox = 0;
		pin_hud_tiles(c.px, layer, plan);
		if (flags & segas24_tile::DRAW_OPAQUE)
			fill_backdrop_sides(c.px, layer, flags);
		c.sides = sliding || repeating || (m_ext && m_wide_frame && !(flags & segas24_tile::DRAW_OPAQUE) && m_gameplay.layer_sides &&
			m_gameplay.layer_sides(layer, &c.px.pix(0), tw, SCREEN_H, m_ext));
		clip_scrolling_sides(c.px, layer, flags);
		{
			android_frame_trace scan(layer == 1 ? "M1 layer 1 span scan" : nullptr);
			c.x0.assign(SCREEN_H, 0);
			c.x1.assign(SCREEN_H, 0);
			for (int y = 0; y < SCREEN_H; y++) scan_layer_row(c.px, y, c.x0[y], c.x1[y]);
		}
		find_curtains(c, layer, flags);
		c.frame = m_frame; c.flags = flags; c.ext = m_ext; c.wide = m_wide_frame; c.gen++;
		c.slide_time = slide_time;
		m_layer_revision[layer & 7] = revision;
	}
	return c;
}

// Widescreen: a row a layer covers edge to edge is a curtain (the black that hides a stage change),
// carried out across the sides unless the game filled them: its edge pixel repeated. With
// repeat_curtains, rows whose pens repeat exactly across their whole width (Wing War's green menu
// panels: two 248-pixel halves, the same tiles in two palettes; stripes) have that pattern drawn on
// across the sides instead, each side from the period beside it, in its own palette. A row holding
// anything else is not one pattern: it keeps its edge pixel only where the art is one colour beside
// both edges (letters on a bar), and pictures have no curtain.
void model1::find_curtains(layer_cache &c, int layer, int flags) const
{
	constexpr uint32_t KEY = 0xff000000;
	c.curtain.assign(SCREEN_H, 0);
	if (!m_ext || !m_wide_frame || c.sides || (flags & segas24_tile::DRAW_OPAQUE)) return;
	const int tw = c.px.width();
	const bool repeat = c.coord.size() == size_t(tw) * SCREEN_H;
	std::vector<uint8_t> pens(repeat ? size_t(SCREEN_H) * SCREEN_W : 0);
	std::array<int16_t, SCREEN_H> period{};
	int last = 1;
	for (int y = 0; y < SCREEN_H; y++) {
		const uint32_t *src = &c.px.pix(y);
		if (!extend_tile_row(layer, y) || src[m_ext] == KEY || src[m_ext + SCREEN_W - 1] == KEY) continue;
		int covered = 0;
		for (int x = m_ext; x < m_ext + SCREEN_W; x++) covered += src[x] != KEY;
		if (covered * 10 < SCREEN_W * 9) continue;
		c.curtain[y] = 1;
		period[y] = 1;
		if (!repeat || covered != SCREEN_W) continue;
		const uint32_t *coord = &c.coord[size_t(y) * tw + m_ext];
		uint8_t *pen = &pens[size_t(y) * SCREEN_W];
		bool known = true;
		for (int x = 0; x < SCREEN_W && known; x++) {
			known = coord[x] != ~0u;
			if (known) pen[x] = m_tiles.pen_at(coord[x]);
		}
		if (!known) continue;
		auto pattern = [&](int p) {
			for (int x = 0; x + p < SCREEN_W; x++)
				if (pen[x] != pen[x + p]) return false;
			return true;
		};
		int p = last > 1 && pattern(last) ? last : 0;          // (a panel's rows share theirs)
		for (int q = 1; q <= SCREEN_W / 2 && !p; q++)
			if (pattern(q)) p = q;
		if (p <= 1) continue;                                   // (one colour: its edge pixel)
		last = period[y] = int16_t(p);
		c.curtain[y] = 0;
	}
	// With repeat_curtains, an edge pixel is carried out only where the art beside the edge is one
	// colour (a curtain, a bar with its letters inside): a picture (Wing War's match screen) would be
	// stretched into streaks, so a run of rows with any such row leaves the sides alone.
	if (m_video->repeat_curtains) {
		constexpr int SOLID = 8;
		for (int y0 = 0; y0 < SCREEN_H;) {
			if (!c.curtain[y0]) { y0++; continue; }
			int y1 = y0;
			bool solid = true;
			for (; y1 < SCREEN_H && c.curtain[y1]; y1++) {
				const uint32_t *r = &c.px.pix(y1) + m_ext;
				for (int x = 1; x < SOLID && solid; x++)
					solid = r[x] == r[0] && r[SCREEN_W - 1 - x] == r[SCREEN_W - 1];
			}
			if (!solid) std::fill(c.curtain.begin() + y0, c.curtain.begin() + y1, uint8_t(0));
			y0 = y1;
		}
	}

	// Each band of rows sharing a period has it drawn on across the sides. Where the colours repeat
	// too (stripes), the period is just wrapped round. Where they don't (a panel half in its own
	// palette, its art not made to wrap: its last column doesn't lead into its first), copies of
	// it are stitched together along the cut stitch_pattern finds, beginning inside the arcade's
	// columns, so the arcade's edge leads into the sides as seamlessly as copies into each other.
	for (int y0 = 0; y0 < SCREEN_H;) {
		const int p = period[y0];
		int y1 = y0 + 1;
		while (y1 < SCREEN_H && period[y1] == p) y1++;
		const int rows = y1 - y0;
		bool exact = true;
		for (int y = y0; y < y1 && exact && p > 1; y++) {
			const uint32_t *r = &c.px.pix(y) + m_ext;
			for (int x = 0; x + p < SCREEN_W && exact; x++) exact = r[x] == r[x + p];
		}
		// the band seen from each side (column m is m columns in from that edge), and its stitch: the
		// plain wrap where the colours repeat, else the one found (once for each band's pens, the
		// two sides at once)
		std::array<curtain_stitch, 2> stitch;
		std::array<std::future<curtain_stitch>, 2> finding;
		std::array<uint64_t, 2> key{};
		std::array<std::vector<uint8_t>, 2> band;
		for (int side = 0; side < 2 && p > 1; side++) {
			stitch[side].q = p;
			stitch[side].k.assign(rows, 0);
			if (exact || rows < 2 || p < 64) continue;
			band[side].resize(size_t(rows) * p);
			uint64_t h = 1469598103934665603ull;
			auto mix = [&](uint64_t v) { h = (h ^ v) * 1099511628211ull; };
			mix(uint64_t(p)); mix(uint64_t(rows));
			for (int r = 0; r < rows; r++)
				for (int m = 0; m < p; m++)
					mix(band[side][size_t(r) * p + m] = pens[size_t(y0 + r) * SCREEN_W + (side ? SCREEN_W - 1 - m : m)]);
			key[side] = h;
			std::lock_guard<std::mutex> lock(m_stitch_lock);
			const auto it = m_stitches.find(h);
			if (it != m_stitches.end()) stitch[side] = it->second;
			else finding[side] = std::async(std::launch::async, stitch_pattern, band[side].data(), rows, p);
		}
		for (int side = 0; side < 2 && p > 1; side++) {
			if (finding[side].valid()) {
				stitch[side] = finding[side].get();
				std::lock_guard<std::mutex> lock(m_stitch_lock);
				if (m_stitches.size() >= 32) m_stitches.clear();
				m_stitches.emplace(key[side], stitch[side]);
			}
			auto column = [&](int s) { return side ? m_ext + SCREEN_W - 1 - s : m_ext + s; };
			const int q = stitch[side].q;
			for (int r = 0; r < rows; r++) {
				uint32_t *row = &c.px.pix(y0 + r);
				const int k = stitch[side].k[r];
				// (the columns copied from, k and on, are never written)
				for (int s = k - 1; s >= -m_ext; s--)
					row[column(s)] = row[column(k + ((s - k) % q + q) % q)];
			}
		}
		if (p > 1)
			for (int y = y0; y < y1; y++) { c.x0[y] = 0; c.x1[y] = int16_t(tw); }
		y0 = y1;
	}
}

// Where copies of a pattern `p` columns wide (`pens`, row by row, column 0 at the arcade's edge) join
// best: copies q columns apart, row r's cut k[r] columns in, columns left of it shown from the copy
// (column + q). The cut is the path through the rows (moving a few columns a row) along which the
// joined pens differ least: across it, and along the diagonals, the pattern's grain (Wing War's
// streaks) weighted most, and where the copy is most like what it covers. A minimum-error boundary
// cut, as in image quilting.
model1::curtain_stitch model1::stitch_pattern(const uint8_t *pens, int rows, int p)
{
	constexpr int MOVE = 4, SPAN = 60;
	const int cuts = std::min(96, p * 2 / 5);
	const int qlo = std::max(cuts, p - cuts - SPAN), qhi = p - cuts;
	auto at = [&](int r, int x) { return int(pens[size_t(r) * p + x]); };
	long down = 0, up = 0;                                    // the grain: the smoother diagonal
	for (int r = 0; r + 1 < rows; r++)
		for (int x = 0; x + 1 < p; x++) {
			down += std::abs(at(r, x) - at(r + 1, x + 1));
			up += std::abs(at(r, x + 1) - at(r + 1, x));
		}
	const int across = 2, wd = down <= up ? 4 : 1, wu = down <= up ? 1 : 4;
	curtain_stitch best;
	long best_cost = std::numeric_limits<long>::max();
	std::vector<long> cost(cuts), next(cuts);
	std::vector<int16_t> back(size_t(rows) * cuts);
	for (int q = qlo; q <= qhi; q++) {
		auto shown = [&](int r, int s, int k) { return s < k ? at(r, s + q) : at(r, s); };
		// the join across the cut, and how like the original the copy is about it
		auto join = [&](int r, int k) {
			long c = across * std::abs(at(r, k - 1 + q) - at(r, k));
			for (int s = std::max(k - 3, 0); s < std::min(k + 3, p - q); s++) c += std::abs(at(r, s) - at(r, s + q));
			return c;
		};
		for (int b = 0; b < cuts; b++) cost[b] = join(0, b + 1);
		bool pruned = false;
		for (int r = 1; r < rows && !pruned; r++) {
			long low = std::numeric_limits<long>::max();
			for (int b = 0; b < cuts; b++) {
				const int k2 = b + 1;
				long bc = std::numeric_limits<long>::max();
				int ba = b;
				for (int a = std::max(0, b - MOVE); a <= std::min(cuts - 1, b + MOVE); a++) {
					const int k1 = a + 1;
					long c = cost[a];
					if (c >= bc) continue;
					for (int s = std::max(std::min(k1, k2) - 2, 0); s <= std::max(k1, k2); s++) {
						if ((s < k1) != (s + 1 < k2)) c += wd * std::abs(shown(r - 1, s, k1) - shown(r, s + 1, k2));
						if ((s + 1 < k1) != (s < k2)) c += wu * std::abs(shown(r - 1, s + 1, k1) - shown(r, s, k2));
					}
					if (c < bc) { bc = c; ba = a; }
				}
				next[b] = bc + join(r, k2);
				back[size_t(r) * cuts + b] = int16_t(ba);
				low = std::min(low, next[b]);
			}
			std::swap(cost, next);
			pruned = low >= best_cost;
		}
		if (pruned) continue;
		const int end = int(std::min_element(cost.begin(), cost.end()) - cost.begin());
		if (cost[end] >= best_cost) continue;
		best_cost = cost[end];
		best.q = q;
		best.k.assign(rows, 0);
		for (int r = rows - 1, b = end; r >= 0; r--) {
			best.k[r] = int16_t(b + 1);
			if (r) b = back[size_t(r) * cuts + b];
		}
	}
	return best;
}

// A curtain row's pixel at side column x (of the layer's widened width): its edge pixel.
uint32_t model1::curtain_pixel(const layer_cache &c, int y, int x) const
{
	return c.px.pix(y, x < m_ext ? m_ext : m_ext + SCREEN_W - 1);
}

bool model1::hud_edge_pinning_active() const
{
	if (!m_video || !m_video->pin_hud_edges || !m_ext || !m_wide_frame) return false;
	const uint8_t mode = m_ram_b[0x1138];
	return mode == 0x03 || mode == 0x07 || mode == 0x12 || mode == 0x14 ||
		mode == 0x15 || mode == 0x16 || mode == 0x19 || mode == 0x1a;
}

size_t model1::hud_cell(const bitmap_rgb32 &pixels, int cx, int cy, int map_row, int scroll) const
{
	for (int y = cy * 8; y < (cy + 1) * 8; y++) {
		if (transparent_tile_row(&pixels.pix(y, m_ext + cx * 8))) continue;
		const uint32_t code = m_tiles.tile_code(m_tiles.tile_r(map_row + ((cx * 8 - scroll) & 511) / 8));
		size_t tag = 1;
		for (size_t i = 0; i < m_video->hud_tile_groups.size(); i++) {
			const auto &range = m_video->hud_tile_groups[i];
			if (code >= range.first && code <= range.last) tag = i + 2;
		}
		return tag;
	}
	return 0;
}

void model1::pin_hud_tiles(bitmap_rgb32 &pixels, int layer, hud_plan *plan) const
{
	if (plan) {
		plan->cells.fill(0);
		for (auto &row : plan->moves) row.clear();
	}
	if (layer != 1 || !hud_edge_pinning_active()) return;
	android_frame_trace hud("M1 HUD pinning");

	constexpr uint32_t KEY = 0xff000000;
	constexpr int COLS = SCREEN_W / 8, ROWS = SCREEN_H / 8;
	// Attract's bottom notices are HUD; its ranking board and central prompts are not.
	const uint8_t mode = m_ram_b[0x1138];
	const int first_row = mode == 0x03 || mode == 0x07 ? 43 : 0;
	std::array<bool, COLS * ROWS> occupied{};
	std::array<size_t, COLS * ROWS> artwork{};
	const uint16_t hscroll = m_tiles.tile_r(0x5000), vscroll = m_tiles.tile_r(0x5004);
#if defined(__ANDROID__) && defined(M1_ALWAYS_FRAME_TIMINGS)
	const bool baseline = hud_benchmark_baseline;
#else
	constexpr bool baseline = false;
#endif
	auto check_occupied = [&](int cx, int cy) {
		for (int row = cy * 8; row < (cy + 1) * 8; row++)
			if (!transparent_tile_row(&pixels.pix(row, m_ext + cx * 8))) return true;
		return false;
	};
	for (int cy = first_row; cy < ROWS; cy++) {
		const int scroll = hscroll & 0x8000 ? m_tiles.tile_r(0x4000 + cy * 8) : hscroll;
		const int y = (cy * 8 + vscroll) & 0x1ff;
		const int map_row = (y / 8) * 64;
		for (int cx = 0; cx < COLS; cx++) {
			const int cell = cy * COLS + cx;
			if (!baseline) {
				const size_t tag = hud_cell(pixels, cx, cy, map_row, scroll);
				if (plan) plan->cells[cell] = tag;
				if (!(occupied[cell] = tag != 0)) continue;
				artwork[cell] = tag - 1;
				continue;
			}
			const int x = (cx * 8 - scroll) & 0x1ff;
			const uint32_t code = m_tiles.tile_code(m_tiles.tile_r(map_row + x / 8));
			for (size_t i = 0; i < m_video->hud_tile_groups.size(); i++) {
				const auto &range = m_video->hud_tile_groups[i];
				if (code >= range.first && code <= range.last) artwork[cell] = i + 1;
			}
		}
	}
	if (baseline)
		for (int cy = first_row; cy < ROWS; cy++)
			for (int cx = 0; cx < COLS; cx++)
				occupied[cy * COLS + cx] = check_occupied(cx, cy);
	std::vector<uint32_t> source;
	std::vector<int> reusable_group;
	const int first_y = first_row * 8;
	for (int cell = first_row * COLS; cell < COLS * ROWS; cell++) {
		if (!occupied[cell]) continue;
		std::vector<int> fresh_group;
		auto &group = baseline ? fresh_group : reusable_group;
		group.clear();
		group.push_back(cell);
		occupied[cell] = false;
		const size_t artwork_group = artwork[cell];
		int left = SCREEN_W, right = 0;
		for (size_t i = 0; i < group.size(); i++) {
			const int cx = group[i] % COLS, cy = group[i] / COLS;
			left = std::min(left, cx * 8);
			right = std::max(right, cx * 8 + 7);
			// Join letters and nearby labels before choosing one anchor for the whole graphic.
			for (int y = std::max(first_row, cy - 2); y <= std::min(ROWS - 1, cy + 2); y++)
				for (int x = std::max(0, cx - 2); x <= std::min(COLS - 1, cx + 2); x++) {
					const int neighbor = y * COLS + x;
					if (!occupied[neighbor] || artwork[neighbor] != artwork_group) continue;
					occupied[neighbor] = false;
					group.push_back(neighbor);
				}
		}
		const int offset = int(hud_edge_offset(left, right));
		if (!offset) continue;
		if (plan) {
			for (int part : group) plan->moves[part / COLS].push_back({(part % COLS) * 8, offset, true});
			for (int part : group) plan->moves[part / COLS].push_back({(part % COLS) * 8, offset, false});
		}
		// Freeze the source before the first move; later groups can overlap moved pixels.
		if (source.empty()) {
			source.reserve(size_t(SCREEN_H - first_y) * SCREEN_W);
			for (int y = first_y; y < SCREEN_H; y++) {
				const uint32_t *row = &pixels.pix(y, m_ext);
				source.insert(source.end(), row, row + SCREEN_W);
			}
		}
		for (int part : group)
			for (int y = (part / COLS) * 8; y < (part / COLS + 1) * 8; y++)
				for (int x = (part % COLS) * 8; x < (part % COLS + 1) * 8; x++) {
					const uint32_t value = source[size_t(y - first_y) * SCREEN_W + x];
					if (value != KEY) pixels.pix(y, m_ext + x) = KEY;
				}
		for (int part : group)
			for (int y = (part / COLS) * 8; y < (part / COLS + 1) * 8; y++)
				for (int x = (part % COLS) * 8; x < (part % COLS + 1) * 8; x++) {
					const uint32_t value = source[size_t(y - first_y) * SCREEN_W + x];
					if (value != KEY) pixels.pix(y, m_ext + x + offset) = value;
				}
	}
}

#if defined(__ANDROID__) || defined(__APPLE__)
bool model1::rebuild_hud_bands(layer_cache &c, uint64_t bands)
{
	constexpr uint32_t KEY = 0xff000000;
	constexpr int ROWS = SCREEN_H / 8;
	const int first = !hud_edge_pinning_active() ? ROWS :
		(m_ram_b[0x1138] == 0x03 || m_ram_b[0x1138] == 0x07 ? 43 : 0);
	if (!bands) {
		android_frame_trace unchanged("M1 layer 1 partial bands=0");
		return true; // The changed map entries were all outside the visible rows.
	}
	auto &scratch = m_hud_band_scratch;
	const int width = c.px.width();
	if (scratch.width() != width) scratch.allocate(width, SCREEN_H);
	const uint16_t hscroll = m_tiles.tile_r(0x5000), vscroll = m_tiles.tile_r(0x5004);
	for (uint64_t pending = bands; pending; pending &= pending - 1) {
		const int band = __builtin_ctzll(pending);
		const int y0 = band * 8;
		std::fill_n(&scratch.pix(y0), width * 8, KEY);
		if (!m_layers_together) m_tiles.m_ox = m_ext;
		m_tiles.draw_layer1_band(scratch, band);
		if (!m_layers_together) m_tiles.m_ox = 0;
		if (band < first) continue;
		const int scroll = hscroll & 0x8000 ? m_tiles.tile_r(0x4000 + y0) : hscroll;
		const int map_row = (((y0 + vscroll) & 511) / 8) * 64;
		for (int cx = 0; cx < SCREEN_W / 8; cx++)
			if (hud_cell(scratch, cx, band, map_row, scroll) != m_hud_plan.cells[band * (SCREEN_W / 8) + cx]) {
				android_frame_trace fallback("M1 layer 1 partial group changed");
				return false;
			}
	}
#ifdef M1_ALWAYS_FRAME_TIMINGS
	char marker[48];
	snprintf(marker, sizeof marker, "M1 layer 1 partial bands=%d", __builtin_popcountll(bands));
	android_frame_trace partial(marker);
#endif
	// With the same cells and artwork groups, replay the original group order on each
	// dirty row. Read from untouched scratch pixels even where moved groups overlap.
	for (uint64_t pending = bands; pending; pending &= pending - 1) {
		const int band = __builtin_ctzll(pending);
		for (int y = band * 8; y < (band + 1) * 8; y++) {
			std::copy_n(&scratch.pix(y), width, &c.px.pix(y));
			for (const auto &move : m_hud_plan.moves[band])
				for (int x = move.x; x < move.x + 8; x++) {
					const uint32_t value = scratch.pix(y, m_ext + x);
					if (value != KEY) c.px.pix(y, m_ext + x + (move.clear ? 0 : move.offset)) = move.clear ? KEY : value;
				}
			scan_layer_row(c.px, y, c.x0[y], c.x1[y]);
		}
	}
	return true;
}
#endif

float model1::hud_edge_offset(float left, float right) const
{
	if (!hud_edge_pinning_active()) return 0;
	if (right < SCREEN_W / 2.0f - 32) return -float(m_ext);
	if (left > SCREEN_W / 2.0f + 32) return float(m_ext);
	return 0;
}

float model1::hud_view_offset(float x, float y, bool viewport) const
{
	if (hud_edge_pinning_active())
		for (const auto &view : viewport ? m_video->edge_viewports : m_video->edge_views)
			if (x == view.x && y == view.y) return float(view.edge * m_ext);
	return 0;
}

void model1::pin_hud_point(point_t *point, float offset) const
{
	point->fx += offset * m_view->scale;
	point->s.x = int32_t(point->fx);
}

// This frame's layers but the sky, each on a thread of its own (they don't depend on one another).
void model1::prepare_layers()
{
	if (m_scale == 1 && m_ext == 0) return;          // (the arcade's picture draws them straight in)
	static const int LAYERS[] = { 6, 2, 0, 7, 5, 3, 1 };
	int todo[7], n = 0;
	for (int l : LAYERS) {
		const int flags = l == 6 ? segas24_tile::DRAW_OPAQUE : 0;
		std::array<float, SCREEN_H> slide_rows;
		const double slide_time = smooth_tile_slide(l, slide_rows) ? m_draw_time : -1;
		if (layer_needs_redraw(l, flags, slide_time, current_layer_revision(l))) todo[n++] = l;
	}
	if (n < 2) return;
	android_frame_trace batch("M1 tile worker batch");
	m_work.layers += n;
	m_tiles.m_ox = m_ext;
	m_layers_together = true;
	parallel_jobs(n, [&](int i) { cached_layer(todo[i], todo[i] == 6 ? segas24_tile::DRAW_OPAQUE : 0); },
		"M1 tile worker wait");
	m_layers_together = false;
	m_tiles.m_ox = 0;
}

// Widescreen, for a game whose backdrops aren't stretched: an opaque layer's sides (where it has drawn
// nothing) take its edge's colour when that edge is all one colour (a plain backdrop), else black
// (art reaching the edge, kept 4:3). A sparse layer (SWA's starfield: at least 99% one colour) whose
// map, wrapping round its 512 columns, is as sparse beside the picture continues across the sides
// from the map instead, as it scrolls (`layer` given).
void model1::clip_scrolling_sides(bitmap_rgb32 &pixels, int layer, int flags)
{
	if (!m_ext) return;
	const uint32_t colour = flags & segas24_tile::DRAW_OPAQUE ? 0 : KEY;
	for (int y = 0; y < pixels.height(); y++)
		if (!extend_tile_row(layer, y)) {
			std::fill_n(&pixels.pix(y, 0), m_ext, colour);
			std::fill(&pixels.pix(y, m_ext + SCREEN_W), &pixels.pix(y, 0) + pixels.width(), colour);
		}
}

std::optional<uint32_t> model1::backdrop_colour(const bitmap_rgb32 &pixels, int x0, int x1, uint32_t colour)
{
	const int total = (x1 - x0) * SCREEN_H;
	const int required = total - total / 100;
	auto meets_threshold = [&](uint32_t candidate) {
		int matches = 0, misses = 0;
		for (int y = 0; y < SCREEN_H; y++) {
			const uint32_t *row = &pixels.pix(y);
			for (int x = x0; x < x1; x++) {
				if (row[x] == candidate) {
					if (++matches >= required) return true;
				} else if (++misses > total - required) return false;
			}
		}
		return false;
	};
	if (colour != KEY) return meets_threshold(colour) ? std::optional<uint32_t>(colour) : std::nullopt;
	uint32_t candidate = pixels.pix(0, x0);
	if (meets_threshold(candidate)) return candidate;
	// A qualifying colour is a strict majority, even if the first pixel was an outlier.
	int balance = 0;
	for (int y = 0; y < SCREEN_H; y++) {
		const uint32_t *row = &pixels.pix(y);
		for (int x = x0; x < x1; x++) {
			if (!balance) candidate = row[x];
			balance += row[x] == candidate ? 1 : -1;
		}
	}
	return meets_threshold(candidate) ? std::optional<uint32_t>(candidate) : std::nullopt;
}

void model1::fill_backdrop_sides(bitmap_rgb32 &pixels, int layer, int flags)
{
	constexpr uint32_t KEY = 0xff000000;
	if (!m_ext) return;
	if (repeat_tile_layer(layer)) return;
	if (extend_backdrops()) {
		if (layer >= 0 && m_gameplay.layer_sides)
			m_gameplay.layer_sides(layer, pixels.data(), pixels.width(), pixels.height(), m_ext);
		return;
	}
	if (!m_ext || !m_wide_frame || stretch_backdrops()) return;
	if (layer >= 0)
		if (const auto sky = backdrop_colour(pixels, m_ext, m_ext + SCREEN_W, KEY); sky && *sky != KEY) {
			if (m_wrap_tmp.width() != pixels.width()) m_wrap_tmp.allocate(pixels.width(), SCREEN_H);
			m_wrap_tmp.fill(KEY);
			const int ox = m_tiles.m_ox;
			m_tiles.m_ox = m_ext;
			m_tiles.draw_wide(m_wrap_tmp, layer, flags, m_ext);
			m_tiles.m_ox = ox;
			if (backdrop_colour(m_wrap_tmp, 0, m_ext, *sky) && backdrop_colour(m_wrap_tmp, m_ext + SCREEN_W, pixels.width(), *sky)) {
				for (int y = 0; y < SCREEN_H; y++) {
					std::copy_n(&m_wrap_tmp.pix(y, 0), m_ext, &pixels.pix(y, 0));
					std::copy_n(&m_wrap_tmp.pix(y, m_ext + SCREEN_W), pixels.width() - m_ext - SCREEN_W, &pixels.pix(y, m_ext + SCREEN_W));
				}
				return;
			}
		}
	// Plain bands down both edges, matching row for row (Wing War's sky with the black strip behind
	// its "Take control!" label): each row continues in its edge's colour.
	bool bands = true;
	for (int y = 0, run = 0; y < SCREEN_H && bands; y++, run++) {
		const uint32_t c = pixels.pix(y, m_ext);
		bands = c != KEY && pixels.pix(y, m_ext + SCREEN_W - 1) == c;
		if (bands && y && c != pixels.pix(y - 1, m_ext)) { bands = run >= 8; run = 0; }
		if (bands && y == SCREEN_H - 1) bands = run + 1 >= 8;
	}
	if (bands) {
		for (int y = 0; y < SCREEN_H; y++) {
			const uint32_t c = pixels.pix(y, m_ext);
			std::fill_n(&pixels.pix(y, 0), m_ext, c);
			std::fill(&pixels.pix(y, m_ext + SCREEN_W), &pixels.pix(y, 0) + pixels.width(), c);
		}
		return;
	}
	for (int x_edge : { m_ext, m_ext + SCREEN_W - 1 }) {
		const uint32_t first = pixels.pix(0, x_edge);
		bool plain = first != KEY;
		for (int y = 1; y < SCREEN_H && plain; y++) plain = pixels.pix(y, x_edge) == first;
		bool any = false;
		for (int y = 0; y < SCREEN_H && !any; y++) any = pixels.pix(y, x_edge) != KEY;
		if (!any) continue;                                   // nothing at this edge: nothing to continue
		const uint32_t c = plain ? first : 0;
		const int x0 = x_edge == m_ext ? 0 : m_ext + SCREEN_W, x1 = x_edge == m_ext ? m_ext : pixels.width();
		for (int y = 0; y < SCREEN_H; y++)
			for (int x = x0; x < x1; x++) pixels.pix(y, x) = c;
	}
}

// The 2D layers are drawn at the arcade's resolution and scaled up to the render's.
void model1::draw_tiles(bitmap_rgb32 &bitmap, const rectangle &cliprect, int layer, int flags)
{
	if (m_free_cam && layer != 4 && layer != 6) return;
	static const int skip = getenv("M1_TILE_SKIP") ? strtol(getenv("M1_TILE_SKIP"), nullptr, 16) : 0;
	if (skip & (1 << layer)) return;
	std::array<float, SCREEN_H> slide_rows;
	const bool smooth_slide = smooth_tile_slide(layer, slide_rows);
	const bool slide_motion = m_tile_slide_interpolating && m_tile_slide_layer == layer;
	if (m_scale == 1 && m_ext == 0 && !smooth_slide && !slide_motion && !(m_free_cam && layer == 4)) {
		m_tiles.m_ox = 0;
		m_tiles.draw(bitmap, cliprect, layer, 0, flags);
		return;
	}
	constexpr uint32_t KEY = 0xff000000;     // pixels are 0x00rrggbb: never the key
	bitmap_rgb32 *tmp = &m_tile_tmp;
	const int16_t *rx0 = nullptr, *rx1 = nullptr;    // each row's drawn span (cached layers)
	const layer_cache *cache = nullptr;
	bool sky = false, sides = false;
	if (layer == 4 && (flags & segas24_tile::DRAW_OPAQUE)) {
		// the sky layer: note where each pixel came from, for a hi-res replacement (drawn once a game
		// frame; which panorama and where, every time)
		if (m_skies.empty()) init_skies();
		sky = !m_skies.empty() && m_wide_frame;
		if (m_l4_frame != m_frame || m_l4_ext != m_ext || m_l4_coords != sky || m_l4_wide != m_wide_frame ||
			m_tile_tmp.width() != SCREEN_W + 2 * m_ext) {
			m_tile_tmp.fill(KEY);
			m_tiles.m_ox = m_ext;
			if (sky) {
				m_tile_coord.assign(size_t(m_tile_tmp.width()) * m_tile_tmp.height(), ~0u);
				m_tiles.m_coord = m_tile_coord.data();
			}
			if (extend_backdrops() || repeat_tile_layer(layer)) m_tiles.draw_wide(m_tile_tmp, layer, flags, m_ext);
			else m_tiles.draw(m_tile_tmp, m_tile_tmp.cliprect(), layer, 0, flags);
			m_tiles.m_coord = nullptr;
			m_tiles.m_ox = 0;
			fill_backdrop_sides(m_tile_tmp, layer, flags);
			clip_scrolling_sides(m_tile_tmp, layer, flags);
			m_l4_frame = m_frame; m_l4_ext = m_ext; m_l4_coords = sky; m_l4_wide = m_wide_frame; m_l4_gen++;
		}
		if (sky) find_sky();
	} else {
		layer_cache &c = cached_layer(layer, flags);
		tmp = &c.px; rx0 = c.x0.data(); rx1 = c.x1.data(); sides = c.sides; cache = &c;
	}
	// Widescreen: the opaque layers are the game's backdrop (VF's sky), which only holds the
	// visible part of its panorama; stretch it across the width. Other layers retain their scale;
	// the game's edge-anchored HUD has already been positioned in cached_layer.
	const bool stretch = m_ext && m_wide_frame && (flags & segas24_tile::DRAW_OPAQUE) && stretch_backdrops();
	if (flags & segas24_tile::DRAW_OPAQUE) sides = true;   // (its sides are its own: stretched or filled)
	const int s = m_scale;
	const int out_w = bitmap.width();
	if (slide_motion) {
		auto fdiv = [](int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); };
		parallel_rows(SCREEN_H * s, [&](int Y0, int Y1) {
			for (int Y = Y0; Y < Y1; Y++) {
				const int y = Y / s, ox = sky_shift_x(-m_tile_slide_residual[y], s);
				const uint32_t *src = &tmp->pix(y);
				uint32_t *dst = &bitmap.pix(Y);
				for (int X = 0; X < out_w; X++) {
					const int x = fdiv(X + ox, s);
					if (x < 0 || x >= tmp->width()) continue;
					const uint32_t c = src[x];
					if (c != KEY) dst[X] = blend_layer_pixel(c, dst[X]);
				}
			}
		});
		return;
	}
	// smooth motion: the sky between two game frames, moved by the blended scroll at full resolution
	float sdx = 0, sdy = 0;
	if (layer == 4 && (flags & segas24_tile::DRAW_OPAQUE)) {
		smooth_sky_shift(sdx, sdy);
		if (m_free_cam) sdx += m_free_cam_pose.yaw * (2048.0f / 6.283185307179586f);
	}
	// a hi-res sky draws over every row of this layer: nothing to draw beneath it
	if (sky && m_sky_now) { draw_sky(bitmap, sdx, sdy); return; }
	// the tile layer itself moves no more than 24 pixels (its padding is 32: more showed its clamped
	// edge); a panorama wraps round, so it takes the whole move
	if (std::fabs(sdx) > 24 || std::fabs(sdy) > 24) sdx = sdy = 0;
	if (m_parts) { parts_layer(layer, flags, *tmp, stretch, sdx, sdy); return; }   // the GPU enlarges it
	if (sdx != 0 || sdy != 0) {
		const int oy = sky_shift_y(sdy, s), ox = sky_shift_x(sdx, s);
		const int x0 = stretch ? 0 : m_ext * s, x1 = stretch ? out_w : (m_ext + SCREEN_W) * s;
		auto fdiv = [](int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); };
		m_col_src.resize(out_w);
		for (int X = x0; X < x1; X++) {
			const int lx = stretch ? m_ext + int(std::floor((X + 0.5f) * SCREEN_W / out_w + sdx)) : fdiv(X + ox, s);
			m_col_src[X] = std::clamp(lx, m_ext, m_ext + SCREEN_W - 1);
		}
		parallel_rows(SCREEN_H * s, [&](int Y0, int Y1) {
			for (int Y = Y0; Y < Y1; Y++) {
				const int ly = std::clamp(fdiv(Y + oy, s), 0, SCREEN_H - 1);
				if (rx0 && rx0[ly] >= rx1[ly]) continue;   // nothing on this row
				const uint32_t *src = &tmp->pix(ly);
				uint32_t *d = &bitmap.pix(Y);
				for (int X = x0; X < x1; X++) {
					const uint32_t c = src[m_col_src[X]];
					if (c != KEY) d[X] = c;
				}
			}
		});
		return;
	}
	parallel_rows(SCREEN_H, [&](int ya, int yb) {
	for (int y = ya; y < yb; y++) {
		if (rx0 && rx0[y] >= rx1[y]) continue;     // nothing on this row
		const uint32_t *src = &tmp->pix(y);
		if (stretch) {
			uint32_t *d0 = &bitmap.pix(y * s);
			for (int x = 0; x < out_w; x++) {
				uint32_t c = src[m_ext + int((x + 0.5f) * SCREEN_W / out_w)];
				if (c != KEY) d0[x] = c;
			}
			for (int dy = 1; dy < s; dy++)
				memcpy(&bitmap.pix(y * s + dy), d0, out_w * 4);
			continue;
		}
		const int xa = rx0 ? rx0[y] : 0, xb = rx0 ? rx1[y] : tmp->width();
		for (int x = xa; x < xb; x++) {
			uint32_t c = src[x];
			if (c == KEY) continue;
			for (int dy = 0; dy < s; dy++) {
				uint32_t *d = &bitmap.pix(y * s + dy, x * s);
				for (int dx = 0; dx < s; dx++) d[dx] = blend_layer_pixel(c, d[dx]);
			}
		}
		// Widescreen: a curtain row carried out across the sides (see find_curtains).
		if (cache && !sides && cache->curtain[y])
			for (int dy = 0; dy < s; dy++) {
				uint32_t *d = &bitmap.pix(y * s + dy);
				for (int x = 0; x < m_ext; x++)
					std::fill_n(d + x * s, s, curtain_pixel(*cache, y, x));
				for (int x = m_ext + SCREEN_W; x < tmp->width(); x++)
					std::fill_n(d + x * s, std::min(s, bitmap.width() - x * s), curtain_pixel(*cache, y, x));
			}
	}
	});
}

// ---- screen: MAME's screen_update_model1 ----

bool model1::let_machine_go()
{
	if (!machine_free || !m_parts || m_parts_whole || m_redrawing || m_tile_slide_interpolating) return false;
	m_last_quads = m_frame_quads;
	if (m_picture_tail) {
		auto tail = std::move(m_picture_tail);
		m_picture_tail = nullptr;
		tail();
	}
	auto go = std::move(machine_free);
	machine_free = nullptr;
	go();
	return true;
}

void model1::screen_update(bitmap_rgb32 &bitmap, const rectangle &cliprect)
{
#ifdef M1_ALWAYS_FRAME_TIMINGS
	work_timer screen_time{m_work.screen_ns};
#endif
	view_t *view = m_view.get();
	view->ayyc = cos(view->ayy);
	view->ayys = sin(view->ayy);

	if (m_tiles_updated != m_frame) {                // (once per game frame)
		android_frame_trace decode("M1 tile decode");
		const uint64_t t0 = trace_ns();
		m_work.tiles_full += m_tiles.all_to_decode();
		m_tiles.update(m_pens);
		m_tiles_updated = m_frame;
		m_work.tiles_ns += trace_ns() - t0;
	}
	if (m_parts) parts_begin(bitmap);                // the GPU fills it, and the 3D goes over nothing
	else { bitmap.fill(m_pens[0], cliprect); m_bitmap_whole = true; }
	static const bool log_scroll = getenv("M1_LOG_SCROLL") != nullptr;
	if (log_scroll)
		fprintf(stderr, "frame %llu tm2 h %04x v %04x tm3 h %04x v %04x ctrl %04x %04x\n", (unsigned long long)m_frame,
				m_tiles.tile_r(0x5002), m_tiles.tile_r(0x5006), m_tiles.tile_r(0x5003), m_tiles.tile_r(0x5007),
				m_tiles.tile_r(0x5004), m_tiles.tile_r(0x5006));
	const uint32_t camera = this->camera();
	// A screen with painted sides, known by its backdrop: 4:3 from its first frame. (Between scenes the
	// camera reads none, and the last one's kept: a fight's, for a few frames into the select.)
	const screen_art *known = nullptr;
	const uint32_t h = m_ext && m_gameplay.screen_name ? backdrop_hash() : 0;
	if (m_ext && !m_screens.empty()) {
		const auto name = m_gameplay.screen_name ? m_gameplay.screen_name(h) : std::nullopt;
		if (name)
			for (const auto &a : m_screens)
				if (a.name == *name) { known = &a; break; }
	}
	if (m_ext) {
		if (m_gameplay.wide_scene) m_camera_wide = m_gameplay.wide_scene(camera, m_camera_wide);
		if (known && !camera) m_camera_wide = false;
		else if (!known && !camera && !m_camera_wide && sky_identity() >= 0) m_camera_wide = true;
		bool wide = m_camera_wide;
		static const bool log_wide = getenv("M1_LOG_WIDE") != nullptr;
		if (log_wide && wide != m_wide_frame)
			fprintf(stderr, "frame %llu: %s\n", (unsigned long long)m_frame, wide ? "wide" : "4:3");
		m_wide_frame = wide;
	}

	static const bool log_backdrop = getenv("M1_LOG_BACKDROP") != nullptr;
	if (log_backdrop) {
		static uint32_t last = 0;
		const uint32_t backdrop = backdrop_hash();
		if (backdrop != last)
			fprintf(stderr, "frame %llu backdrop %08x scroll %04x %04x cam %08x\n", (unsigned long long)m_frame, backdrop,
					m_tiles.tile_r(0x5002), m_tiles.tile_r(0x5006), this->camera());
		last = backdrop;
	}
	m_screen_now = m_wide_frame ? nullptr : known;
	const auto layers = [&] {
		const uint64_t t0 = trace_ns();
		prepare_layers();
		m_work.layers_ns += trace_ns() - t0;
	};
	static const bool no_polys = getenv("M1_NO_POLYS") != nullptr;   // debug: the 2D layers alone
	const bool g3 = gpu3d_on() && !no_polys;   // the 3D on the GPU (gpu3d_end), else the CPU's
	const auto layers_below = [&] {
		draw_tiles(bitmap, cliprect, 6, segas24_tile::DRAW_OPAQUE);
		draw_tiles(bitmap, cliprect, 4, segas24_tile::DRAW_OPAQUE);
		if (m_screen_now && m_parts) {
			// its painted sides, as an image of their own over the backdrop (KEY between them)
			if (m_sides.width() != bitmap.width() || m_sides.height() != bitmap.height()) {
				m_sides.allocate(bitmap.width(), bitmap.height());
				m_sides.fill(0xff000000);
				m_sides_art = nullptr;
			}
			draw_screen_sides(m_sides);
			m_parts->img[TEX_SIDES] = { &m_sides.pix(0), m_sides.width(), m_sides.height(), size_t(m_sides.width()), m_sides_gen };
			gpu_op o; o.tex = TEX_SIDES; o.sw = o.dw = float(m_sides.width()); o.sh = o.dh = float(m_sides.height());
			m_parts->ops.push_back(o);
		} else if (m_screen_now) draw_screen_sides(bitmap);
		if (m_parts) {
			if (!m_free_cam) {
				parts_composite(m_mid, m_mid_key, m_mid_gen, { 2, 0 });
				m_parts->img[TEX_MID] = { &m_mid.pix(0), m_mid.width(), m_mid.height(), size_t(m_mid.width()), m_mid_gen };
				gpu_op mid; mid.tex = TEX_MID; mid.sw = float(m_mid.width()); mid.sh = SCREEN_H; mid.dw = float(m_mid.width() * m_scale); mid.dh = float(SCREEN_H * m_scale);
				m_parts->ops.push_back(mid);
			}
			gpu_op o;
			o.tex = TEX_3D; o.sw = o.dw = float(bitmap.width()); o.sh = o.dh = float(bitmap.height());
			m_parts->ops.push_back(o);                   // (drawn below, before the host puts it on)
			if (m_screen_now) {                          // the painted sides again, over the 3D (see below)
				gpu_op s; s.tex = TEX_SIDES; s.sw = s.dw = float(m_sides.width()); s.sh = s.dh = float(m_sides.height());
				m_parts->ops.push_back(s);
			}
		} else {
			draw_tiles(bitmap, cliprect, 2, 0);
			draw_tiles(bitmap, cliprect, 0, 0);
		}
	};
	// In parts, with the 3D recorded for the GPU, the 2D layers (drawn again where changed, their
	// images and the parts' list up to the 3D) and the HUD's are made on another thread while the 3D
	// is walked: the walk reads only the game's memory, and adds to the parts nothing before gpu3d_end.
	static const bool no_overlap = getenv("M1_NO_LAYER_OVERLAP") != nullptr;
	// (only beside a walk long enough to hide them: without, handing them over costs more than it saves)
	const bool overlap = m_parts && g3 && !no_overlap && m_walk_ns >= 1500000;
	if (overlap)
		m_layers_job.start([&] {
			android_frame_trace trace("M1 2D beside the 3D");
			layers();
			layers_below();
			if (!m_free_cam) parts_composite(m_top, m_top_key, m_top_gen, { 7, 5, 3, 1 });   // (taken as made, below)
		});
	else { layers(); layers_below(); }

	m_above_hud_seen = false;
	m_frame_quads = 0;
	// (Low antialiasing, a picture drawn whole: what's under the 3D marked, to tell the 3D's pixels by)
	if (m_edge_aa && !m_parts && !no_polys)
		parallel_rows(bitmap.height(), [&](int a, int b) {
			for (int y = a; y < b; y++) { uint32_t *p = &bitmap.pix(y); for (int x = 0; x < bitmap.width(); x++) p[x] |= UNDER_3D; }
		});
	// A picture's painted sides stay over the 3D: what it draws beyond the arcade's 4:3 was off the
	// cabinet's screen (a boss's empty select portrait, in Cheats' Akira's place, leaves its sky
	// there). Put together on the GPU, the sides go on again above the 3D (above); else kept here.
	std::vector<uint32_t> sides_kept;
	const int side_l = m_ext * m_scale, side_r = std::min((m_ext + SCREEN_W) * m_scale, bitmap.width());
	if (m_screen_now && !m_parts && side_l > 0) {
		const int w = bitmap.width();
		sides_kept.resize(size_t(cliprect.max_y - cliprect.min_y + 1) * (side_l + w - side_r));
		uint32_t *k = sides_kept.data();
		for (int y = cliprect.min_y; y <= cliprect.max_y; y++) {
			const uint32_t *p = &bitmap.pix(y);
			k = std::copy(p, p + side_l, k);
			k = std::copy(p + side_r, p + w, k);
		}
	}
	m_raster_ms = 0;
	m_picture_frame = m_frame;
	bool released = false;            // the game's next frame may be running: its state isn't read again
	bool black_sides = false;
	{
		android_frame_trace trace("M1 prepare 3D");
#ifdef M1_ALWAYS_FRAME_TIMINGS
		work_timer tgp_time{m_work.scene_3d_ns};
#endif
		if (g3) gpu3d_begin(bitmap);
		const uint64_t walk0 = trace_ns();
		if (!no_polys) tgp_render(bitmap, cliprect, RENDER_BELOW_HUD);
		m_walk_ns = trace_ns() - walk0;
		if (overlap) m_layers_job.wait("M1 2D wait");
		// In parts the HUD's image is made here, before the game's next frame may start (below):
		// the layers are made from the tile RAM it writes. (Its place in the parts' list is the same.)
		if (m_parts && !m_free_cam) {
			parts_composite(m_top, m_top_key, m_top_gen, { 7, 5, 3, 1 });
			m_parts->img[TEX_TOP] = { &m_top.pix(0), m_top.width(), m_top.height(), size_t(m_top.width()), m_top_gen };
			gpu_op o; o.tex = TEX_TOP; o.sw = float(m_top.width()); o.sh = SCREEN_H; o.dw = float(m_top.width() * m_scale); o.dh = float(SCREEN_H * m_scale);
			m_parts->ops.push_back(o);
		}
		black_sides = m_ext && !m_wide_frame && !m_screen_now && !extend_backdrops();
		// nothing over the HUD: the rest reads only the picture's own
		if (!m_above_hud_seen) released = let_machine_go();
		if (g3) gpu3d_end(bitmap);
		else { m_g3.gpu = false; m_g3.ms = m_raster_ms; m_g3.gpu_ms = 0; }
	}
	clear_moire_marks(bitmap, cliprect);
	if (m_edge_aa && !no_polys && m_parts && m_parts->d3_target >= 0) m_parts->d3_edge = true;
	if (m_edge_aa && !no_polys && !(m_parts && m_parts->d3_edge)) {
		const uint64_t t0 = trace_ns();
		edge_smooth(bitmap, m_parts != nullptr);
		m_work.edge_ns += trace_ns() - t0;
	}
	if (!sides_kept.empty()) {
		const int w = bitmap.width();
		const uint32_t *k = sides_kept.data();
		for (int y = cliprect.min_y; y <= cliprect.max_y; y++) {
			uint32_t *p = &bitmap.pix(y);
			std::copy(k, k + side_l, p); k += side_l;
			std::copy(k, k + (w - side_r), p + side_r); k += w - side_r;
		}
	}

	if (m_parts) {
		if (m_above_hud_seen && title().name != "swa") m_parts_whole = true;
	} else {
		draw_tiles(bitmap, cliprect, 7, 0);
		draw_tiles(bitmap, cliprect, 5, 0);
		draw_tiles(bitmap, cliprect, 3, 0);
		draw_tiles(bitmap, cliprect, 1, 0);
	}

	// The above-HUD pass and its stencil copy the whole frame twice: only when there is something
	// to draw there (never in VF; it is for the other Model 1 games' radar blips).
	if (m_above_hud_seen && !no_polys) {
		android_frame_trace trace("M1 prepare 3D overlay");
#ifdef M1_ALWAYS_FRAME_TIMINGS
		work_timer tgp_time{m_work.scene_3d_ns};
#endif
		if (m_parts && title().name == "swa") {
			if (m_overlay.width() != bitmap.width() || m_overlay.height() != bitmap.height())
				m_overlay.allocate(bitmap.width(), bitmap.height());
			{
				android_frame_trace clear("M1 overlay clear");
				m_overlay.fill(KEY);
			}
			m_overlay_bounds = rectangle(0, -1, 0, -1);
			m_overlay_separate = true;
			{
				android_frame_trace draw("M1 overlay draw");
				tgp_render(m_overlay, cliprect, RENDER_ABOVE_HUD);
			}
			m_overlay_separate = false;
			if (!m_parts_whole) add_overlay_parts(cliprect);
			if (!released) released = let_machine_go();
		} else {
			build_overlay_mask(bitmap, cliprect);
			tgp_render(bitmap, cliprect, RENDER_ABOVE_HUD);
			apply_overlay_stencil(bitmap, cliprect);
		}
	}
	clear_moire_marks(bitmap, cliprect);
	if (!released) m_last_quads = m_frame_quads;
	if (m_parts && !black_sides) parts_end(bitmap);
	if (black_sides) {   // 4:3 screens without a picture's sides: black
		const int l = m_ext * m_scale, r = (m_ext + SCREEN_W) * m_scale;
		if (m_parts) {
			gpu_op o;
			o.dw = float(l); o.dh = float(bitmap.height());
			m_parts->ops.push_back(o);
			o.dx = float(r); o.dw = float(bitmap.width() - r);
			m_parts->ops.push_back(o);
			parts_end(bitmap);
			return;
		}
		for (int y = cliprect.min_y; y <= cliprect.max_y; y++) {
			uint32_t *p = &bitmap.pix(y);
			std::fill(p, p + l, 0);
			std::fill(p + r, p + bitmap.width(), 0);
		}
	}
}


// Above 1x shadows mark the pixels they shade (see draw_hline_moired); drop the marks.
void model1::clear_moire_marks(bitmap_rgb32 &bitmap, const rectangle &cliprect)
{
	if (!s_moire_blend) return;
	const int y1 = std::max(cliprect.min_y, s_mark_y1), y2 = std::min(cliprect.max_y, s_mark_y2);
	s_mark_y1 = 1 << 30;
	s_mark_y2 = -1;
	for (int y = y1; y <= y2; y++) {
		uint32_t *p = &bitmap.pix(y);
		for (int x = cliprect.min_x; x <= cliprect.max_x; x++) {
			if (p[x] == KEY) continue;              // (KEY: nothing drawn there, in the GPU's parts)
			if ((p[x] >> 24) == SHADOW_KEY >> 24) {   // a shadow over the 2D: blended with it now
				p[x] = ((parts_below(x, y) >> 1) & 0x7f7f7f) + (p[x] & 0x7f7f7f);
				continue;
			}
			p[x] &= 0xffffff | UNDER_3D;              // (Low antialiasing's mark stays until it's read)
		}
	}
}

// A fingerprint of the backdrop map (the tile map layers 4 and 5 draw): which full-screen picture
// is up (title, player select, ranking), whatever its palette is doing.
uint32_t model1::backdrop_hash() const
{
	auto entry = [&](uint32_t o) -> uint16_t {
		if (m_gameplay.backdrop_tile)
			if (auto tile = m_gameplay.backdrop_tile(o)) return *tile;
		return m_tiles.tile_r(o);
	};
	uint32_t h = 2166136261u;
	for (int i = 0; i < 0x1000; i++) h = (h ^ entry(0x2000 + i)) * 16777619u;
	return h;
}
