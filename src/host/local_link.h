// Experimental local-only transport. One full, sequenced snapshot per cabinet vblank.
#pragma once
#include "../machine/m1comm.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>
#ifndef _WIN32
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

class local_link
{
public:
	local_link() = default;
	local_link(const local_link &) = delete;
	local_link &operator=(const local_link &) = delete;
	~local_link()
	{
#ifndef _WIN32
		if (m_socket >= 0) close(m_socket);
		if (m_listener >= 0) close(m_listener);
#endif
	}
	bool failed() const { return m_failed.load(); }
	bool open(int player, int port)
	{
		m_player = player;
		m_port = port;
#ifdef _WIN32
		return fail("this prototype currently requires macOS or Linux");
#else
		if (player == 1) {
			m_listener = socket(AF_INET, SOCK_STREAM, 0);
			if (m_listener < 0) return fail("socket", errno);
			int yes = 1;
			if (setsockopt(m_listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes) < 0) return fail("setsockopt", errno);
			sockaddr_in address = endpoint();
			if (bind(m_listener, reinterpret_cast<sockaddr *>(&address), sizeof address) < 0) return fail("bind", errno);
			if (listen(m_listener, 1) < 0 || !nonblocking(m_listener)) return fail("listen", errno);
		}
		fprintf(stderr, "VR link: player %d, loopback port %d; waiting for the other cabinet (15 s timeout)\n", player, port);
		return true;
#endif
	}
	bool exchange(const m1comm::memory &out, m1comm::memory &in)
	{
		if (failed()) return false;
#ifdef _WIN32
		return false;
#else
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
		if (m_socket < 0 && !connect_peer(deadline)) return false;
		std::array<uint8_t, 16 + 0x1000> sent{}, received{};
		memcpy(sent.data(), "M1LK", 4);
		sent[4] = 1;
		sent[5] = uint8_t(m_player);
		for (int i = 0; i < 8; i++) sent[8 + i] = uint8_t(m_sequence >> (i * 8));
		std::copy(out.begin(), out.end(), sent.begin() + 16);
		if (m_player == 1) {
			if (!transfer(sent.data(), sent.size(), true, deadline) ||
				!transfer(received.data(), received.size(), false, deadline)) return false;
		} else {
			if (!transfer(received.data(), received.size(), false, deadline) ||
				!transfer(sent.data(), sent.size(), true, deadline)) return false;
		}
		auto expected = sent;
		expected[5] = uint8_t(3 - m_player);
		if (!std::equal(received.begin(), received.begin() + 16, expected.begin()))
			return fail("incompatible peer or out-of-sequence packet");
		std::copy(received.begin() + 16, received.end(), in.begin());
		if (in[1] != 3 - m_player) return fail("peer cabinet mode does not match its player number");
		if (!m_sequence) fprintf(stderr, "VR link: connected, cabinet %d of 2\n", m_player);
		++m_sequence;
		return true;
#endif
	}

private:
	int m_player = 0, m_port = 0;
	std::atomic<bool> m_failed{false};
	uint64_t m_sequence = 0;
	bool fail(const char *what, int error = 0)
	{
		fprintf(stderr, "VR link: %s%s%s; ending linked session\n", what, error ? ": " : "", error ? strerror(error) : "");
		m_failed = true;
		return false;
	}
#ifndef _WIN32
	using time_point = std::chrono::steady_clock::time_point;
	int m_socket = -1, m_listener = -1;
	sockaddr_in endpoint() const
	{
		sockaddr_in address{};
		address.sin_family = AF_INET;
		address.sin_port = htons(uint16_t(m_port));
		address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		return address;
	}
	static bool nonblocking(int fd)
	{
		const int flags = fcntl(fd, F_GETFL);
		return flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
	}
	bool wait(int fd, short events, time_point deadline)
	{
		for (;;) {
			const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
			if (ms <= 0) return fail("peer timed out (paused, closed, or not started)");
			pollfd p{fd, events, 0};
			const int result = poll(&p, 1, int(ms));
			if (result > 0) return true;
			if (result < 0 && errno != EINTR) return fail("poll", errno);
		}
	}
	bool connect_peer(time_point deadline)
	{
		if (m_player == 1) {
			if (!wait(m_listener, POLLIN, deadline)) return false;
			m_socket = accept(m_listener, nullptr, nullptr);
			if (m_socket < 0) return fail("accept", errno);
			close(m_listener);
			m_listener = -1;
		} else {
			for (;;) {
				m_socket = socket(AF_INET, SOCK_STREAM, 0);
				if (m_socket < 0 || !nonblocking(m_socket)) return fail("socket", errno);
				sockaddr_in address = endpoint();
				int result = connect(m_socket, reinterpret_cast<sockaddr *>(&address), sizeof address);
				if (result < 0 && errno == EINPROGRESS) {
					if (!wait(m_socket, POLLOUT, deadline)) return false;
					int error = 0;
					socklen_t size = sizeof error;
					if (getsockopt(m_socket, SOL_SOCKET, SO_ERROR, &error, &size) < 0) return fail("connect status", errno);
					if (!error) break;
					errno = error;
				} else if (!result) break;
				if (errno != ECONNREFUSED) return fail("connect", errno);
				close(m_socket);
				m_socket = -1;
				if (std::chrono::steady_clock::now() >= deadline) return fail("connect timed out");
				std::this_thread::sleep_for(std::chrono::milliseconds(20));
			}
		}
		if (!nonblocking(m_socket)) return fail("nonblocking socket", errno);
		int yes = 1;
		if (setsockopt(m_socket, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof yes) < 0) return fail("TCP_NODELAY", errno);
#ifdef __APPLE__
		if (setsockopt(m_socket, SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof yes) < 0) return fail("SO_NOSIGPIPE", errno);
#endif
		return true;
	}
	bool transfer(uint8_t *data, size_t size, bool sending, time_point deadline)
	{
		while (size) {
			if (!wait(m_socket, sending ? POLLOUT : POLLIN, deadline)) return false;
#ifdef MSG_NOSIGNAL
			constexpr int flags = MSG_NOSIGNAL;
#else
			constexpr int flags = 0;
#endif
			const ssize_t n = sending ? send(m_socket, data, size, flags) : recv(m_socket, data, size, 0);
			if (n > 0) { data += n; size -= size_t(n); }
			else if (!n) return fail("peer disconnected");
			else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) return fail(sending ? "send" : "receive", errno);
		}
		return true;
	}
#endif
};
