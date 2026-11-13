#pragma once
#include "../../third_party/arcade-lan/include/arcade_lan.h"
#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <span>
#include <string>
#include <vector>

class lan_session
{
public:
	explicit lan_session(lan_config config) : m_config(config) {}
	bool open(const std::string &name)
	{
		if (m_device) return true;
		m_device.reset(lan_open(&m_config, name.c_str(), 1));
		if (!m_device) return fail("Cannot open the local network. Check network access permissions.");
		m_clock_set = false;
		m_error.clear();
		return true;
	}
	void close() { m_device.reset(); m_clock_set = m_interrupted = false; }
	bool host()
	{
		if (!m_device) return fail("LAN is not open.");
		m_interrupted = false;
		return lan_host(m_device.get()) || fail("Cannot create the LAN room.");
	}
	bool search()
	{
		if (!m_device) return fail("LAN is not open.");
		lan_search(m_device.get());
		return role() == LAN_SEARCHING || fail("Cannot search for rooms. Check network permissions.");
	}
	bool join(const lan_room &room)
	{
		if (!m_device) return fail("LAN is not open.");
		return lan_join(m_device.get(), &room) || fail("Cannot join this room. Search again.");
	}
	bool start(std::span<const uint8_t> settings)
	{
		if (!m_device) return fail("LAN is not open.");
		return lan_start(m_device.get(), settings.data(), settings.size()) || fail("Cannot start this LAN session.");
	}
	void update(uint64_t milliseconds)
	{
		if (!m_device) return;
		const uint64_t now = milliseconds * 60;
		if (!m_clock_set) { m_next = now; m_clock_set = true; }
		if (now > m_next + 180000 && role() == LAN_PLAYING) {
			fail("LAN session interrupted by sleep or suspension.");
			m_interrupted = true;
		}
		int polls = 0;
		while (now >= m_next && polls++ < 4) {
			lan_update(m_device.get());
			m_next += 1000;
		}
		if (now >= m_next) m_next = now + 1000;
		if (const char *problem = lan_problem(m_device.get())) fail(std::string("LAN: ") + problem);
	}
	bool interrupted() { const bool value = m_interrupted; m_interrupted = false; return value; }
	lan_role role() const { return m_device ? lan_role_of(m_device.get()) : LAN_OFF; }
	bool hosting() const { return m_device && lan_hosting(m_device.get()); }
	bool in_room() const { return role() == LAN_IN_ROOM || role() == LAN_PLAYING; }
	const std::string &error() const { return m_error; }
	void clear_error() { m_error.clear(); }
	bool fail(const std::string &message)
	{
		if (m_error != message) fprintf(stderr, "%s\n", message.c_str());
		m_error = message;
		return false;
	}
	std::vector<lan_room> rooms() const
	{
		std::array<lan_room, LAN_ROOMS> rooms{};
		const int count = lan_rooms(m_device.get(), rooms.data());
		return {rooms.begin(), rooms.begin() + count};
	}
	std::vector<lan_member> members(bool game = false) const
	{
		std::array<lan_member, LAN_DEVICES> members{};
		if (!m_device) return {};
		const int count = game ? lan_game_members(m_device.get(), members.data()) :
			lan_members(m_device.get(), members.data());
		return {members.begin(), members.begin() + count};
	}
	void status(uint8_t value) { if (m_device) lan_set_status(m_device.get(), value); }
	bool started(std::array<uint8_t, LAN_SETTINGS_MOST> &settings, size_t &size)
	{
		return m_device && lan_game_started(m_device.get(), settings.data(), &size, nullptr);
	}
	void publish(std::span<const uint8_t> bytes)
	{
		if (m_device) lan_publish(m_device.get(), bytes.data(), bytes.size());
	}
	std::vector<uint8_t> block(int index) const
	{
		std::array<uint8_t, LAN_BLOCK_MOST> bytes{};
		const size_t size = m_device ? lan_block(m_device.get(), index, bytes.data()) : 0;
		return {bytes.begin(), bytes.begin() + size};
	}
private:
	struct closer { void operator()(lan_device *device) const { lan_close(device); } };
	lan_config m_config;
	std::unique_ptr<lan_device, closer> m_device;
	uint64_t m_next = 0;
	bool m_clock_set = false, m_interrupted = false;
	std::string m_error;
};
