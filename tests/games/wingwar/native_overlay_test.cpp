#include "../../../src/games/wingwar/profile.h"
#include "../../../src/games/wingwar/gameplay.h"
#include "../../../src/games/script.h"
#include "../../../src/host/regions.h"
#include "../../../src/rt/native.h"
#include "../../../src/host/input_replay.h"
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <cstdio>

struct model1_native_test
{
	static void lists(const model1 &a, const model1 &b)
	{
		assert(!memcmp(a.m_display_list0, b.m_display_list0, 0x10000));
		assert(!memcmp(a.m_display_list1, b.m_display_list1, 0x10000));
	}
};

namespace {

model1::regions synthetic_regions()
{
	model1::regions r;
	r.title = game::wingwar::ID;
	r.maincpu.resize(0x2000000);
	r.maincpu[0xfffff0] = 0xca;
	return r;
}

void guarded_versions()
{
	model1 m(synthetic_regions());
	m.reset();
	const uint32_t base = 0x40d000;
	const std::array<uint8_t, 8> original{0x2d, 0x20, 0xe1, 0x6a, 0xfd, 0, 0, 0};
	for (size_t i = 0; i < original.size(); i++) m.write8(base + i, original[i]);
	m.cpu().set_reg(32, base);
	rec::context runtime;
	runtime.attach(m.cpu(), game::wingwar::ID.name);
	rec::hints h;
	h.ram.push_back({base, base + 16});
	rec::native_stats st;
	assert(rec::build_native(m.maincpu_rom(), h, &st));
	assert(st.overlays);
	runtime.run(8);
	assert(m.cpu().reg(0) == 1 && m.cpu().pc() == base + 3);
	// Replace the next instruction while its old native caller is suspended at the slice boundary.
	const std::array<uint8_t, 5> replacement{0x2d, 0x21, 0xe2, 0x6a, 0xfa};
	for (size_t i = 0; i < replacement.size(); i++) m.write8(base + 3 + i, replacement[i]);
	runtime.run(8);
	assert(m.cpu().reg(1) == 2 && m.cpu().pc() == base + 6);
	assert(st.overlays->stale_instructions > 0 && st.overlays->compilations == 2);
	runtime.run(8);
	assert(m.cpu().pc() == base);
	for (size_t i = 0; i < original.size(); i++) m.write8(base + i, original[i]);
	runtime.run(8);
	assert(m.cpu().reg(0) == 1 && m.cpu().pc() == base + 3);
	runtime.attach(m.cpu(), game::wingwar::ID.name);
	m.cpu().set_reg(32, base);
	runtime.run(8);
	assert(m.cpu().pc() == base + 3 && !rec::fallback_count());
	assert(st.overlays->cache_hits > 0);
	puts("Native RAM guards: suspended instruction replacement, retained tails, cached versions and stack reset passed");
}

void context_isolation()
{
	model1 a(synthetic_regions()), b(synthetic_regions());
	a.reset();
	b.reset();
	rec::context first, second;
	rec::native_stats one, two;
	rec::hints h;
	h.ram.push_back({0x40d000, 0x40d010});
	for (auto *m : {&a, &b}) {
		m->write8(0x40d000, 0x2d);
		m->write8(0x40d001, 0x20);
		m->write8(0x40d002, m == &a ? 0xe1 : 0xe2);
		m->write8(0x40d003, 0x6a);
		m->write8(0x40d004, 0xfd);
		m->cpu().set_reg(32, 0x40d000);
	}
	first.attach(a.cpu(), game::wingwar::ID.name);
	assert(rec::build_native(a.maincpu_rom(), h, &one));
	second.attach(b.cpu(), game::wingwar::ID.name);
	assert(rec::build_native(b.maincpu_rom(), h, &two));
	for (int i = 0; i < 4; i++) {
		first.run(8);
		second.run(8);
		assert(a.cpu().reg(0) == 1 && b.cpu().reg(0) == 2);
	}
	a.write8(0x40d002, 0xe3);
	first.run(8);
	second.run(8);
	assert(a.cpu().reg(0) == 3 && b.cpu().reg(0) == 2);
	assert(one.overlays != two.overlays && one.overlays->compilations > two.overlays->compilations);
	rec::hints invalid;
	invalid.ram = {{0x410000, 0x410010}};
	assert(!rec::build_native(b.maincpu_rom(), invalid));
	invalid.ram = {{0x40d000, 0x40d010}, {0x40d008, 0x40d018}};
	assert(!rec::build_native(b.maincpu_rom(), invalid));
	second.run(8);
	assert(b.cpu().reg(0) == 2 && !rec::fallback_count());
	puts("Native RAM guards: independent contexts with different bytes at the same PC passed");
}

void invincibility(const char *regions, const char *map)
{
	for (bool native : {false, true}) {
		model1 m(load_regions(regions, game::wingwar::ID));
		m.reset();
		rec::context runtime;
		if (native) {
			runtime.attach(m.cpu(), game::wingwar::ID.name);
			rec::hints hints;
			assert(hints.read(map));
			hints.entries.insert(0x1f13e);
			assert(rec::build_native(m.maincpu_rom(), hints));
		}
		for (uint32_t local : {0x40b400u, 0x40b800u})
		for (uint32_t player : {0x40b400u, 0x40b800u, 0x40be00u})
			for (bool enabled : {false, true})
				for (uint8_t health : {1, 10}) {
					if (native) runtime.attach(m.cpu(), game::wingwar::ID.name);
					assert(m.set_cheats(enabled ? game::wingwar::INVINCIBILITY : 0));
					m.write32(0x40f32c, local);
					for (uint32_t offset = 0; offset < 0x100; offset += 4) m.write32(player + offset, 0);
					m.write8(0x40f308, 0);
					m.write8(0x40f309, 0x5a);
					m.write8(0x40f302, 1);
					m.write32(player + 0x50, 0);
					m.write8(player + 0x56, health);
					m.write8(player + 0x99, 0);
					m.write8(player + 0x94, 0x21);
					m.write8(player + 0x95, 0x43);
					m.write8(player + 0x96, 0x65);
					m.write32(player + 0x20, player == local ? 0x162ed : 0x18e73);
					m.write32(player + 0x8c, 0x22092);
					m.write16(player + 0x4c, 2);
					m.write16(0x502b04, 0x101);
					m.cpu().set_reg(24, player);
					m.cpu().set_reg(25, 0x408000);
					m.cpu().set_reg(1, 1);
					m.cpu().set_reg(2, 0);
					m.cpu().set_reg(3, 0);
					m.cpu().set_reg(32, game::wingwar::HIT_EVENT_READ + 8);
					assert(m.read8(0x40f302) == 1);
					m.cpu().set_reg(32, 0x1f13e);
					assert(m.read8(0x40f309) == 0x5a);
					int steps = 0;
					while (m.cpu().pc() != 0x1f28d && m.cpu().pc() != 0x1f28c && steps++ < 1000) {
						if (native) runtime.run(1);
						else m.cpu().run(1);
					}
					assert(steps < 1000);
					const bool protected_player = enabled && player == local;
					assert(m.read8(player + 0x56) == (protected_player ? health : health - 1));
					assert((m.read32(player + 0x50) & 2) == ((!protected_player && health == 1) ? 2u : 0u));
					assert(m.read8(player + 0x94) == 0x21 && m.read8(player + 0x95) == 0x43 &&
						m.read8(player + 0x96) == 0x65);
					assert(m.read32(player + 0x20) == (player == local ? 0x162edu : 0x18e73u));
					assert(m.read32(player + 0x8c) == ((!protected_player && health == 1) ? 0x284e8u : 0x22092u));
					assert(m.read8(0x40f308) == 0);
					assert(m.read16(0x502b04) == 0);
					if (protected_player) {
						assert(m.read8(player + 0x59) == 0);
						assert(m.read8(player + 0x99) == 0);
						assert(!(m.read32(player + 0x50) & 0x3fe0));
					}
					assert(!native || !rec::fallback_count());
					if (player == local) {
						if (native) runtime.attach(m.cpu(), game::wingwar::ID.name);
						m.write8(0x40f0b0, 0x12);
						m.write8(0x40f0b1, 0x34);
						m.write8(0x40f0b2, 0x56);
						m.cpu().set_reg(32, 0x162ed);
						steps = 0;
						while (m.cpu().pc() != 0x1630a && steps++ < 100) {
							if (native) runtime.run(1);
							else m.cpu().run(1);
						}
						assert(steps < 100);
						assert(m.read8(player + 0x94) == 0x12 && m.read8(player + 0x95) == 0x34 &&
							m.read8(player + 0x96) == 0x56);
						assert(!native || !rec::fallback_count());
					}
				}
		m.reset();
		assert(m.cheats() == 0 && !m.set_cheats(4));
	}
	puts("Invincibility: local pilot only, CPU damage, lethal hits when off and live flight controls, interpreter/native passed");
}

void hit_recovery(const char *regions, const char *map)
{
	for (bool native : {false, true}) {
		model1 m(load_regions(regions, game::wingwar::ID));
		m.reset();
		rec::context runtime;
		if (native) {
			runtime.attach(m.cpu(), game::wingwar::ID.name);
			rec::hints hints;
			assert(hints.read(map));
			for (uint32_t pc : {0x1f13eu, 0x1f157u, 0x2bf5du}) hints.entries.insert(pc);
			assert(rec::build_native(m.maincpu_rom(), hints));
		}
		auto run_to = [&](uint32_t start, uint32_t end) {
			if (native) runtime.attach(m.cpu(), game::wingwar::ID.name);
			m.cpu().set_reg(32, start);
			int steps = 0;
			while (m.cpu().pc() != end && steps++ < 1000) {
				if (native) runtime.run(1);
				else m.cpu().run(1);
			}
			assert(steps < 1000 && (!native || !rec::fallback_count()));
		};
		for (uint32_t local : {0x40b400u, 0x40b800u})
		for (uint32_t player : {0x40b400u, 0x40b800u})
		for (uint32_t cheats : {0u, 1u, 2u, 3u})
		for (uint32_t pending : {0x2a9b5u, 0x2a9f7u, 0x2aa38u, 0x2ab62u,
			0x2c211u, 0x2c24fu, 0x2c285u, 0x2c2c2u, 0x2c382u, 0x2c1a4u}) {
			assert(m.set_cheats(cheats));
			m.write32(0x40f32c, local);
			for (uint32_t offset = 0; offset < 0x110; offset += 4) m.write32(player + offset, 0);
			m.write16(player + 0x4c, player == 0x40b400 ? 0 : 2);
			const uint32_t latch = 0x502b00 + 2 * m.read16(player + 0x4c);
			m.write32(player + 0x50, pending == 0x2c1a4 ? 0x410000 : 0x412000);
			m.write8(player + 0x56, 32);
			m.write32(player + 0xf8, pending);
			m.write16(player + 0xf6, 12);
			for (uint32_t offset = 0x70; offset <= 0x74; offset++) m.write8(player + offset, 0x55);
			m.write8(player + 0x9a, pending == 0x2c1a4 ? 0 : 0x6f);
			m.write8(player + 0x9b, pending == 0x2c1a4 ? 0 : 16);
			m.write32(player + 0x08, 0x3f800000);
			m.write32(player + 0x30, 0x40000000);
			m.write16(player + 0xbc, 0x1234);
			m.write32(player + 0xc0, 0x40400000);
			m.cpu().set_reg(24, player);
			m.cpu().set_reg(25, 0x408000);
			const bool protected_player = (cheats & game::wingwar::INVINCIBILITY) && player == local;
			// A zero event must also release a reaction that started before the toggle.
			for (uint16_t event : {0, 0x101, 0x102, 0x103}) {
				for (int repeat = 0; repeat < 80; repeat++) {
					m.write16(latch, event);
					run_to(0x1f13e, 0x1f157);
					assert(m.read16(latch) == 0);
					assert((m.cpu().reg(1) & 0xffff) == (protected_player ? 0 : event));
					assert(m.read32(player + 0x08) == 0x3f800000);
					assert(m.read32(player + 0x30) == 0x40000000);
					assert(m.read16(player + 0xbc) == 0x1234);
					assert(m.read32(player + 0xc0) == 0x40400000);
					if (protected_player) {
						run_to(0x1f157, 0x1f2a3);
						assert(m.read8(player + 0x56) == 32);
						assert(!(m.read32(player + 0x50) & 0x3fe0));
						assert(m.read8(player + 0x99) == 0);
						const uint32_t base = pending < 0x2c000 ? 0x2a90a : 0x2c1a4;
						assert(m.read32(player + 0xf8) == base);
						if (pending != 0x2c1a4) {
							assert(m.read16(player + 0xf6) == 0);
							for (uint32_t offset = 0x70; offset <= 0x74; offset++)
								assert(m.read8(player + offset) == 0);
						}
						// Execute the ROM's steering-penalty routine, not just the input copy.
						for (uint32_t offset = 0x7c; offset <= 0x7f; offset++) m.write8(player + offset, 0x50);
						run_to(0x2bf5d, 0x2bf6f);
						for (uint32_t offset = 0x7c; offset <= 0x7f; offset++)
							assert(m.read8(player + offset) == 0x50);
						assert(m.read16(player + 0x9a) == 0);
					} else {
						assert(m.read32(player + 0xf8) == pending);
						assert(m.read32(player + 0x50) == (pending == 0x2c1a4 ? 0x410000u : 0x412000u));
						assert(m.read16(player + 0xf6) == 12);
					}
				}
			}
			assert(m.set_cheats(0));
			m.write16(latch, 0x103);
			run_to(0x1f13e, 0x1f157);
			assert((m.cpu().reg(1) & 0xffff) == 0x103);
		}
	}
	puts("Hit recovery: repeated UFO hits consumed, active spin and steering penalty cleared, CPU/off behavior preserved, interpreter/native passed");
}

void projectile_impulses(const char *regions, const char *map)
{
	for (bool native : {false, true}) {
		model1 m(load_regions(regions, game::wingwar::ID));
		m.reset();
		rec::context runtime;
		if (native) {
			runtime.attach(m.cpu(), game::wingwar::ID.name);
			rec::hints hints;
			assert(hints.read(map));
			hints.entries.insert(0x1fb8d);
			hints.entries.insert(0x2b6a6);
			hints.entries.insert(0x2bd4a);
			assert(rec::build_native(m.maincpu_rom(), hints));
		}
		auto run_to = [&](uint32_t start, uint32_t end) {
			if (native) runtime.attach(m.cpu(), game::wingwar::ID.name);
			m.cpu().set_reg(31, 0x40f600);
			m.cpu().set_reg(32, start);
			int steps = 0;
			while (m.cpu().pc() != end && !(end == 0x2bd8f && m.cpu().pc() == 0x2bd80) && steps++ < 1000) {
				if (native) runtime.run(1);
				else m.cpu().run(1);
			}
			assert(steps < 1000 && (!native || !rec::fallback_count()));
		};
		for (uint32_t local : {0x40b400u, 0x40b800u})
		for (uint32_t target : {0x40b400u, 0x40b800u})
		for (uint32_t cheats : {0u, 1u, 2u, 3u})
		for (uint8_t mode : {0, 1})
		for (int impulse : {-256, 256}) {
			assert(m.set_cheats(cheats));
			m.write32(0x40f32c, local);
			// R24 is the projectile here, not the protected aircraft.
			const uint32_t projectile = 0x40d000;
			m.write8(projectile + 0x58, 0);
			m.write32(0x40f3e4, target);
			m.write8(0x40f302, mode);
			m.cpu().set_reg(24, projectile);
			m.cpu().set_reg(25, 0x408000);
			const bool protected_target = (cheats & game::wingwar::INVINCIBILITY) && target == local;
			m.write16(target + 0xb8, 123);
			m.write16(target + 0xbc, uint16_t(-321));
			for (int repeat = 0; repeat < 100; repeat++) {
				const int pitch = int16_t(m.read16(target + 0xb8));
				const int roll = int16_t(m.read16(target + 0xbc));
				if (native) runtime.attach(m.cpu(), game::wingwar::ID.name);
				m.cpu().set_reg(15, uint32_t(-impulse));
				m.cpu().set_reg(17, uint32_t(impulse));
				m.cpu().set_reg(32, 0x1fb8d);
				int steps = 0;
				while (m.cpu().pc() != 0x1fbba && steps++ < 100) {
					if (native) runtime.run(1);
					else m.cpu().run(1);
				}
				assert(steps < 100 && (!native || !rec::fallback_count()));
				const int divisor = mode == 1 ? 1 : 3;
				assert(int16_t(m.read16(target + 0xb8)) == int16_t(pitch - (protected_target ? 0 : impulse)) / divisor);
				assert(int16_t(m.read16(target + 0xbc)) == int16_t(roll + (protected_target ? 0 : impulse)) / divisor);
				assert(m.cpu().reg(15) == uint32_t(-impulse) && m.cpu().reg(17) == uint32_t(impulse));
			}
		}
		for (uint32_t player : {0x40b400u, 0x40b800u})
		for (uint8_t type : {0, 1, 2, 3, 4})
		for (uint32_t cheats : {1u, 3u})
		for (uint8_t stick : {0, 128, 255}) {
			assert(m.set_cheats(cheats));
			m.write32(0x40f32c, player);
			for (uint32_t offset = 0; offset < 0x110; offset += 4) m.write32(player + offset, 0);
			m.write8(player + 0x54, type);
			m.write8(player + 0x94, stick);
			m.write8(player + 0x95, 128);
			m.write8(0x40d058, 0);
			m.write32(0x40f3e4, player);
			m.write8(0x40f302, 1);
			m.cpu().set_reg(25, 0x408000);
			for (int repeat = 0; repeat < 80; repeat++) {
				m.cpu().set_reg(24, 0x40d000);
				m.cpu().set_reg(15, uint32_t(-256));
				m.cpu().set_reg(17, 256);
				run_to(0x1fb8d, 0x1fbba);
				m.cpu().set_reg(24, player);
				m.cpu().set_reg(18, m.read32(0x1fe024 + type * 4));
				run_to(0x2b6a6, 0x2b6d3);
				run_to(0x2bd4a, 0x2bd8f);
				run_to(0x2c0d0, 0x2c19e);
				run_to(0x2beff, 0x2bf5c);
				assert(m.read16(player + 0xb8) == 0 && m.read16(player + 0xbc) == 0);
				const int roll = int16_t(m.read16(player + 0x18));
				if (stick == 128) assert(std::abs(roll) < 256);
				else if (repeat >= 20) assert(stick == 0 ? roll < -1000 : roll > 1000);
			}
		}
	}
	puts("Projectile hits: local-only protection, existing momentum/CPU/off behavior preserved, neutral bank <256 and both steering directions under repeated fire across five aircraft, interpreter/native passed");
}

void flight_and_missiles(const char *regions, const char *map)
{
	for (bool native : {false, true}) {
		model1 m(load_regions(regions, game::wingwar::ID));
		m.reset();
		rec::context runtime;
		if (native) {
			runtime.attach(m.cpu(), game::wingwar::ID.name);
			rec::hints hints;
			assert(hints.read(map));
			for (uint32_t pc : {0x2a905u, 0x2c19fu,
				0x2bc08u, game::wingwar::MISSILE_STOCK_READ}) hints.entries.insert(pc);
			assert(rec::build_native(m.maincpu_rom(), hints));
		}
		auto run_to = [&](uint32_t start, uint32_t end) {
			if (native) runtime.attach(m.cpu(), game::wingwar::ID.name);
			m.cpu().set_reg(31, 0x40f600);
			m.cpu().set_reg(32, start);
			int steps = 0;
			while (m.cpu().pc() != end && !(end == 0x1fe70 && m.cpu().pc() == 0x1fe66) && steps++ < 1000) {
				if (native) runtime.run(1);
				else m.cpu().run(1);
			}
			assert(steps < 1000 && (!native || !rec::fallback_count()));
		};
		for (uint32_t local : {0x40b400u, 0x40b800u})
		for (uint32_t player : {0x40b400u, 0x40b800u})
		for (uint32_t flags : {0u, 1u, 2u, 3u}) {
			assert(m.set_cheats(flags));
			m.write32(0x40f32c, local);
			m.cpu().set_reg(24, player);
			m.cpu().set_reg(25, 0x408000);
			for (bool combat : {false, true}) {
				const uint32_t dispatcher = combat ? 0x2c19f : 0x2a905;
				const uint32_t pending = combat ? 0x2c2c2 : 0x2aa38;
				m.write32(player + 0xf8, pending);
				m.write32(player + 0x50, 0x3c001800);
				m.write16(player + 0xf6, 8);
				m.write16(0x40f028, 2);
				for (int i = 0; i < 5; i++) m.write8(player + 0x70 + i, 0x55);
				run_to(dispatcher, dispatcher + 4);
				assert(m.read32(player + 0xf8) == pending);
				assert(m.read8(player + 0x71) != 0x55);
				assert(m.set_cheats(0));
				assert(m.read32(player + 0xf8) == pending);
				assert(m.set_cheats(flags));
			}
			for (uint8_t axis : {0, 128, 255}) {
				m.write16(player + 0x18, 0x1000);
				m.write8(player + 0x94, axis);
				m.write8(player + 0x95, 255 - axis);
				assert(m.set_cheats(0));
				run_to(0x2bc08, 0x2bc51);
				const auto normal = m.read8(player + 0x7e);
				assert(m.set_cheats(flags));
				run_to(0x2bc08, 0x2bc51);
				assert(m.read8(player + 0x7e) == normal);
				assert(m.read8(player + 0x94) == axis && m.read8(player + 0x95) == 255 - axis);
			}
			for (uint8_t type : {0, 1, 2, 3, 4}) {
				m.write8(player + 0x54, type);
				const uint32_t weapons = m.read32(0x1feb18 + type * 4);
				const uint8_t capacity = m.read8(weapons + 2);
				assert(capacity > 1 && capacity <= 127);
				for (uint8_t stock : {uint8_t(0), uint8_t(1), capacity}) {
					m.write8(player + 0xea, stock);
					m.write8(player + 0xe9, m.read8(weapons));
					m.write8(player + 0xe8, 0);
					m.write8(player + 0xe6, 0);
					m.write8(player + 0xe7, 0);
					m.write8(player + 0x97, 2);
					m.write8(player + 0x57, 0);
					run_to(game::wingwar::MISSILE_STOCK_READ, 0x1fe70);
					const bool infinite = (flags & game::wingwar::INFINITE_MISSILES) && player == local;
					const uint8_t initial = infinite ? capacity : stock;
					const uint8_t cost = type == 3 ? 1 : m.read8(weapons);
					assert(m.read8(player + 0xea) == (initial ? std::max(0, int(initial) - cost) : 0));
					assert(bool(m.read8(player + 0x57) & 2) == bool(initial));
					assert(m.read8(player + 0xe6) == (initial ? 0x2d : 0));
					if (initial) {
						const auto remaining = m.read8(player + 0xea);
						m.write8(player + 0x57, 0);
						run_to(game::wingwar::MISSILE_STOCK_READ, 0x1fe70);
						// No second launch during reload; only the unlimited magazine is refreshed.
						assert(m.read8(player + 0xea) == (infinite ? capacity : remaining));
						assert(!(m.read8(player + 0x57) & 2));
						assert(m.read8(player + 0xe6) == (infinite || remaining ? 0x2c : 0x2d));
					}
				}
			}
		}
	}
	puts("Wing War cheats: normal steering unchanged, empty magazines refill, normal missile reload and CPU stock preserved, interpreter/native passed");
}

void equivalence(game::id title, const char *regions, const char *map, bool collect, const char *recording = nullptr)
{
	const bool wingwar = title == game::wingwar::ID;
	model1 reference(load_regions(regions, title));
	model1 native(load_regions(regions, title));
	input_replay::recording replay;
	if (recording) {
		const auto error = input_replay::load(recording, title, replay);
		if (!error.empty()) { fprintf(stderr, "%s\n", error.c_str()); std::abort(); }
	}
	input_replay::start(reference, replay);
	input_replay::start(native, replay);
	reference.reset();
	native.reset();
	auto play = wingwar && !recording ? game::make_auto_play(title, "attract", 1, true) : nullptr;
	const int frames = recording ? int(replay.frames.size()) : wingwar ? play->min_frames() : 3600;
	rec::context runtime;
	runtime.attach(native.cpu(), title.name);
	rec::hints h;
	assert(h.read(map));
	if (wingwar) assert(h.ram.size() == 1 && h.ram[0].first == 0x40d000 && h.ram[0].last == 0x40e000);
	else assert(h.ram.empty());
	rec::native_stats st;
	assert(rec::build_native(native.maincpu_rom(), h, &st));
	native.run_cpu = [&](int n) { runtime.run(n); };
	size_t lines = 0;
	std::vector<std::array<uint32_t, 69>> expected;
	reference.on_line = [&](int) {
		std::array<uint32_t, 69> state;
		for (int i = 0; i < 68; i++) state[i] = reference.cpu().reg(i);
		state[68] = reference.cpu().psw();
		expected.push_back(state);
	};
	native.on_line = [&](int line) {
		std::array<uint32_t, 69> state;
		for (int i = 0; i < 68; i++) state[i] = native.cpu().reg(i);
		state[68] = native.cpu().psw();
		if (state != expected.at(line)) {
			fprintf(stderr, "Native mismatch at frame %llu line %d\n",
				(unsigned long long)native.frame_number(), line);
			for (int i = 0; i < 69; i++)
				if (state[i] != expected[line][i])
					fprintf(stderr, "r%d: interpreter %08x native %08x\n", i, expected[line][i], state[i]);
			std::abort();
		}
		lines++;
	};
	for (int f = 1; f <= frames; f++) {
		game::script_inputs in;
		if (recording) in = replay.frames[f - 1];
		else if (play) in = play->frame(f);
		else {
			const auto &io = game::get_io_profile(title);
			for (size_t c = 0; c < in.analog.size(); c++)
				if (native.has_analog_input(c)) in.analog[c] = io.analog_neutral[c];
		}
		for (auto *m : {&reference, &native}) {
			assert(m->set_cheats(in.cheats));
			for (size_t c = 0; c < in.analog.size(); c++)
				if (in.analog[c] >= 0) assert(m->set_analog_input(c, uint8_t(in.analog[c])));
			for (int p = 0; p < 3; p++) m->m_inputs[p] = uint8_t(~in.pressed[p]);
		}
		expected.clear();
		reference.run_frame();
		native.run_frame();
		assert(reference.nvram() == native.nvram() && reference.work_ram() == native.work_ram());
		assert(reference.tgp_cycles_executed() == native.tgp_cycles_executed());
		assert(reference.advanced_ioboard_cycles() == native.advanced_ioboard_cycles());
		assert(reference.frame_quads() == native.frame_quads());
		model1_native_test::lists(reference, native);
		if (play) {
			const auto a = std::static_pointer_cast<game::wingwar::gameplay_state>(reference.game_state());
			const auto b = std::static_pointer_cast<game::wingwar::gameplay_state>(native.game_state());
			for (int buffer = 0; buffer < 2; buffer++) assert(a->cameras[buffer].matrix == b->cameras[buffer].matrix);
			const auto error = play->check(f, native);
			if (!error.empty()) { fprintf(stderr, "%s\n", error.c_str()); std::abort(); }
		}
		if (!collect && rec::fallback_count()) {
			fprintf(stderr, "%.*s native fallback at frame %d PC %06x\n",
				int(title.name.size()), title.name.data(), f, rec::first_fallback_address());
			std::abort();
		}
	}
	if (play) assert(play->finish().empty() && st.overlays->compilations > 0 && st.overlays->cache_hits > 0);
	if (recording && input_replay::ram_hash(native) != replay.expected_ram) {
		fprintf(stderr, "Recorded input RAM hash mismatch\n"); std::abort();
	}
	printf("%.*s native equivalence: %d frames, %zu scanline states, %zu RAM compilations, %zu cache hits; "
		"RAM, TGP/I/O timing, display lists and camera hooks identical; %zu function fallbacks\n",
		int(title.name.size()), title.name.data(), frames, lines,
		st.overlays ? st.overlays->compilations : 0, st.overlays ? st.overlays->cache_hits : 0, rec::fallback_count());
}

}

int main(int argc, char **argv)
{
	if ((argc == 5 || (argc == 6 && !strcmp(argv[5], "--collect-fallbacks"))) &&
		!strcmp(argv[3], "--replay-inputs")) {
		equivalence(game::wingwar::ID, argv[1], argv[2], argc == 6, argv[4]);
		return 0;
	}
	if (argc == 4 && !strcmp(argv[1], "--shared")) {
		equivalence(game::id{"vr"}, argv[2], "assets/games/vr/vr_map.txt", false);
		equivalence(game::id{"swa"}, argv[3], "assets/games/swa/swa_map.txt", false);
		return 0;
	}
	if (argc != 3 && !(argc == 4 && !strcmp(argv[3], "--collect-fallbacks"))) {
		fprintf(stderr, "usage: %s regions-dir provisional-map [--replay-inputs file] [--collect-fallbacks]\n", argv[0]);
		return 2;
	}
	guarded_versions();
	context_isolation();
	invincibility(argv[1], argv[2]);
	hit_recovery(argv[1], argv[2]);
	projectile_impulses(argv[1], argv[2]);
	flight_and_missiles(argv[1], argv[2]);
	equivalence(game::wingwar::ID, argv[1], argv[2], argc == 4);
}
