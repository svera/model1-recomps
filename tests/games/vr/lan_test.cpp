#include "../../../src/host/regions.h"
#include "../../../src/games/vr/gameplay.h"
#include "../../../src/machine/link_packet.h"
#include "../../../src/rt/native.h"
#include "../../../src/games/script.h"
#include "../../../src/host/model1_lan.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <deque>
#include <memory>
#include <random>

static void codec()
{
	using namespace model1_link;
	static_assert(PACKET_SIZE == 490 && PACKET_SIZE <= 1024);
	m1comm::memory out{};
	out[1] = 2; out[4] = 1;
	for (size_t i = 6; i < 16; i++) out[i] = uint8_t(i);
	for (size_t i = 0; i < RECORD_SIZE; i++) out[0x10 + i] = uint8_t(i);
	const auto bytes = encode(out, 42, 0xffffffffu);
	receiver peer;
	peer.reset(42, 2);
	assert(peer.take(bytes));
	assert(!peer.take(bytes));
	assert(peer.memory()[1] == 2 && peer.memory()[4] == 1);
	assert(std::equal(out.begin() + 6, out.begin() + 16, peer.memory().begin() + 6));
	assert(std::equal(out.begin() + 0x10, out.begin() + 0x1d4, peer.memory().begin() + 0x10));
	assert(peer.take(encode(out, 42, 0))); // sequence wrap
	assert(!peer.take(encode(out, 42, 0xfffffffeu)));
	assert(!peer.take(encode(out, 43, 1)));
	out[1] = 1;
	assert(!peer.take(encode(out, 42, 1)));
	assert(!peer.take(std::span(bytes).first(bytes.size() - 1)));
	auto bad = bytes;
	bad[4] = 1;
	assert(!peer.take(bad));
	bad = bytes;
	bad[21] = 1;
	assert(!peer.take(bad));
	bad = bytes;
	bad[0] = 'X';
	assert(!peer.take(bad));
	peer.reset(43, 2);
	assert(!peer.available() && !peer.take(bytes));
	peer.reset(42, 2);
	out[1] = 2;
	assert(peer.take(encode(out, 42, 1, 1, 12)));
	assert(peer.pause_step() == 1 && peer.pause_epoch() == 12);
	assert(peer.take(encode(out, 42, 3, 2, 13, 2, 3, 123)));
	assert(peer.frame() == 123 && peer.active_mask() == 3);
	assert(!peer.take(encode(out, 42, 2, 1, 12)));
	assert(peer.pause_step() == 2 && peer.pause_epoch() == 13);
	assert(!peer.take(encode(out, 42, 4, 5, 14)));
	puts("LAN codec: 490 bytes, record/status roundtrip, versions, identities, generations and ordering passed");
}

static void network_board()
{
	const lan_config config{{'T', 'V', 'R', 'B'}, 1, 29876, 0, 0, 0, LAN_LINK, 0, 2};
	lan_session host(config), guest(config);
	assert(host.open("MASTER") && guest.open("SLAVE"));
	assert(host.host() && guest.search());
	uint64_t now = 0;
	auto update = [&] {
		now += 17;
		host.update(now); guest.update(now);
	};
	for (int i = 0; i < 180 && guest.rooms().empty(); i++) update();
	assert(!guest.rooms().empty() && guest.join(guest.rooms().front()));
	for (int i = 0; i < 180 && !guest.in_room(); i++) update();
	assert(guest.in_room() && host.start({}));
	for (int i = 0; i < 180 && guest.role() != LAN_PLAYING; i++) update();
	model1::regions a_regions, b_regions;
	a_regions.title = b_regions.title = game::id{"vr"};
	model1 a(std::move(a_regions)), b(std::move(b_regions));
	model1_lan master, slave;
	assert(master.begin(host, 7) && slave.begin(guest, 7));
	assert(master.player() == 1 && slave.player() == 2);
	master.attach(a); slave.attach(b);
	for (model1 *m : {&a, &b}) {
		m->link()->write(1, m == &a ? 1 : 2);
		m->link()->write(4, 1);
		m->link()->write(0x10, m == &a ? 0x31 : 0x52);
		m->link()->set_cn(1);
	}
	host.publish(model1_link::encode(a.link()->snapshot(), 7, 1));
	guest.publish(model1_link::encode(b.link()->snapshot(), 7, 1));
	for (int i = 0; i < 60 && (host.block(1).empty() || guest.block(0).empty()); i++) update();
	assert(!host.block(1).empty() && !guest.block(0).empty());
	assert(master.update(host, now, a) && slave.update(guest, now, b));
	master.pause(true, now);
	assert(!master.paused()); // Opening a menu during preparation must not stop cabinet boot.
	master.exchange(a); slave.exchange(b);
	assert(a.link()->read(0x398) == 0x52 && b.link()->read(0x1d4) == 0x31);
	assert(a.link()->ticks() == 1 && b.link()->ticks() == 1);
	std::vector<std::pair<int, uint64_t>> steps;
	auto poll = [&] {
		update();
		assert(master.update(host, now, a) && slave.update(guest, now, b));
		const int step = master.countdown() ? master.countdown() : master.paused() ? -1 : 0;
		if (steps.empty() || steps.back().first != step) steps.emplace_back(step, now);
	};
	auto wait_for = [&](auto condition) {
		for (int i = 0; i < 180 && !condition(); i++) poll();
		assert(condition());
	};
	assert(master.host() && !slave.host());
	slave.pause(true, now);
	assert(!slave.paused());
	master.pause(true, now);
	wait_for([&] { return slave.paused(); });
	for (int i = 0; i < 400; i++) poll();
	assert(master.paused() && slave.paused());
	assert(a.link()->ticks() == 1 && b.link()->ticks() == 1);
	slave.pause(false, now);
	assert(slave.paused());
	master.pause(false, now);
	wait_for([&] { return slave.countdown() == 3; });
	assert(master.countdown() == 3);
	// Withhold the guest's acknowledgement of a new pause: the host must not
	// start another countdown until the guest has seen it.
	master.pause(true, now);
	master.pause(false, now);
	for (int i = 0; i < 90; i++) {
		update();
		assert(master.update(host, now, a));
		assert(master.paused() && master.countdown() == 0);
	}
	wait_for([&] { return slave.countdown() == 3; });
	for (int value : {2, 1}) {
		wait_for([&] { return slave.countdown() == value; });
		assert(master.paused() && slave.paused());
	}
	wait_for([&] { return !slave.paused(); });
	assert(!master.paused() && steps.size() >= 4);
	const auto start = steps.size() - 4;
	for (size_t i = 0; i < 4; i++) {
		assert(steps[start + i].first == 3 - int(i));
		if (i) assert(steps[start + i].second - steps[start + i - 1].second >= 1000);
	}
	puts("LAN pause: host authority, long pause heartbeats, acknowledged 3-2-1 countdown and cancellation passed");
	master.pause(true, now);
	wait_for([&] { return slave.paused(); });
	const auto first = now;
	// Membership heartbeats do not hide lost cabinet data; duplicates and old sessions
	// must not keep an otherwise dead race alive.
	while (now < first + 3020) {
		guest.publish(model1_link::encode(b.link()->snapshot(), 6, 2));
		update();
		if (!master.update(host, now, a)) break;
	}
	assert(host.error().find("Fewer than two racers") != std::string::npos);
	master.stop(a); slave.stop(b);
	assert(!a.link() && !b.link() && !master.active() && !slave.active());
	assert(host.host());
	assert(!master.begin(host, 8));
	host.close(); guest.close();
	puts("LAN board adapter: stable IDs, compact record delivery, stale-session timeout and teardown passed");
}

static void network_group(int count)
{
	const lan_config config{{'T', 'V', 'R', 'G'}, 3, 29878, 0, 0, 0, LAN_LINK, 0, 4};
	std::array<std::unique_ptr<lan_session>, 5> sessions;
	std::array<std::unique_ptr<model1>, 4> machines;
	std::array<model1_lan, 4> adapters;
	uint64_t now = 0;
	for (int i = 0; i <= count; i++) {
		sessions[i] = std::make_unique<lan_session>(config);
		assert(sessions[i]->open("RACER-" + std::to_string(i + 1)));
	}
	auto transport = [&] {
		now += 17;
		for (int i = 0; i <= count; i++) sessions[i]->update(now);
	};
	assert(sessions[0]->host());
	for (int i = 1; i < count; i++) {
		assert(sessions[i]->search());
		for (int n = 0; n < 180 && sessions[i]->rooms().empty(); n++) transport();
		assert(!sessions[i]->rooms().empty() && sessions[i]->join(sessions[i]->rooms().front()));
		for (int n = 0; n < 180 && !sessions[i]->in_room(); n++) transport();
		assert(sessions[i]->in_room());
	}
	assert(sessions[0]->members().size() == unsigned(count));
	if (count == 4) {
		assert(sessions[4]->search());
		for (int n = 0; n < 180 && sessions[4]->rooms().empty(); n++) transport();
		assert(!sessions[4]->rooms().empty() && sessions[4]->join(sessions[4]->rooms().front()));
		for (int n = 0; n < 180 && sessions[4]->error().empty(); n++) transport();
		assert(sessions[4]->error() == "LAN: ROOM FULL");
	}
	assert(sessions[0]->start({}));
	for (int n = 0; n < 180 && sessions[count - 1]->role() != LAN_PLAYING; n++) transport();
	for (int i = 0; i < count; i++) {
		assert(sessions[i]->role() == LAN_PLAYING && adapters[i].begin(*sessions[i], 81));
		assert(adapters[i].player() == i + 1 && adapters[i].role() == (i ? 2 : 1));
		model1::regions regions;
		regions.title = game::id{"vr"};
		machines[i] = std::make_unique<model1>(std::move(regions));
		adapters[i].attach(*machines[i]);
		auto &board = *machines[i]->link();
		board.write(1, i ? 2 : 1);
		board.write(4, 1);
		board.write(0x10, uint8_t(0x40 + i));
		board.write(0x10 + 0x10e, uint8_t(i + 1));
		board.write(6, uint8_t(0x60 + i));
	}
	// Slow ROM boot must not trip arcade-lan's three-second peer timeout.
	for (int n = 0; n < 240; n++) {
		transport();
		for (int i = 0; i < count; i++) assert(adapters[i].update(*sessions[i], now, *machines[i]));
	}
	for (int i = 0; i < count; i++) {
		const auto roster = sessions[i]->members(true);
		assert(std::none_of(roster.begin(), roster.end(), [](const auto &member) { return member.gone; }));
		machines[i]->link()->set_cn(1);
		assert(!adapters[i].ready_for_frame(*machines[i]));
	}
	int alive = count;
	std::array<bool, 4> connected{true, true, true, true};
	auto poll = [&](int skip = -1) {
		transport();
		for (int i = 0; i < count; i++)
			if (connected[i] && i != skip) {
				assert(adapters[i].update(*sessions[i], now, *machines[i]));
				if (!adapters[i].paused() && adapters[i].ready_for_frame(*machines[i]))
					adapters[i].exchange(*machines[i]);
			}
	};
	for (int n = 0; n < 60; n++) poll();
	for (int i = 0; i < count; i++) {
		const auto &board = *machines[i]->link();
		assert(board.read(2) == i + 1 && board.read(3) == count && board.read(6) == 0x60);
		for (int slot = 0; slot < count; slot++)
			assert(board.read(0x10 + unsigned(slot + 1) * 0x1c4) == 0x40 + slot);
	}
	adapters[0].pause(true, now);
	for (int n = 0; n < 60; n++) poll();
	for (int i = 0; i < count; i++) assert(adapters[i].paused());
	adapters[count - 1].pause(false, now);
	assert(adapters[count - 1].paused());
	// All guests, not just the first, must acknowledge each countdown step.
	adapters[0].pause(false, now);
	for (int n = 0; n < 90; n++) poll(count - 1);
	assert(adapters[0].countdown() == 3);
	for (int n = 0; n < 240; n++) poll();
	for (int i = 0; i < count; i++) assert(!adapters[i].paused());
	// Leave while paused: keep IDs and valid car data, but clear participation.
	adapters[0].pause(true, now);
	for (int n = 0; n < 30; n++) poll();
	while (alive > 2) {
		const int departed = count - alive + 1;
		sessions[departed]->close();
		connected[departed] = false;
		--alive;
		for (int n = 0; n < 90; n++) poll();
		for (int i = 0; i < count; i++) {
			if (!connected[i]) continue;
			assert(adapters[i].racers() == alive && adapters[i].player() == i + 1 && adapters[i].paused());
			assert(machines[i]->link()->read(0x10 + unsigned(departed + 1) * 0x1c4) == 0x40 + departed);
		}
	}
	adapters[0].pause(false, now);
	for (int n = 0; n < 240; n++) poll();
	assert(!adapters[0].paused() && !adapters[count - 1].paused());
	for (int i = 0; i < count; i++)
		if (connected[i])
			for (int slot = 0; slot < count; slot++)
				assert(machines[i]->link()->read(0x10 + unsigned(slot + 1) * 0x1c4 + 0x10e) ==
					(connected[slot] ? slot + 1 : 0));
	sessions[0]->close();
	for (int n = 0; n < 240 && adapters[count - 1].update(*sessions[count - 1], now, *machines[count - 1]); n++) transport();
	assert(sessions[count - 1]->error().find("host disconnected") != std::string::npos);
	for (int i = 0; i < count; i++) adapters[i].stop(*machines[i]);
	printf("LAN group: %d cabinets, slots, status, all-peer pause, guest loss and host loss passed\n", count);
}

struct delivery
{
	int64_t at;
	int to, from;
	model1_link::packet bytes;
};

static int asynchronous(const char *dir, bool impaired, bool grand_prix, int count, bool dropout)
{
	std::array<std::unique_ptr<model1>, 4> machines;
	std::array<rec::context, 4> runtimes;
	std::array<std::array<model1_link::receiver, 4>, 4> peers;
	std::array<std::unique_ptr<game::auto_play>, 4> drivers;
	std::array<uint32_t, 4> sequences{};
	bool departed = false;
	const int departed_index = count == 4 ? 1 : count - 1;
	auto connected = [&](int i) { return !departed || i != departed_index; };
	std::deque<delivery> queue;
	std::mt19937 random(0x4d314c41);
	rec::hints map;
	assert(map.read("assets/games/vr/vr_map.txt"));
	for (int p = 0; p < count; p++) {
		machines[p] = std::make_unique<model1>(load_regions(dir, game::id{"vr"}));
		drivers[p] = game::make_auto_play(game::id{"vr"}, "qualifying", 0, false);
		auto &m = *machines[p];
		m.set_drawing(false);
		runtimes[p].attach(m.cpu(), "vr");
		assert(rec::build_native(m.maincpu_rom(), map));
		m.run_cpu = [&, p](int n) { runtimes[p].run(n); };
		m.reset();
		for (int f = 0; f < 900; f++) m.run_frame();
		assert(game::vr::set_coin_setting(m, game::vr::FREE_PLAY));
		assert(game::vr::set_link_player(m, p == 0 ? 1 : 2));
		game::vr::set_machine_setting(m, game::vr::machine_setting::race_type, grand_prix ? 1 : 0);
		for (int other = 0; other < count; other++) peers[p][other].reset(1, uint8_t(other + 1));
		m.enable_link({}, true);
		m.reset();
		runtimes[p].attach(m.cpu(), "vr");
	}
	std::array<int64_t, 4> next{};
	for (int p = 0; p < count; p++) next[p] = impaired ? p * 170000 : 0;
	std::array<int, 4> previous{-1, -1, -1, -1}, races{}, race_frames{}, results{};
	std::array<bool, 4> saw_results{};
	int start_at = 2400, completed = 0;
	bool racing = false;
	const int limit = grand_prix ? 1200000 : 180000;
	for (int frames = 0; frames < limit * count; frames++) {
		int p = -1;
		for (int i = 0; i < count; i++)
			if (connected(i) && (p < 0 || next[i] < next[p])) p = i;
		const int64_t now = next[p];
		for (auto it = queue.begin(); it != queue.end();) {
			if (it->at <= now) {
				if (connected(it->from) && connected(it->to)) peers[it->to][it->from].take(it->bytes);
				it = queue.erase(it);
			} else ++it;
		}
		auto &m = *machines[p];
		const int elapsed = int(now / 16667);
		const bool start = (elapsed >= start_at && elapsed < start_at + 8) ||
			(elapsed >= start_at + 120 && elapsed < start_at + 128);
		m.m_inputs[0] = uint8_t(~(start ? 0x10 : 0));
		assert(m.set_analog_input(0, 0x80));
		assert(m.set_analog_input(1, p ? 0x90 : 0xff));
		assert(m.set_analog_input(2, 0x30));
		if (grand_prix && m.work_ram()[0x1138] == 0x14) {
			const auto inputs = drivers[p]->frame(6130 + race_frames[p], m);
			m.m_inputs[1] = uint8_t(~inputs.pressed[1]);
			for (int channel = 0; channel < 3; channel++)
				assert(m.set_analog_input(channel, uint8_t(inputs.analog[channel])));
		}
		for (int line = 0; line < model1::FRAME_LINES; line++) {
			if (line == model1::FRAME_VBLANK && m.link()->ready() &&
				m.link()->snapshot()[1] == (p == 0 ? 1 : 2)) {
				const auto packet = model1_link::encode(m.link()->snapshot(), 1, sequences[p]++,
					0, 0, uint8_t(p + 1), uint8_t((1u << count) - 1), uint32_t(m.link()->ticks()));
				const bool gap = impaired && elapsed % 1800 >= 900 && elapsed % 1800 < 912;
				for (int to = 0; to < count; to++)
					if (connected(to) && to != p && !gap && (!impaired || random() % 100 >= 10)) {
						queue.push_back({now + (impaired ? int64_t(random() % 50001) : 0), to, p, packet});
						if (impaired && random() % 10 == 0) queue.push_back({now + 60000, to, p, packet});
					}
				std::array<m1comm::memory, 4> snapshots{};
				bool ready = true;
				for (int other = 0; other < count; other++) {
					if (other == p) snapshots[other] = m.link()->snapshot();
					else if (!connected(other))
						snapshots[other] = model1_lan::retired_snapshot(peers[p][other].memory());
					else {
						ready = ready && peers[p][other].available();
						snapshots[other] = peers[p][other].memory();
					}
					snapshots[other][1] = other == 0 ? 1 : 2;
				}
				if (ready) m.link()->tick_group(unsigned(p + 1), std::span(snapshots).first(count));
			}
			m.run_scanline();
		}
		m.m_audio_out.clear();
		if (m.link()->snapshot()[0] == 0xff) {
			fprintf(stderr, "LAN proof: board failed, cabinet %d at %d\n", p + 1, elapsed);
			return 1;
		}
		const int phase = m.work_ram()[0x1138];
		if (phase != previous[p]) {
			fprintf(stderr, "LAN proof: %s %s t=%d cabinet=%d phase=%02x ticks=%llu participants=%u\n",
				impaired ? "impaired" : "clean", grand_prix ? "GP" : "normal", elapsed, p + 1, phase,
				(unsigned long long)m.link()->ticks(), m.read8(0x40d826));
			if (phase == 0x14) { races[p]++; racing = true; }
			if (phase == 0x1c) { results[p]++; saw_results[p] = true; }
			previous[p] = phase;
		}
		if (phase == 0x14) {
			race_frames[p]++;
			const int expected_count = count - int(departed && completed > 0);
			if (m.read8(0x40d826) != expected_count) {
				fprintf(stderr, "LAN proof: cabinet %d entered race with %u participants\n", p + 1, m.read8(0x40d826));
				return 1;
			}
			for (int other = 0; other < count; other++)
				if (connected(other) && machines[other]->work_ram()[0x1138] == 0x14) {
					assert(m.read8(0x501934) == machines[other]->read8(0x501934));
					for (int i = 0; i <= expected_count; i++)
						assert(m.read8(0x40d826 + i) == machines[other]->read8(0x40d826 + i));
				}
		}
		if (dropout && !departed && races[0] && race_frames[0] >= 600) {
			departed = true;
			fprintf(stderr, "LAN proof: dropped cabinet %d during racing\n", departed_index + 1);
		}
		bool attract = true;
		for (int i = 0; i < count; i++)
			if (connected(i)) attract = attract && game::vr::in_attract_mode(*machines[i]);
		if (racing && attract) {
			completed++;
			fprintf(stderr, "LAN proof: completed cycle %d; races %d/%d, results %d/%d\n",
				completed, races[0], races[1], results[0], results[1]);
			for (int i = 0; i < count; i++)
				if (connected(i) && (races[i] != completed || !saw_results[i])) return 1;
			if (completed == (dropout ? 2 : 10)) return 0;
			racing = false;
			saw_results = {};
			for (int i = 0; i < count; i++)
				drivers[i] = game::make_auto_play(game::id{"vr"}, "qualifying", 0, false);
			start_at = elapsed + 180;
		}
		if (elapsed > start_at + (grand_prix ? 120000 : 16000)) {
			fprintf(stderr, "LAN proof: no completed race before deadline; phases %02x/%02x races %d/%d\n",
				previous[0], previous[1], races[0], races[1]);
			return 1;
		}
		next[p] += impaired ? 16663 + p * 4 : 16667;
	}
	fprintf(stderr, "LAN proof: exceeded simulation budget\n");
	return 1;
}

int main(int argc, char **argv)
{
	codec();
	network_board();
	network_group(3);
	network_group(4);
	if (argc == 1) return 0;
	if (argc < 3 || strcmp(argv[1], "--roms")) {
		fprintf(stderr, "usage: vr_lan_test [--roms <regions> [--impaired] [--grand-prix] [--players 2|3|4] [--dropout]]\n");
		return 2;
	}
	bool impaired = false, grand_prix = false;
	bool dropout = false;
	int count = 2;
	for (int i = 3; i < argc; i++) {
		if (!strcmp(argv[i], "--impaired")) impaired = true;
		else if (!strcmp(argv[i], "--grand-prix")) grand_prix = true;
		else if (!strcmp(argv[i], "--dropout")) dropout = true;
		else if (!strcmp(argv[i], "--players") && i + 1 < argc) count = atoi(argv[++i]);
		else return 2;
	}
	if (count < 2 || count > 4 || (dropout && count < 3)) return 2;
	return asynchronous(argv[2], impaired, grand_prix, count, dropout);
}
