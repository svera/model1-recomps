#include "../src/host/lan_session.h"
#include <cassert>
#include <cstdio>

int main()
{
	const lan_config config{{'T', 'M', '1', 'L'}, 1, 29874, 0, 0, 0, LAN_LINK, 0, 2};
	lan_session host(config), guest(config), third(config);
	assert(host.open("HOST") && guest.open("GUEST") && third.open("THIRD"));
	assert(host.host() && guest.search());
	uint64_t now = 0;
	auto update = [&] {
		now += 17;
		host.status(1); guest.status(1); third.status(1);
		host.update(now); guest.update(now); third.update(now);
	};
	for (int i = 0; i < 180 && guest.rooms().empty(); i++) update();
	assert(!guest.rooms().empty());
	assert(guest.join(guest.rooms().front()));
	for (int i = 0; i < 180 && !guest.in_room(); i++) update();
	assert(guest.in_room() && host.members().size() == 2);
	assert(third.search());
	for (int i = 0; i < 180 && third.rooms().empty(); i++) update();
	assert(!third.rooms().empty() && third.join(third.rooms().front()));
	for (int i = 0; i < 180 && third.error().empty(); i++) update();
	assert(third.error() == "LAN: ROOM FULL" && host.members().size() == 2);
	const std::array<uint8_t, 4> settings{1, 2, 3, 4};
	assert(host.start(settings));
	std::array<uint8_t, LAN_SETTINGS_MOST> received{};
	size_t size = 0;
	for (int i = 0; i < 180 && guest.role() != LAN_PLAYING; i++) update();
	assert(host.started(received, size) && size == settings.size());
	assert(std::equal(settings.begin(), settings.end(), received.begin()));
	assert(guest.started(received, size) && size == settings.size());
	assert(!guest.started(received, size));
	const std::array<uint8_t, 3> block{5, 6, 7};
	host.publish(block);
	for (int i = 0; i < 60 && guest.block(0).empty(); i++) update();
	assert(guest.block(0) == std::vector<uint8_t>(block.begin(), block.end()));
	assert(host.members(true).size() == 2 && guest.members(true).size() == 2);
	host.close();
	for (int i = 0; i < 240 && !guest.hosting(); i++) update();
	assert(guest.hosting()); // Room migration must not become an arcade role change.
	assert(guest.host() && guest.members().size() == 1);
	guest.close(); third.close();
	assert(host.role() == LAN_OFF && guest.role() == LAN_OFF);
	assert(host.open("AGAIN") && host.host());
	host.close();
	puts("LAN session: discovery, join, capacity, start settings, blocks, host loss and reopen passed");
}
