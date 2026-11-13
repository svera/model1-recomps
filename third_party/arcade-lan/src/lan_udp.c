/**
 *    Name:        lan_udp.c
 *    Description: the library's sockets: UDP, non-blocking, on Windows (Winsock), the Switch (libnx's BSD sockets)
 *                 and the rest (POSIX: Linux, macOS, Android, iOS)
 *    Version:     0.2.0
 *    Created:     06-11-2026
 *    Modified:    11-11-2026
 *    Author:      Sebastian Toyos
 *
 *    license:BSD-3-Clause
 *    outrunners-port's console/net_udp.c, each socket its caller's (several devices in one program: the tests). The
 *    sweep is the one its iOS port had there (outrunners-port's pull request 14).
 */
#include "lan_udp.h"

#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#define close_socket    closesocket
#else
#ifdef __SWITCH__
#include <switch.h>
#endif
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#define close_socket    close
#endif

/* The sweep (ARCADE_LAN_NO_BROADCAST) on Apple's systems alone, the ones that refuse broadcasts: it lists the device's
   networks with getifaddrs, and most of its datagrams go to addresses no device has. Linux keeps each of those for 3
   seconds, while it asks whose the address is, counted against its socket's send buffer, which the first sweep fills
   (measured: 238 of its 254 datagrams sent, and none more from that socket for 3 seconds, to any address: a room's
   list never left its host). Darwin's UDP keeps nothing in the socket's buffer; there the sweep was tried on iOS's
   simulator, in outrunners-port's pull request 14, and not here */
#ifdef ARCADE_LAN_NO_BROADCAST
#ifndef __APPLE__
#error "ARCADE_LAN_NO_BROADCAST is for Apple's systems (iOS without the multicast entitlement): see lan_udp.c"
#endif
#include <ifaddrs.h>
#include <net/if.h>
#endif

#define SWEEP_ADDRESSES 256    /* of a local network: a larger one's, the /24 around this device */

static bool started;

/* The system's sockets, once (Winsock's, libnx's) */
static bool start(void)
{
	if (started)
		return true;
#ifdef _WIN32
	{
		WSADATA data;

		if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
			return false;
	}
#elif defined(__SWITCH__)
	if (R_FAILED(socketInitializeDefault()))
		return false;
#endif
	started = true;
	return true;
}

lan_socket lan_udp_open(uint16_t port, bool shared)
{
	struct sockaddr_in address;
	const int on = 1;
#ifdef _WIN32
	SOCKET opened;
#else
	int opened;
#endif

	if (!start())
		return LAN_NO_SOCKET;
	opened = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
#ifdef _WIN32
	if (opened == INVALID_SOCKET)
		return LAN_NO_SOCKET;
#else
	if (opened < 0)
		return LAN_NO_SOCKET;
#endif
	setsockopt(opened, SOL_SOCKET, SO_BROADCAST, (const char *)&on, sizeof(on));
	if (shared)
	{
		setsockopt(opened, SOL_SOCKET, SO_REUSEADDR, (const char *)&on, sizeof(on));
#if defined(SO_REUSEPORT) && !defined(__SWITCH__)
		setsockopt(opened, SOL_SOCKET, SO_REUSEPORT, (const char *)&on, sizeof(on));    /* (macOS, Linux) */
#endif
	}
	memset(&address, 0, sizeof(address));
	address.sin_family = AF_INET;
	address.sin_port = htons(port);
	address.sin_addr.s_addr = htonl(INADDR_ANY);
	if (bind(opened, (const struct sockaddr *)&address, sizeof(address)) != 0)
	{
		close_socket(opened);
		return LAN_NO_SOCKET;
	}
#ifdef _WIN32
	{
		u_long non_blocking = 1;

		ioctlsocket(opened, FIONBIO, &non_blocking);
	}
#else
	fcntl(opened, F_SETFL, fcntl(opened, F_GETFL, 0) | O_NONBLOCK);
#endif
	return (lan_socket)opened;
}

void lan_udp_close(lan_socket socket)
{
	if (socket != LAN_NO_SOCKET)
		close_socket(socket);
}

uint16_t lan_udp_port(lan_socket socket)
{
	struct sockaddr_in address;
	socklen_t size = sizeof(address);

	if (socket == LAN_NO_SOCKET || getsockname(socket, (struct sockaddr *)&address, &size) != 0)
		return 0;
	return ntohs(address.sin_port);
}

bool lan_udp_send(lan_socket socket, lan_address to, const void *data, size_t size)
{
	struct sockaddr_in address;

	if (socket == LAN_NO_SOCKET)
		return false;
	memset(&address, 0, sizeof(address));
	address.sin_family = AF_INET;
	address.sin_port = htons(to.port);
	address.sin_addr.s_addr = htonl(to.ip);
	return sendto(socket, (const char *)data, (int)size, 0, (const struct sockaddr *)&address, sizeof(address))
		== (int)size;
}

size_t lan_udp_receive(lan_socket socket, void *data, size_t room, lan_address *from)
{
	if (socket == LAN_NO_SOCKET)
		return 0;
	for (;;)
	{
		struct sockaddr_in sender;
		socklen_t sender_size = sizeof(sender);
		const int got = (int)recvfrom(socket, (char *)data, (int)room, 0, (struct sockaddr *)&sender, &sender_size);

		if (got > 0)
		{
			from->ip = ntohl(sender.sin_addr.s_addr);
			from->port = ntohs(sender.sin_port);
			return (size_t)got;
		}
#ifdef _WIN32
		/* (a datagram sent earlier to a port no longer open, or one too large: the next) */
		if (got < 0 && (WSAGetLastError() == WSAECONNRESET || WSAGetLastError() == WSAEMSGSIZE))
			continue;
#else
		if (got < 0 && errno == EINTR)
			continue;
#endif
		return 0;
	}
}

bool lan_udp_private(uint32_t ip)
{
	return ip >> 24 == 10u || ip >> 20 == 0xAC1u || ip >> 16 == 0xC0A8u;
}

bool lan_udp_local(uint32_t ip)
{
	return ip >> 24 == 127u || lan_udp_private(ip);
}

bool lan_udp_sweep_range(uint32_t ip, uint32_t mask, uint32_t *first, uint32_t *last)
{
	/* (a private address: a phone's cellular network, or any other, is not one whose devices are asked) */
	if (!lan_udp_private(ip))
		return false;
	if (~mask >= SWEEP_ADDRESSES)
		mask = 0xFFFFFF00u;
	*first = (ip & mask) + 1u;    /* (not the network's own address, nor its broadcast one) */
	*last = (ip | ~mask) - 1u;
	return *first <= *last;
}

#ifdef ARCADE_LAN_NO_BROADCAST
void lan_udp_sweep(lan_socket socket, uint16_t port, const void *data, size_t size)
{
	struct ifaddrs *interfaces, *entry;

	if (socket == LAN_NO_SOCKET || getifaddrs(&interfaces) != 0)
		return;
	for (entry = interfaces; entry; entry = entry->ifa_next)
	{
		uint32_t ip, mask, first, last;

		if (!entry->ifa_addr || !entry->ifa_netmask || entry->ifa_addr->sa_family != AF_INET
			|| !(entry->ifa_flags & IFF_UP) || !(entry->ifa_flags & IFF_BROADCAST))
			continue;
		ip = ntohl(((const struct sockaddr_in *)(const void *)entry->ifa_addr)->sin_addr.s_addr);
		mask = ntohl(((const struct sockaddr_in *)(const void *)entry->ifa_netmask)->sin_addr.s_addr);
		if (!lan_udp_sweep_range(ip, mask, &first, &last))
			continue;
		for (uint32_t address = first; address <= last; address++)
		{
			const lan_address to = { address, port };

			if (address != ip)
				lan_udp_send(socket, to, data, size);
		}
	}
	freeifaddrs(interfaces);
}
#else
void lan_udp_sweep(lan_socket socket, uint16_t port, const void *data, size_t size)
{
	(void)socket;
	(void)port;
	(void)data;
	(void)size;
}
#endif
