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
	static_assert(PACKET_SIZE == 486 && PACKET_SIZE <= 1024);
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
	bad[20] = 1;
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
	assert(peer.take(encode(out, 42, 3, 2, 13)));
	assert(!peer.take(encode(out, 42, 2, 1, 12)));
	assert(peer.pause_step() == 2 && peer.pause_epoch() == 13);
	assert(!peer.take(encode(out, 42, 4, 5, 14)));
	puts("LAN codec: 486 bytes, record/status roundtrip, versions, identities, generations and ordering passed");
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
	a.link()->tick(); b.link()->tick();
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
	assert(host.error().find("No cabinet data") != std::string::npos);
	master.stop(a); slave.stop(b);
	assert(!a.link() && !b.link() && !master.active() && !slave.active());
	assert(host.host());
	assert(!master.begin(host, 8));
	host.close(); guest.close();
	puts("LAN board adapter: stable IDs, compact record delivery, stale-session timeout and teardown passed");
}

struct delivery
{
	int64_t at;
	int to;
	model1_link::packet bytes;
};

static int asynchronous(const char *dir, bool impaired, bool grand_prix)
{
	std::array<std::unique_ptr<model1>, 2> machines;
	std::array<rec::context, 2> runtimes;
	std::array<model1_link::receiver, 2> peers;
	std::array<std::unique_ptr<game::auto_play>, 2> drivers;
	std::array<uint32_t, 2> sequences{};
	std::deque<delivery> queue;
	std::mt19937 random(0x4d314c41);
	rec::hints map;
	assert(map.read("assets/games/vr/vr_map.txt"));
	for (int p = 0; p < 2; p++) {
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
		assert(game::vr::set_link_player(m, p + 1));
		game::vr::set_machine_setting(m, game::vr::machine_setting::race_type, grand_prix ? 1 : 0);
		peers[p].reset(1, uint8_t(2 - p));
		m.enable_link([&, p](const auto &, auto &in) {
			assert(peers[p].available());
			in = peers[p].memory();
			return true;
		}, true);
		m.reset();
		runtimes[p].attach(m.cpu(), "vr");
	}
	std::array<int64_t, 2> next{0, impaired ? 170000 : 0};
	std::array<int, 2> previous{-1, -1}, races{}, race_frames{}, results{};
	std::array<bool, 2> saw_results{};
	int start_at = 2400, completed = 0;
	bool racing = false;
	const int limit = grand_prix ? 1200000 : 180000;
	for (int frames = 0; frames < limit * 2; frames++) {
		const int p = next[0] <= next[1] ? 0 : 1;
		const int64_t now = next[p];
		for (auto it = queue.begin(); it != queue.end();) {
			if (it->at <= now) {
				peers[it->to].take(it->bytes);
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
				m.link()->snapshot()[1] == p + 1) {
				const auto packet = model1_link::encode(m.link()->snapshot(), 1, sequences[p]++);
				const bool gap = impaired && elapsed % 1800 >= 900 && elapsed % 1800 < 912;
				if (!gap && (!impaired || random() % 100 >= 10)) {
					queue.push_back({now + (impaired ? int64_t(random() % 50001) : 0), 1 - p, packet});
					if (impaired && random() % 10 == 0) queue.push_back({now + 60000, 1 - p, packet});
				}
				if (peers[p].available()) m.link()->tick();
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
			if (m.read8(0x40d826) != 2) {
				fprintf(stderr, "LAN proof: cabinet %d entered race with %u participants\n", p + 1, m.read8(0x40d826));
				return 1;
			}
		}
		if (racing && game::vr::in_attract_mode(*machines[0]) && game::vr::in_attract_mode(*machines[1])) {
			completed++;
			fprintf(stderr, "LAN proof: completed cycle %d; races %d/%d, results %d/%d\n",
				completed, races[0], races[1], results[0], results[1]);
			if (races[0] != completed || races[1] != completed || !saw_results[0] || !saw_results[1]) return 1;
			if (completed == 10) return 0;
			racing = false;
			saw_results = {};
			for (int i = 0; i < 2; i++)
				drivers[i] = game::make_auto_play(game::id{"vr"}, "qualifying", 0, false);
			start_at = elapsed + 180;
		}
		if (elapsed > start_at + (grand_prix ? 120000 : 16000)) {
			fprintf(stderr, "LAN proof: no completed race before deadline; phases %02x/%02x races %d/%d\n",
				previous[0], previous[1], races[0], races[1]);
			return 1;
		}
		next[p] += impaired ? (p ? 16670 : 16663) : 16667;
	}
	fprintf(stderr, "LAN proof: exceeded simulation budget\n");
	return 1;
}

int main(int argc, char **argv)
{
	codec();
	network_board();
	if (argc == 1) return 0;
	if (argc < 3 || argc > 5 || strcmp(argv[1], "--roms")) {
		fprintf(stderr, "usage: vr_lan_test [--roms <regions> [--impaired] [--grand-prix]]\n");
		return 2;
	}
	bool impaired = false, grand_prix = false;
	for (int i = 3; i < argc; i++) {
		if (!strcmp(argv[i], "--impaired")) impaired = true;
		else if (!strcmp(argv[i], "--grand-prix")) grand_prix = true;
		else return 2;
	}
	return asynchronous(argv[2], impaired, grand_prix);
}
