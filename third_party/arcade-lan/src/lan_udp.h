/**
 *    Name:        lan_udp.h
 *    Description: the library's sockets (lan_udp.c): UDP, non-blocking, IPv4
 *    Version:     0.2.0
 *    Created:     06-11-2026
 *    Modified:    11-11-2026
 *    Author:      Sebastian Toyos
 *
 *    license:BSD-3-Clause
 */
#ifndef ARCADE_LAN_UDP_H
#define ARCADE_LAN_UDP_H

#include "arcade_lan.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LAN_BROADCAST     0xFFFFFFFFu    /* 255.255.255.255: every device on the local network */
#define LAN_LOOPBACK      0x7F000001u    /* 127.0.0.1: this one (another game on the same computer) */

typedef intptr_t lan_socket;
#define LAN_NO_SOCKET     ((lan_socket)-1)

/* A UDP socket on 'port' (0: the system's choice), able to broadcast, non-blocking; 'shared' with the other games on
   the device (the beacons' port): LAN_NO_SOCKET where the device has no network */
lan_socket lan_udp_open(uint16_t port, bool shared);
void lan_udp_close(lan_socket socket);

/* The port it was given */
uint16_t lan_udp_port(lan_socket socket);

/* A datagram to 'to': false when it could not go */
bool lan_udp_send(lan_socket socket, lan_address to, const void *data, size_t size);

/* The next datagram that came, into 'data' (at most 'room' bytes), and who sent it: its size, 0 when none waits */
size_t lan_udp_receive(lan_socket socket, void *data, size_t room, lan_address *from);

/* Where the system refuses broadcasts (the library built with ARCADE_LAN_NO_BROADCAST: iOS without Apple's multicast
   entitlement): a datagram from 'socket' to 'port' of every other address of the device's local networks, one by
   one. Nothing elsewhere, where LAN_BROADCAST reaches them */
void lan_udp_sweep(lan_socket socket, uint16_t port, const void *data, size_t size);

/* An address of a private network (10.x, 172.16-31.x, 192.168.x); one of those or of this device (127.x): the ones a
   SEEK is answered to */
bool lan_udp_private(uint32_t ip);
bool lan_udp_local(uint32_t ip);

/* The addresses a sweep goes to, 'first' to 'last', on the network of the device's address 'ip' and its 'mask': a
   private network's alone (10.x, 172.16-31.x, 192.168.x), and of one larger than a /24 the /24 around the device;
   false where there are none */
bool lan_udp_sweep_range(uint32_t ip, uint32_t mask, uint32_t *first, uint32_t *last);

#endif
