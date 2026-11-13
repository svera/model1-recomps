/**
 *    Name:        lan.c
 *    Description: the rooms (a host's beacons, JOIN, the room's list) and the game, in lockstep through the host or
 *                 linked (each device's block to every other)
 *    Version:     0.3.0
 *    Created:     06-11-2026
 *    Modified:    11-11-2026
 *    Author:      Sebastian Toyos
 *
 *    license:BSD-3-Clause
 *    The rooms are outrunners-port's (console/network.c): a host's beacon to the network every half second, JOIN asked
 *    until the host's list has the device, the list sent by the host (with where it hears each device, and what each
 *    says of itself), a device unheard for a third of a second waited for and after 3 seconds gone. Every datagram is
 *    written byte by byte, little-endian, so that the devices need not be alike.
 *
 *    A device that hears no broadcast (iOS without Apple's multicast entitlement) hears no beacon, and its own are not
 *    heard. A device looking for rooms also sends a SEEK once a second, from the socket it hears the beacons at, to
 *    the seeks' port, which every room's host listens at and answers with its beacon, to where the SEEK came from (the
 *    beacons' port of the device that asks): to that port of its own address (a room hosted on the same computer) and,
 *    where the library is built with ARCADE_LAN_NO_BROADCAST, of every other address of its local networks, one by
 *    one (lan_udp_sweep). A room's host built so sends its beacon the same way once a second, to the beacons' port,
 *    unless the game holds it (lan_hold_sweep: a race) or plays in lockstep (its room takes no one then); from its
 *    own socket, as every beacon, which is where a device asks to JOIN. The beacon is the same and a SEEK is of no
 *    room, so a device without them takes neither for its own: the protocol's number stays. A SEEK is answered only
 *    at the seeks' port, to an address of a local network (a private one, or the device's own) and 16 times a second
 *    at most: it is anyone's to send, under any address, and a host is not to be made to send its beacons elsewhere.
 *
 *    The game: at its START every device runs frame 0 on, each frame only once it has every slot's inputs for it. A
 *    device's players' controls given at a frame play 'delay' frames on (the first 'delay' frames have none); it sends
 *    those the host has not put together yet (INPUT, with the frames it has from the host and its last state's hash),
 *    the host puts each frame's together as every slot's come (its own, the devices', none for a slot gone) and sends
 *    every device the frames it has not had yet (FRAMES). A frame can be put together only once every device has
 *    given its inputs for it, and a device gives them only as it runs its frames: no device is more than 'delay' and
 *    a datagram's frames apart from the others, and a ring of RING frames holds them all.
 *
 *    Linked (LAN_LINK): the START lists the game's devices (the room's then); each one answers it with a BLOCK, and
 *    sends its block to every other of the game (BLOCK) each time the game publishes one, to where the room's list
 *    says the host hears it until it hears that device itself (each one at the address first heard from it then). A
 *    device keeps every other's newest block; one unheard for 3 seconds (or that leaves) is gone for the rest of the
 *    game. The room takes devices while the game goes on, and the host's START starts another. The room's host gone
 *    (unheard for 3 seconds, or its LEAVE), each device takes it off its list, and the next one hosts the room on.
 */
#include "arcade_lan.h"
#include "lan_udp.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#define PROTOCOL            2          /* the library's datagrams: another's are not heard */
#define BEACON_FRAMES       30         /* a room's beacon, every half second */
#define LIST_FRAMES         30         /* and its list, to its devices */
#define SEEK_FRAMES         60         /* a search's SEEK, every second */
#define SWEEP_FRAMES        60         /* and a room's beacon to each address, where broadcasts are refused */
#define SEEKS_ANSWERED      16         /* the SEEKs a host answers in a second, at most */
#define ROOM_FORGOTTEN      180        /* a room unheard for 3 seconds is gone from the rooms found */
#define JOIN_FRAMES         10         /* JOIN again until the room lists the device */
#define JOIN_GIVEN_UP       300        /* a room that does not answer in 5 seconds */
#define SILENT_FRAMES       20         /* a device unheard for a third of a second: waited for */
#define LOST_FRAMES         180        /* and for 3 seconds: gone */
#define LEAVE_COPIES        3          /* a LEAVE sent this many times (one lost costs nothing) */
#define RING                64         /* the frames kept, by frame % RING */
#define WINDOW              (RING / 2) /* the frames past those put together that a device may give */
#define SENT_MOST           16         /* the frames a datagram carries */
#define PACKET_MOST         1400
#define SLOT_BYTES          LAN_INPUT_BYTES

static const char *const NO_NETWORK = "NO NETWORK";
static const char *const ROOM_FULL = "ROOM FULL";
static const char *const ROOM_PLAYING = "ROOM PLAYING";
static const char *const NO_ANSWER = "NO ANSWER";
static const char *const HOST_GONE = "HOST GONE";
static const char *const OUT_OF_STEP = "OUT OF STEP";
static const char *const LEFT_OUT = "LEFT OUT";

/* (PACKET_SEEK: a device looking for rooms asks their hosts for their beacons; of no room, a device that does not
   know it takes it for none of its own) */
enum { PACKET_BEACON = 1, PACKET_JOIN, PACKET_LIST, PACKET_LEAVE, PACKET_REFUSED, PACKET_START, PACKET_INPUT,
	PACKET_FRAMES, PACKET_BLOCK, PACKET_SEEK };
enum { REFUSED_FULL = 1, REFUSED_PLAYING };

#define MEMBER_GONE         0x01u      /* a LIST's member flags */
#define MEMBER_SILENT       0x02u
#define FRAMES_OUT_OF_STEP  0x01u      /* a FRAMES's flags */
#define NOBODY              0xFFu

/* A frame's inputs, every slot's (SLOT_BYTES apart), and which have come (the host's) or that it is whole */
typedef struct
{
	uint32_t frame;
	bool used;
	bool whole;
	uint8_t slots_in;
	uint8_t inputs[LAN_PLAYERS * SLOT_BYTES];
} frame_entry;

typedef struct
{
	uint32_t id;
	char name[LAN_NAME_LENGTH + 1];
	int players;
	int first_slot;
	lan_address address;       /* where its datagrams come from (the host's member 0: the host's) */
	uint32_t heard;            /* the clock when last heard */
	bool gone;
	bool silent;               /* (a client's list: as the host hears it) */
	uint8_t status;            /* what it says of itself (LAN_LINK) */
	/* the host's, in the game */
	uint32_t have;             /* the frames it has whole: [0, have) */
	uint8_t answered;          /* the START it answered */
	bool hash_new;
	uint32_t hash_frame;
	uint64_t hash;
} member;

/* LAN_LINK: a device of the game, as its START listed it */
typedef struct
{
	uint32_t id;
	char name[LAN_NAME_LENGTH + 1];
	int players;
	int first_slot;
	lan_address address;       /* where its blocks go */
	bool direct;               /* heard from that address itself (else the room's list's) */
	uint32_t heard;
	bool gone;                 /* unheard for 3 seconds, or left: for the rest of the game */
	uint8_t status;
	uint8_t answered;          /* (the host's) the START it answered */
	size_t block_size;         /* its newest block's: 0 while none has come */
	uint8_t block[LAN_BLOCK_MOST];
} link_member;

struct lan_device
{
	lan_config config;
	lan_socket socket, beacon_socket;
	lan_socket seek_socket;    /* a room's host's: where it hears the devices looking for rooms */
	bool sweep_held;           /* the game's (lan_hold_sweep) */
	uint32_t id;
	uint32_t random;           /* the tests' losses */
	char name[LAN_NAME_LENGTH + 1];
	int players;
	lan_role role;
	uint32_t clock;            /* lan_update's count */
	const char *problem;

	/* the room: its id (its host's), its list (the host first), the host's address (a client's) */
	uint32_t room;
	member members[LAN_DEVICES];
	int member_count;
	uint16_t list_version;
	bool list_changed;
	lan_address host_address;
	uint32_t joining_since;
	uint32_t seeking_since;    /* the clock when it began to look for rooms */
	uint32_t answering_since;  /* the host's: the second its answers to SEEKs are counted in, */
	int seeks_answered;        /* and how many */

	/* the rooms found */
	lan_room rooms[LAN_ROOMS];
	uint32_t rooms_heard[LAN_ROOMS];
	int room_count;

	/* the game */
	uint8_t serial;            /* its START's */
	uint8_t settings[LAN_SETTINGS_MOST];
	size_t settings_size;
	bool started_new;
	int first_slot;
	uint32_t next;             /* the frame run next */
	uint32_t given;            /* the frame its next inputs given are for */
	uint32_t whole;            /* the frames [0, whole) whole: every slot's in */
	frame_entry local[RING];   /* its players' inputs, by frame */
	frame_entry frames[RING];  /* every slot's */
	uint64_t hashes[RING];     /* its state's after each frame (the host compares) */
	uint32_t hash_frames[RING];
	bool hash_set[RING];
	uint32_t hash_frame;       /* a client's last, for its INPUTs */
	uint64_t hash;
	bool hash_known;
	bool out_of_step;
	uint8_t waiting_for;       /* the member the frames wait for (NOBODY) */

	/* LAN_LINK */
	uint8_t status;            /* what this device says of itself */
	link_member game[LAN_DEVICES];    /* the game's devices, as its START listed them */
	int game_count;
};

/* ---- bytes ---- */

typedef struct
{
	uint8_t bytes[PACKET_MOST];
	size_t size;
} writer;

typedef struct
{
	const uint8_t *bytes;
	size_t size;
	size_t at;
	bool short_of;             /* read past its end */
} reader;

static void put8(writer *out, uint32_t value)
{
	if (out->size < sizeof(out->bytes))
		out->bytes[out->size] = (uint8_t)value;
	out->size++;
}

static void put16(writer *out, uint32_t value)
{
	put8(out, value);
	put8(out, value >> 8);
}

static void put32(writer *out, uint32_t value)
{
	put16(out, value);
	put16(out, value >> 16);
}

static void put64(writer *out, uint64_t value)
{
	put32(out, (uint32_t)value);
	put32(out, (uint32_t)(value >> 32));
}

static void put_name(writer *out, const char *name)
{
	const size_t length = strlen(name);

	for (size_t at = 0; at < LAN_NAME_LENGTH; at++)
		put8(out, at < length ? (uint8_t)name[at] : 0u);
}

static uint32_t get8(reader *in)
{
	if (in->at >= in->size)
	{
		in->short_of = true;
		return 0;
	}
	return in->bytes[in->at++];
}

static uint32_t get16(reader *in)
{
	const uint32_t low = get8(in);

	return low | get8(in) << 8;
}

static uint32_t get32(reader *in)
{
	const uint32_t low = get16(in);

	return low | get16(in) << 16;
}

static uint64_t get64(reader *in)
{
	const uint64_t low = get32(in);

	return low | (uint64_t)get32(in) << 32;
}

/* A name as the library keeps it: A-Z, 0-9, '.', '-' (lower case raised), anything else a space */
static void clean_name(char name[LAN_NAME_LENGTH + 1], const char *from)
{
	int at = 0;

	for (; at < LAN_NAME_LENGTH && from[at]; at++)
	{
		char letter = from[at];

		if (letter >= 'a' && letter <= 'z')
			letter = (char)(letter - 'a' + 'A');
		name[at] = (letter >= 'A' && letter <= 'Z') || (letter >= '0' && letter <= '9') || letter == '.'
			|| letter == '-' ? letter : ' ';
	}
	name[at] = '\0';
}

static void get_name(reader *in, char name[LAN_NAME_LENGTH + 1])
{
	char raw[LAN_NAME_LENGTH + 1];

	for (int at = 0; at < LAN_NAME_LENGTH; at++)
		raw[at] = (char)get8(in);
	raw[LAN_NAME_LENGTH] = '\0';
	clean_name(name, raw);
}

/* A datagram's start: the game, the library's and the game's versions, its kind, who sends it, of what room */
static void put_header(const lan_device *device, writer *out, int kind)
{
	out->size = 0;
	for (int at = 0; at < 4; at++)
		put8(out, (uint8_t)device->config.game[at]);
	put8(out, PROTOCOL);
	put8(out, device->config.version);
	put8(out, (uint32_t)kind);
	put32(out, device->id);
	put32(out, device->room);
}

static void send_from(lan_device *device, lan_socket socket, lan_address to, const writer *out)
{
	if (out->size > sizeof(out->bytes) || !to.ip)
		return;
	if (device->config.loss > 0)
	{
		device->random = device->random * 1103515245u + 12345u;
		if ((int)((device->random >> 16) % 100u) < device->config.loss)
			return;
	}
	lan_udp_send(socket, to, out->bytes, out->size);
}

/* From the device's own socket: every datagram but a SEEK */
static void send_datagram(lan_device *device, lan_address to, const writer *out)
{
	send_from(device, device->socket, to, out);
}

/* ---- the room ---- */

static int member_index(const lan_device *device, uint32_t id)
{
	for (int index = 0; index < device->member_count; index++)
		if (device->members[index].id == id)
			return index;
	return -1;
}

static int room_players(const lan_device *device)
{
	int players = 0;

	for (int index = 0; index < device->member_count; index++)
		players += device->members[index].players;
	return players;
}

/* The slots in the list's order */
static void give_slots(lan_device *device)
{
	int slot = 0;

	for (int index = 0; index < device->member_count; index++)
	{
		device->members[index].first_slot = slot;
		slot += device->members[index].players;
	}
}

/* The slots' bits of a member */
static uint8_t slot_bits(const member *who)
{
	uint8_t bits = 0;

	for (int player = 0; player < who->players; player++)
		if (who->first_slot + player < LAN_PLAYERS)
			bits |= (uint8_t)(1u << (who->first_slot + player));
	return bits;
}

/* The seeks' port: the game's, or the one after its beacons' */
static uint16_t seek_port(const lan_device *device)
{
	return device->config.seek_port ? device->config.seek_port : (uint16_t)(device->config.beacon_port + 1u);
}

/* A room's host: the seeks' port listened at (shared with the other games on the device; of several rooms hosted on
   one, a SEEK reaches one) */
static void hear_seeks(lan_device *device)
{
	if (device->seek_socket == LAN_NO_SOCKET)
		device->seek_socket = lan_udp_open(seek_port(device), true);
}

static void leave_room(lan_device *device, lan_role role)
{
	lan_udp_close(device->seek_socket);
	device->seek_socket = LAN_NO_SOCKET;
	device->role = role;
	device->room = 0;
	device->member_count = 0;
	device->game_count = 0;
	device->waiting_for = NOBODY;
}

static void set_problem(lan_device *device, const char *problem)
{
	device->problem = problem;
}

static bool linked(const lan_device *device)
{
	return device->config.mode == LAN_LINK;
}

/* LAN_LINK: the game's device of id 'id', or NULL */
static link_member *link_member_of(lan_device *device, uint32_t id)
{
	for (int index = 0; index < device->game_count; index++)
		if (device->game[index].id == id)
			return &device->game[index];
	return NULL;
}

/* A player's input's bytes in lockstep, within a slot's (none linked) */
static int input_width(const lan_device *device)
{
	const int bytes = device->config.input_bytes;

	return linked(device) || bytes < 0 ? 0 : (bytes > SLOT_BYTES ? SLOT_BYTES : bytes);
}

/* The host: a device off the room's list (LAN_LINK: gone from the game too) */
static void remove_member(lan_device *device, int index)
{
	link_member *was = linked(device) ? link_member_of(device, device->members[index].id) : NULL;

	if (was)
		was->gone = true;
	memmove(&device->members[index], &device->members[index + 1],
		(size_t)(device->member_count - index - 1) * sizeof(member));
	device->member_count--;
	give_slots(device);
	device->list_version++;
	device->list_changed = true;
}

/* ---- the game's frames ---- */

/* A ring's entry for 'frame', cleared when it held another */
static frame_entry *entry_for(frame_entry ring[RING], uint32_t frame)
{
	frame_entry *entry = &ring[frame % RING];

	if (!entry->used || entry->frame != frame)
	{
		memset(entry, 0, sizeof(*entry));
		entry->frame = frame;
		entry->used = true;
	}
	return entry;
}

/* The entry of 'frame' when the ring holds it, else NULL */
static frame_entry *entry_of(frame_entry ring[RING], uint32_t frame)
{
	frame_entry *entry = &ring[frame % RING];

	return entry->used && entry->frame == frame ? entry : NULL;
}

/* The game from frame 0: the first 'delay' frames whole with no inputs */
static void start_game(lan_device *device)
{
	memset(device->local, 0, sizeof(device->local));
	memset(device->frames, 0, sizeof(device->frames));
	memset(device->hash_set, 0, sizeof(device->hash_set));
	device->next = 0;
	device->given = (uint32_t)device->config.delay;
	device->whole = (uint32_t)device->config.delay;
	for (uint32_t frame = 0; frame < (uint32_t)device->config.delay; frame++)
	{
		entry_for(device->local, frame);
		entry_for(device->frames, frame)->whole = true;
	}
	for (int index = 0; index < device->member_count; index++)
	{
		device->members[index].have = (uint32_t)device->config.delay;
		device->members[index].hash_new = false;
		device->members[index].gone = false;
		device->members[index].heard = device->clock;
	}
	device->first_slot = 0;
	for (int index = 0; index < device->member_count; index++)
		if (device->members[index].id == device->id)
			device->first_slot = device->members[index].first_slot;
	device->hash_known = false;
	device->out_of_step = false;
	device->waiting_for = NOBODY;
	device->started_new = true;
	device->role = LAN_PLAYING;
}

/* The host: a member's slots' inputs put in a frame */
static void take_slots(lan_device *device, const member *who, uint32_t frame, const uint8_t *inputs)
{
	frame_entry *entry;

	if (frame < device->whole || frame >= device->whole + WINDOW)
		return;
	entry = entry_for(device->frames, frame);
	for (int player = 0; player < who->players; player++)
	{
		const int slot = who->first_slot + player;

		if (slot < LAN_PLAYERS)
			memcpy(entry->inputs + slot * SLOT_BYTES, inputs + player * SLOT_BYTES, SLOT_BYTES);
	}
	entry->slots_in |= slot_bits(who);
}

/* The host: the frames whole as every slot of a device not gone has come (a gone one's none) */
static void put_frames_together(lan_device *device)
{
	uint8_t needed = 0;

	for (int index = 0; index < device->member_count; index++)
		if (!device->members[index].gone)
			needed |= slot_bits(&device->members[index]);
	device->waiting_for = NOBODY;
	for (;;)
	{
		frame_entry *entry = entry_for(device->frames, device->whole);

		if ((entry->slots_in & needed) != needed)
		{
			/* (who the frames wait for: the first device missing, if it is unheard a while) */
			for (int index = 0; index < device->member_count; index++)
			{
				const member *who = &device->members[index];

				if (who->id != device->id && !who->gone && (entry->slots_in & slot_bits(who)) != slot_bits(who)
					&& device->clock - who->heard > SILENT_FRAMES)
				{
					device->waiting_for = (uint8_t)index;
					break;
				}
			}
			return;
		}
		for (int index = 0; index < device->member_count; index++)
			if (device->members[index].gone)
				for (int player = 0; player < device->members[index].players; player++)
				{
					const int slot = device->members[index].first_slot + player;

					if (slot < LAN_PLAYERS)
						memset(entry->inputs + slot * SLOT_BYTES, 0, SLOT_BYTES);
				}
		entry->whole = true;
		device->whole++;
	}
}

/* The host: a device's state's hash against its own of the same frame */
static void compare_hash(lan_device *device, member *who)
{
	const int at = (int)(who->hash_frame % RING);

	if (!who->hash_new || !device->hash_set[at] || device->hash_frames[at] != who->hash_frame)
		return;
	who->hash_new = false;
	if (device->hashes[at] != who->hash && !device->out_of_step)
	{
		device->out_of_step = true;
		set_problem(device, OUT_OF_STEP);
	}
}

/* ---- the datagrams ---- */

static void put_beacon(const lan_device *device, writer *out)
{
	put_header(device, out, PACKET_BEACON);
	put_name(out, device->name);
	put8(out, (uint32_t)device->member_count);
	put8(out, (uint32_t)room_players(device));
	put8(out, device->role == LAN_PLAYING && !linked(device) ? 1u : 0u);    /* (a linked room takes devices) */
}

static void send_beacon(lan_device *device)
{
	writer out;
	const lan_address everyone = { LAN_BROADCAST, device->config.beacon_port };
	const lan_address this_one = { LAN_LOOPBACK, device->config.beacon_port };

	put_beacon(device, &out);
	send_datagram(device, everyone, &out);
	send_datagram(device, this_one, &out);
}

/* Where broadcasts are refused: the room's beacon to every other address of the device's networks (nothing
   elsewhere) */
static void sweep_beacon(lan_device *device)
{
	writer out;

	put_beacon(device, &out);
	lan_udp_sweep(device->socket, device->config.beacon_port, out.bytes, out.size);
}

/* The host: its beacon to a device that asks for it, at the address it asks from (its beacons' port). To a device of
   a local network alone (a private address, or this device's), and so many a second: a SEEK is anyone's to send, with
   any address as its own, and the host's answers are not to be sent to whoever that names */
static void answer_seek(lan_device *device, lan_address to)
{
	writer out;

	if (!lan_udp_local(to.ip))
		return;
	if (device->clock - device->answering_since >= SEEK_FRAMES)
	{
		device->answering_since = device->clock;
		device->seeks_answered = 0;
	}
	if (device->seeks_answered >= SEEKS_ANSWERED)
		return;
	device->seeks_answered++;
	put_beacon(device, &out);
	send_datagram(device, to, &out);
}

/* Looking for rooms: SEEK to the seeks' port of this device (a room hosted on the same computer) and, where broadcasts
   are refused, of every other address of its networks; a room's host answers each with its beacon. From the socket
   the beacons are heard at: its answer comes there, and the sweep is none of the device's own socket's */
static void send_seek(lan_device *device)
{
	writer out;
	const lan_address this_one = { LAN_LOOPBACK, seek_port(device) };

	put_header(device, &out, PACKET_SEEK);
	send_from(device, device->beacon_socket, this_one, &out);
	lan_udp_sweep(device->beacon_socket, this_one.port, out.bytes, out.size);
}

/* JOIN: asked of a room's host, and again while in it (heard by it, with what this device says of itself) */
static void send_join(lan_device *device)
{
	writer out;

	put_header(device, &out, PACKET_JOIN);
	put_name(&out, device->name);
	put8(&out, (uint32_t)device->players);
	put8(&out, device->status);
	send_datagram(device, device->host_address, &out);
}

/* The host: its list, each device with where the host hears it and what it says of itself */
static void send_list(lan_device *device)
{
	writer out;

	put_header(device, &out, PACKET_LIST);
	put16(&out, device->list_version);
	put8(&out, (uint32_t)device->member_count);
	for (int index = 0; index < device->member_count; index++)
	{
		const member *who = &device->members[index];

		put32(&out, who->id);
		put_name(&out, who->name);
		put8(&out, (uint32_t)who->players);
		put8(&out, (who->gone ? MEMBER_GONE : 0u)
			| (index && device->clock - who->heard > SILENT_FRAMES ? MEMBER_SILENT : 0u));
		put32(&out, index ? who->address.ip : 0u);
		put16(&out, index ? who->address.port : 0u);
		put8(&out, index ? who->status : device->status);
	}
	for (int index = 1; index < device->member_count; index++)
		if (!device->members[index].gone)
			send_datagram(device, device->members[index].address, &out);
}

static void send_refused(lan_device *device, lan_address to, int reason)
{
	writer out;

	put_header(device, &out, PACKET_REFUSED);
	put8(&out, (uint32_t)reason);
	send_datagram(device, to, &out);
}

/* The host's START: the game's devices (the room's; LAN_LINK: as the game listed them, whatever the room's list is
   now) and its settings */
static void send_start(lan_device *device, lan_address to)
{
	writer out;
	const int count = linked(device) ? device->game_count : device->member_count;

	put_header(device, &out, PACKET_START);
	put8(&out, device->serial);
	put8(&out, (uint32_t)count);
	for (int index = 0; index < count; index++)
	{
		put32(&out, linked(device) ? device->game[index].id : device->members[index].id);
		put8(&out, (uint32_t)(linked(device) ? device->game[index].players : device->members[index].players));
	}
	put16(&out, (uint32_t)device->settings_size);
	for (size_t at = 0; at < device->settings_size; at++)
		put8(&out, device->settings[at]);
	send_datagram(device, to, &out);
}

/* A client: its players' inputs the host has not put together yet, the frames it has, its last hash */
static void send_input(lan_device *device)
{
	writer out;
	uint32_t from = device->whole > device->given ? device->given : device->whole;
	const int bytes = input_width(device);

	if (device->given - from > SENT_MOST)
		from = device->given - SENT_MOST;
	put_header(device, &out, PACKET_INPUT);
	put8(&out, device->serial);
	put32(&out, device->whole);
	put32(&out, from);
	put8(&out, device->given - from);
	for (uint32_t frame = from; frame < device->given; frame++)
	{
		const frame_entry *entry = entry_of(device->local, frame);

		for (int player = 0; player < device->players; player++)
			for (int at = 0; at < bytes; at++)
				put8(&out, entry ? entry->inputs[player * SLOT_BYTES + at] : 0u);
	}
	put8(&out, device->hash_known ? 1u : 0u);
	put32(&out, device->hash_frame);
	put64(&out, device->hash);
	send_datagram(device, device->host_address, &out);
}

/* The host: a device's frames whole it has not had yet */
static void send_frames(lan_device *device, const member *to)
{
	writer out;
	uint32_t from = to->have, until;
	const int bytes = input_width(device);

	if (from + WINDOW < device->whole)
		from = device->whole - WINDOW;    /* (a device that far behind has lost the game) */
	until = device->whole - from > SENT_MOST ? from + SENT_MOST : device->whole;
	put_header(device, &out, PACKET_FRAMES);
	put8(&out, device->serial);
	put8(&out, device->out_of_step ? FRAMES_OUT_OF_STEP : 0u);
	put8(&out, device->waiting_for);
	put32(&out, from);
	put8(&out, until - from);
	for (uint32_t frame = from; frame < until; frame++)
	{
		const frame_entry *entry = entry_of(device->frames, frame);

		for (int slot = 0; slot < LAN_PLAYERS; slot++)
			for (int at = 0; at < bytes; at++)
				put8(&out, entry ? entry->inputs[slot * SLOT_BYTES + at] : 0u);
	}
	send_datagram(device, to->address, &out);
}

static void send_leave(lan_device *device, lan_address to)
{
	writer out;

	put_header(device, &out, PACKET_LEAVE);
	send_datagram(device, to, &out);
}

/* ---- the linked game (LAN_LINK) ---- */

/* A block to a device of the game (none: the START answered) */
static void send_block(lan_device *device, const link_member *to, const uint8_t *block, size_t size)
{
	writer out;

	if (!to->address.ip)
		return;
	put_header(device, &out, PACKET_BLOCK);
	put8(&out, device->serial);
	put8(&out, device->status);
	put16(&out, (uint32_t)size);
	for (size_t at = 0; at < size; at++)
		put8(&out, block[at]);
	send_datagram(device, to->address, &out);
}

/* The game linked: its devices the START's ('ids' and their 'players', in its order), their names, where they are
   heard and what they say of themselves as the room's list has them */
static void start_link(lan_device *device, const uint32_t *ids, const int *players, int count)
{
	int slot = 0;

	memset(device->game, 0, sizeof(device->game));
	device->game_count = count;
	device->first_slot = 0;
	for (int index = 0; index < count; index++)
	{
		link_member *each = &device->game[index];
		const int known = member_index(device, ids[index]);

		each->id = ids[index];
		each->players = players[index];
		each->first_slot = slot;
		slot += players[index];
		each->heard = device->clock;
		if (known >= 0)
		{
			memcpy(each->name, device->members[known].name, sizeof(each->name));
			each->address = device->members[known].address;
			each->status = device->members[known].status;
		}
		if (each->id == device->id)
		{
			each->answered = device->serial;
			device->first_slot = each->first_slot;
		}
	}
	device->waiting_for = NOBODY;
	device->started_new = true;
	device->role = LAN_PLAYING;
}

/* The game's devices unheard for 3 seconds: gone for the rest of the game */
static void link_frame(lan_device *device)
{
	for (int index = 0; index < device->game_count; index++)
	{
		link_member *each = &device->game[index];

		if (each->id != device->id && !each->gone && device->clock - each->heard > LOST_FRAMES)
			each->gone = true;
	}
}

/* A device of the room: its host gone, off its list (and out of the game), the next one hosts the room on (this
   one, or the one the room is heard from then): false where none is left that it can reach */
static bool take_over(lan_device *device)
{
	link_member *was = link_member_of(device, device->members[0].id);

	if (was)
		was->gone = true;
	memmove(&device->members[0], &device->members[1], (size_t)(device->member_count - 1) * sizeof(member));
	device->member_count--;
	if (device->member_count == 0 || (device->members[0].id != device->id && !device->members[0].address.ip))
		return false;
	for (int index = 0; index < device->member_count; index++)
		device->members[index].heard = device->clock;
	give_slots(device);
	if (device->members[0].id == device->id)
	{
		device->list_version++;
		device->list_changed = true;
		for (int index = 0; index < device->game_count; index++)
			device->game[index].answered = device->serial;    /* (its START had come from the host gone) */
		hear_seeks(device);
		send_beacon(device);
	}
	else
	{
		/* (where this device hears it itself, its blocks', before where the host gone heard it) */
		const link_member *next = link_member_of(device, device->members[0].id);

		device->host_address = next && next->direct ? next->address : device->members[0].address;
		device->members[0].address = device->host_address;
	}
	return true;
}

/* ---- what comes ---- */

/* A room's beacon, while looking for rooms: its host's to everyone, or its answer to this device's SEEK */
static void heard_beacon(lan_device *device, reader *in, uint32_t room, lan_address from)
{
	lan_room heard;
	int index;

	memset(&heard, 0, sizeof(heard));
	heard.room = room;
	get_name(in, heard.name);
	heard.devices = (int)get8(in);
	heard.players = (int)get8(in);
	heard.playing = get8(in) != 0u;
	heard.host = from;
	if (in->short_of || !room)
		return;
	for (index = 0; index < device->room_count && device->rooms[index].room != room; index++)
		;
	if (index == device->room_count)
	{
		if (device->room_count == LAN_ROOMS)
			return;
		device->room_count++;
	}
	device->rooms[index] = heard;
	device->rooms_heard[index] = device->clock;
}

/* The host: a device asks to join */
static void heard_join(lan_device *device, reader *in, uint32_t sender, lan_address from)
{
	char name[LAN_NAME_LENGTH + 1];
	int players, index;
	uint8_t status;

	get_name(in, name);
	players = (int)get8(in);
	status = (uint8_t)get8(in);
	if (in->short_of || players < 1 || players > LAN_PLAYERS)
		return;
	index = member_index(device, sender);
	if (index >= 0)
	{
		device->members[index].address = from;
		device->members[index].heard = device->clock;
		device->members[index].status = status;
		device->list_changed = true;    /* (its JOIN again: the list it has not had) */
		return;
	}
	if (device->role == LAN_PLAYING && !linked(device))
	{
		send_refused(device, from, REFUSED_PLAYING);
		return;
	}
	if (device->member_count >= (device->config.max_devices ? device->config.max_devices : LAN_DEVICES)
		|| (!linked(device) && room_players(device) + players > LAN_PLAYERS))
	{
		send_refused(device, from, REFUSED_FULL);
		return;
	}
	index = device->member_count++;
	memset(&device->members[index], 0, sizeof(device->members[index]));
	device->members[index].id = sender;
	memcpy(device->members[index].name, name, sizeof(name));
	device->members[index].players = players;
	device->members[index].address = from;
	device->members[index].heard = device->clock;
	device->members[index].status = status;
	give_slots(device);
	device->list_version++;
	device->list_changed = true;
}

/* A client: its host's list */
static void heard_list(lan_device *device, reader *in)
{
	member members[LAN_DEVICES];
	const int count = ((void)get16(in), (int)get8(in));    /* (its version: the list as it is now) */
	bool listed = false;

	if (count < 1 || count > LAN_DEVICES)
		return;
	memset(members, 0, sizeof(members));
	for (int index = 0; index < count; index++)
	{
		uint8_t flags;

		members[index].id = get32(in);
		get_name(in, members[index].name);
		members[index].players = (int)get8(in);
		flags = (uint8_t)get8(in);
		members[index].gone = (flags & MEMBER_GONE) != 0;
		members[index].silent = (flags & MEMBER_SILENT) != 0;
		members[index].address.ip = get32(in);
		members[index].address.port = (uint16_t)get16(in);
		members[index].status = (uint8_t)get8(in);
		listed = listed || members[index].id == device->id;
	}
	if (in->short_of)
		return;
	if (!listed)
	{
		/* (left out of its list: LAN_LINK says so, its game going on alone) */
		if (device->role == LAN_IN_ROOM || (linked(device) && device->role == LAN_PLAYING))
		{
			leave_room(device, LAN_OFF);
			if (linked(device))
				set_problem(device, LEFT_OUT);
		}
		return;
	}
	if (device->role == LAN_PLAYING && !linked(device))
	{
		/* (the game's slots stay those of its START: of the list, who is gone or silent) */
		for (int index = 0; index < count; index++)
		{
			const int known = member_index(device, members[index].id);

			if (known >= 0)
			{
				device->members[known].gone = members[index].gone;
				device->members[known].silent = members[index].silent;
			}
		}
		device->members[0].heard = device->clock;
		return;
	}
	memcpy(device->members, members, sizeof(members));
	device->member_count = count;
	device->members[0].address = device->host_address;
	device->members[0].heard = device->clock;
	give_slots(device);
	if (device->role == LAN_JOINING)
		device->role = LAN_IN_ROOM;
}

/* A client: the host's START */
static void heard_start(lan_device *device, reader *in)
{
	const uint8_t serial = (uint8_t)get8(in);
	const int count = (int)get8(in);
	member members[LAN_DEVICES];
	size_t size;

	if (device->role == LAN_PLAYING && serial == device->serial)
	{
		/* (its START again: not answered yet; LAN_LINK answers it once more) */
		link_member *host = linked(device) && device->member_count > 0 ? link_member_of(device, device->members[0].id) : NULL;

		if (host)
			send_block(device, host, NULL, 0);
		return;
	}
	if (count < 1 || count > LAN_DEVICES)
		return;
	memset(members, 0, sizeof(members));
	for (int index = 0; index < count; index++)
	{
		members[index].id = get32(in);
		members[index].players = (int)get8(in);
	}
	size = get16(in);
	if (size > LAN_SETTINGS_MOST)
		return;
	for (size_t at = 0; at < size; at++)
		device->settings[at] = (uint8_t)get8(in);
	if (in->short_of || member_index(device, device->id) < 0)
		return;
	if (linked(device))
	{
		/* the game's devices, the room's list as it is (it takes devices while the game goes on) */
		uint32_t ids[LAN_DEVICES];
		int players[LAN_DEVICES];
		bool listed = false;
		link_member *host;

		for (int index = 0; index < count; index++)
		{
			ids[index] = members[index].id;
			players[index] = members[index].players < 1 || members[index].players > LAN_PLAYERS ? 1 : members[index].players;
			listed = listed || ids[index] == device->id;
		}
		if (!listed)
			return;
		device->settings_size = size;
		device->serial = serial;
		start_link(device, ids, players, count);
		host = link_member_of(device, device->members[0].id);
		if (host)
			send_block(device, host, NULL, 0);    /* (its answer) */
		return;
	}
	for (int index = 0; index < count; index++)
	{
		const int known = member_index(device, members[index].id);

		if (known >= 0)
			memcpy(members[index].name, device->members[known].name, sizeof(members[index].name));
	}
	memcpy(device->members, members, sizeof(members));
	device->member_count = count;
	device->members[0].address = device->host_address;
	give_slots(device);
	device->settings_size = size;
	device->serial = serial;
	start_game(device);
}

/* The host: a device's inputs */
static void heard_input(lan_device *device, reader *in, member *who)
{
	const uint8_t serial = (uint8_t)get8(in);
	const uint32_t have = get32(in);
	const uint32_t from = get32(in);
	const int count = (int)get8(in);
	const int bytes = input_width(device);
	uint8_t inputs[SENT_MOST][LAN_PLAYERS * SLOT_BYTES];
	bool hash_known;

	if (device->role != LAN_PLAYING || serial != device->serial || count > SENT_MOST)
		return;
	memset(inputs, 0, sizeof(inputs));
	for (int frame = 0; frame < count; frame++)
		for (int player = 0; player < who->players; player++)
			for (int at = 0; at < bytes; at++)
				inputs[frame][player * SLOT_BYTES + at] = (uint8_t)get8(in);
	hash_known = get8(in) != 0u;
	who->hash_frame = get32(in);
	who->hash = get64(in);
	if (in->short_of)
		return;
	who->answered = serial;
	if (have > who->have && have <= device->whole)
		who->have = have;
	if (!who->gone)
		for (int frame = 0; frame < count; frame++)
			take_slots(device, who, from + (uint32_t)frame, inputs[frame]);
	who->hash_new = hash_known;
	compare_hash(device, who);
}

/* A client: the host's frames */
static void heard_frames(lan_device *device, reader *in)
{
	const uint8_t serial = (uint8_t)get8(in);
	const uint8_t flags = (uint8_t)get8(in);
	const uint8_t waiting_for = (uint8_t)get8(in);
	const uint32_t from = get32(in);
	const int count = (int)get8(in);
	const int bytes = input_width(device);

	if (device->role != LAN_PLAYING || serial != device->serial || count > SENT_MOST)
		return;
	for (int frame = 0; frame < count; frame++)
	{
		uint8_t inputs[LAN_PLAYERS * SLOT_BYTES];
		const uint32_t number = from + (uint32_t)frame;

		memset(inputs, 0, sizeof(inputs));
		for (int slot = 0; slot < LAN_PLAYERS; slot++)
			for (int at = 0; at < bytes; at++)
				inputs[slot * SLOT_BYTES + at] = (uint8_t)get8(in);
		if (in->short_of)
			return;
		if (number >= device->whole && number < device->whole + WINDOW)
		{
			frame_entry *entry = entry_for(device->frames, number);

			memcpy(entry->inputs, inputs, sizeof(inputs));
			entry->whole = true;
		}
	}
	while (entry_of(device->frames, device->whole) && entry_of(device->frames, device->whole)->whole)
		device->whole++;
	device->waiting_for = waiting_for;
	if ((flags & FRAMES_OUT_OF_STEP) && !device->out_of_step)
	{
		device->out_of_step = true;
		set_problem(device, OUT_OF_STEP);
	}
}

/* The host: a device leaves (out of the list in a room; gone in a game in lockstep; LAN_LINK: both) */
static void heard_leave(lan_device *device, int index)
{
	if (device->role == LAN_PLAYING && !linked(device))
	{
		device->members[index].gone = true;
		device->list_changed = true;
		return;
	}
	remove_member(device, index);
}

/* LAN_LINK: a device's block, or its answer to the START (none), straight from it: kept as its newest */
static void heard_block(lan_device *device, reader *in, uint32_t sender, lan_address from)
{
	uint8_t block[LAN_BLOCK_MOST];
	const uint8_t serial = (uint8_t)get8(in);
	const uint8_t status = (uint8_t)get8(in);
	const size_t size = get16(in);
	const int index = member_index(device, sender);
	link_member *who;

	if (size > LAN_BLOCK_MOST)
		return;
	for (size_t at = 0; at < size; at++)
		block[at] = (uint8_t)get8(in);
	who = link_member_of(device, sender);
	if (in->short_of || device->role != LAN_PLAYING || serial != device->serial || !who || who->gone)
		return;
	if (who->direct && (who->address.ip != from.ip || who->address.port != from.port))
		return;    /* (its id from another address: not its) */
	who->address = from;
	who->direct = true;
	who->heard = device->clock;
	who->status = status;
	who->answered = serial;
	if (index >= 0)
		device->members[index].status = status;
	if (size > 0)
	{
		memcpy(who->block, block, size);
		who->block_size = size;
	}
}

/* Where a datagram came: the device's own socket, the beacons' port, the seeks' */
typedef enum { AT_OWN, AT_BEACONS, AT_SEEKS } heard_at;

/* A datagram that came: a beacon is taken at the beacons' port alone, a SEEK at the seeks', the rest at the device's
   own socket */
static void take_datagram(lan_device *device, const uint8_t *bytes, size_t size, lan_address from, heard_at at_port)
{
	reader in = { bytes, size, 0, false };
	int kind, index;
	uint32_t sender, room;

	for (int at = 0; at < 4; at++)
		if (get8(&in) != (uint8_t)device->config.game[at])
			return;
	if (get8(&in) != PROTOCOL || get8(&in) != device->config.version)
		return;
	kind = (int)get8(&in);
	sender = get32(&in);
	room = get32(&in);
	if (in.short_of || sender == device->id)
		return;
	if (kind == PACKET_BEACON)
	{
		if (at_port == AT_BEACONS && device->role == LAN_SEARCHING)
			heard_beacon(device, &in, room, from);
		return;
	}
	if (kind == PACKET_SEEK)
	{
		if (at_port == AT_SEEKS && lan_hosting(device))
			answer_seek(device, from);
		return;
	}
	if (at_port != AT_OWN)
		return;
	if (lan_hosting(device))
	{
		if (room != device->room && !(kind == PACKET_JOIN && room == 0))    /* (0: joined by the host's address) */
			return;
		if (kind == PACKET_JOIN)
		{
			heard_join(device, &in, sender, from);
			return;
		}
		index = member_index(device, sender);
		if (index <= 0)
			return;
		device->members[index].heard = device->clock;
		device->members[index].address = from;
		if (kind == PACKET_INPUT && !linked(device))
			heard_input(device, &in, &device->members[index]);
		else if (kind == PACKET_LEAVE)
			heard_leave(device, index);
		else if (kind == PACKET_BLOCK && linked(device))
			heard_block(device, &in, sender, from);
		return;
	}
	/* a client: only its host (joined by its address, its room as its first answer says) */
	if (device->role == LAN_JOINING && device->room == 0 && from.ip == device->host_address.ip
		&& from.port == device->host_address.port)
		device->room = room;
	/* LAN_LINK: the game's devices too, straight from each (a block; a LEAVE: gone at once); the host where its
	   blocks come from, if that is not where the room's list said */
	if (linked(device) && room == device->room && device->member_count > 0
		&& (device->role == LAN_IN_ROOM || device->role == LAN_PLAYING))
	{
		link_member *who = link_member_of(device, sender);
		const bool from_it = who && who->direct && who->address.ip == from.ip && who->address.port == from.port;

		if (sender == device->members[0].id)
		{
			if (kind == PACKET_BLOCK)
			{
				/* (its block: where it comes from, the host's from then on) */
				heard_block(device, &in, sender, from);
				if (who && who->direct && who->address.ip == from.ip && who->address.port == from.port)
				{
					device->host_address = device->members[0].address = from;
					device->members[0].heard = device->clock;
				}
				return;
			}
			if (from_it && (from.ip != device->host_address.ip || from.port != device->host_address.port))
				device->host_address = device->members[0].address = from;
		}
		else
		{
			if (kind == PACKET_BLOCK)
				heard_block(device, &in, sender, from);
			else if (kind == PACKET_LEAVE && who && (!who->direct || from_it))
				who->gone = true;
			return;
		}
	}
	if ((device->role != LAN_JOINING && device->role != LAN_IN_ROOM && device->role != LAN_PLAYING)
		|| room != device->room || from.ip != device->host_address.ip || from.port != device->host_address.port)
		return;
	if (device->member_count > 0)
		device->members[0].heard = device->clock;
	switch (kind)
	{
		case PACKET_LIST:
			heard_list(device, &in);
			break;
		case PACKET_REFUSED:
			if (device->role == LAN_JOINING)
			{
				const int reason = (int)get8(&in);

				leave_room(device, LAN_OFF);
				set_problem(device, reason == REFUSED_PLAYING ? ROOM_PLAYING : ROOM_FULL);
			}
			break;
		case PACKET_START:
			if (device->role == LAN_IN_ROOM || device->role == LAN_PLAYING)
				heard_start(device, &in);
			break;
		case PACKET_FRAMES:
			if (!linked(device))
				heard_frames(device, &in);
			break;
		case PACKET_BLOCK:
			if (linked(device))
				heard_block(device, &in, sender, from);
			break;
		case PACKET_LEAVE:
			if (linked(device) && device->member_count > 0)
				device->members[0].heard = device->clock - LOST_FRAMES - 1u;    /* (gone at once: the next one hosts) */
			else
			{
				leave_room(device, LAN_OFF);
				set_problem(device, HOST_GONE);
			}
			break;
		default:
			break;
	}
}

/* ---- the device ---- */

lan_device *lan_open(const lan_config *config, const char *name, int players)
{
	static uint32_t opened;
	lan_device *device;

	if (!config || players < 1 || players > LAN_PLAYERS || config->max_devices < 0 || config->max_devices > LAN_DEVICES
		|| (config->mode != LAN_LOCKSTEP && config->mode != LAN_LINK)
		|| (config->mode == LAN_LOCKSTEP && (config->input_bytes < 1 || config->input_bytes > LAN_INPUT_BYTES
		|| config->delay < 1 || config->delay > LAN_DELAY_MOST)))
		return NULL;
	device = (lan_device *)calloc(1, sizeof(*device));
	if (!device)
		return NULL;
	device->config = *config;
	device->beacon_socket = LAN_NO_SOCKET;
	device->seek_socket = LAN_NO_SOCKET;
	device->socket = lan_udp_open(0, false);
	if (device->socket == LAN_NO_SOCKET)
	{
		free(device);
		return NULL;
	}
	/* (an id at random: the time, the clock, the port and the devices opened) */
	device->id = (uint32_t)time(NULL) * 2654435761u ^ (uint32_t)clock() * 40503u ^ (uint32_t)lan_udp_port(device->socket)
		<< 16 ^ ++opened * 2246822519u;
	if (!device->id)
		device->id = 1;
	device->random = device->id;
	clean_name(device->name, name ? name : "");
	device->players = players;
	device->role = LAN_OFF;
	device->waiting_for = NOBODY;
	return device;
}

void lan_close(lan_device *device)
{
	if (!device)
		return;
	lan_leave(device);
	lan_udp_close(device->beacon_socket);
	lan_udp_close(device->socket);
	free(device);
}

static void stop_searching(lan_device *device)
{
	lan_udp_close(device->beacon_socket);
	device->beacon_socket = LAN_NO_SOCKET;
	device->room_count = 0;
}

void lan_update(lan_device *device)
{
	uint8_t bytes[PACKET_MOST];
	lan_address from;
	size_t size;

	if (!device)
		return;
	device->clock++;
	while ((size = lan_udp_receive(device->socket, bytes, sizeof(bytes), &from)) > 0)
		take_datagram(device, bytes, size, from, AT_OWN);
	while ((size = lan_udp_receive(device->beacon_socket, bytes, sizeof(bytes), &from)) > 0)
		take_datagram(device, bytes, size, from, AT_BEACONS);
	while ((size = lan_udp_receive(device->seek_socket, bytes, sizeof(bytes), &from)) > 0)
		take_datagram(device, bytes, size, from, AT_SEEKS);

	switch (device->role)
	{
		case LAN_SEARCHING:
			if ((device->clock - device->seeking_since) % SEEK_FRAMES == 0)
				send_seek(device);
			for (int index = 0; index < device->room_count; index++)
				if (device->clock - device->rooms_heard[index] > ROOM_FORGOTTEN)
				{
					device->room_count--;
					device->rooms[index] = device->rooms[device->room_count];
					device->rooms_heard[index] = device->rooms_heard[device->room_count];
					index--;
				}
			break;
		case LAN_JOINING:
			if (device->clock - device->joining_since > JOIN_GIVEN_UP)
			{
				leave_room(device, LAN_OFF);
				set_problem(device, NO_ANSWER);
			}
			else if ((device->clock - device->joining_since) % JOIN_FRAMES == 0)
				send_join(device);
			break;
		case LAN_IN_ROOM:
		case LAN_PLAYING:
			if (!lan_hosting(device))
			{
				if (device->member_count > 0 && device->clock - device->members[0].heard > LOST_FRAMES)
				{
					/* (LAN_LINK: the next device of the list hosts the room on) */
					if (!linked(device) || !take_over(device))
					{
						leave_room(device, LAN_OFF);
						set_problem(device, HOST_GONE);
					}
					break;
				}
				if (device->role == LAN_PLAYING && !linked(device))
					send_input(device);
				else if (device->clock % JOIN_FRAMES == 0)
					send_join(device);    /* (in the room: heard by its host, its JOIN again, more often than a
					                         device unheard is waited for) */
				if (device->role == LAN_PLAYING && linked(device))
					link_frame(device);
				break;
			}
			/* the host */
			for (int index = 1; index < device->member_count; index++)
			{
				member *who = &device->members[index];

				if (who->gone || device->clock - who->heard <= LOST_FRAMES)
					continue;
				if (device->role == LAN_PLAYING && !linked(device))
				{
					who->gone = true;    /* (its slots none from the frames not yet whole) */
					device->list_changed = true;
				}
				else
				{
					heard_leave(device, index);
					index--;
				}
			}
			if (device->clock % BEACON_FRAMES == 0)
			{
				hear_seeks(device);    /* (again, had the port not opened) */
				send_beacon(device);
			}
			/* (not in a game in lockstep, whose room takes no one, nor while the game holds it) */
			if (device->clock % SWEEP_FRAMES == 0 && !device->sweep_held
				&& (device->role != LAN_PLAYING || linked(device)))
				sweep_beacon(device);
			if (device->list_changed || device->clock % LIST_FRAMES == 0)
			{
				send_list(device);
				device->list_changed = false;
			}
			if (device->role == LAN_PLAYING && linked(device))
			{
				link_frame(device);
				for (int index = 0; index < device->game_count; index++)
				{
					const link_member *who = &device->game[index];

					if (who->id != device->id && !who->gone && who->answered != device->serial)
						send_start(device, who->address);
				}
			}
			else if (device->role == LAN_PLAYING)
			{
				put_frames_together(device);
				for (int index = 1; index < device->member_count; index++)
				{
					member *who = &device->members[index];

					if (who->gone)
						continue;
					if (who->answered != device->serial)
						send_start(device, who->address);
					else
						send_frames(device, who);
				}
			}
			break;
		default:
			break;
	}
}

lan_role lan_role_of(const lan_device *device)
{
	return device ? device->role : LAN_OFF;
}

bool lan_hosting(const lan_device *device)
{
	return device && (device->role == LAN_IN_ROOM || device->role == LAN_PLAYING) && device->member_count > 0
		&& device->members[0].id == device->id;
}

const char *lan_problem(lan_device *device)
{
	const char *problem;

	if (!device)
		return NO_NETWORK;
	problem = device->problem;
	device->problem = NULL;
	return problem;
}

bool lan_host(lan_device *device)
{
	if (!device)
		return false;
	lan_leave(device);
	stop_searching(device);
	device->room = device->id;
	device->member_count = 1;
	memset(&device->members[0], 0, sizeof(device->members[0]));
	device->members[0].id = device->id;
	memcpy(device->members[0].name, device->name, sizeof(device->name));
	device->members[0].players = device->players;
	device->members[0].heard = device->clock;
	give_slots(device);
	device->list_version++;
	device->role = LAN_IN_ROOM;
	hear_seeks(device);
	send_beacon(device);
	return true;
}

void lan_search(lan_device *device)
{
	if (!device)
		return;
	lan_leave(device);
	if (device->beacon_socket == LAN_NO_SOCKET)
		device->beacon_socket = lan_udp_open(device->config.beacon_port, true);
	if (device->beacon_socket == LAN_NO_SOCKET)
	{
		set_problem(device, NO_NETWORK);
		return;
	}
	device->room_count = 0;
	device->role = LAN_SEARCHING;
	device->seeking_since = device->clock;
	send_seek(device);    /* (at once, and every second from now) */
}

int lan_rooms(const lan_device *device, lan_room rooms[LAN_ROOMS])
{
	if (!device || device->role != LAN_SEARCHING)
		return 0;
	memcpy(rooms, device->rooms, (size_t)device->room_count * sizeof(lan_room));
	return device->room_count;
}

bool lan_join(lan_device *device, const lan_room *room)
{
	if (!device || !room || !room->host.ip || !room->host.port)
		return false;
	lan_leave(device);
	stop_searching(device);
	device->room = room->room;
	device->host_address = room->host;
	device->member_count = 0;
	device->joining_since = device->clock;
	device->role = LAN_JOINING;
	send_join(device);
	return true;
}

void lan_leave(lan_device *device)
{
	if (!device)
		return;
	if (lan_hosting(device))
	{
		for (int copy = 0; copy < LEAVE_COPIES; copy++)
			for (int index = 1; index < device->member_count; index++)
				send_leave(device, device->members[index].address);
	}
	else if (device->role == LAN_JOINING || device->role == LAN_IN_ROOM || device->role == LAN_PLAYING)
		for (int copy = 0; copy < LEAVE_COPIES; copy++)
			send_leave(device, device->host_address);
	if (linked(device) && device->role == LAN_PLAYING)
	{
		/* (the game's devices hear it too: gone at once) */
		for (int copy = 0; copy < LEAVE_COPIES; copy++)
			for (int index = 0; index < device->game_count; index++)
				if (device->game[index].id != device->id && !device->game[index].gone && device->game[index].address.ip
					&& (device->member_count == 0 || device->game[index].id != device->members[0].id))
					send_leave(device, device->game[index].address);
	}
	if (device->role == LAN_SEARCHING)
		stop_searching(device);
	leave_room(device, LAN_OFF);
}

int lan_members(const lan_device *device, lan_member members[LAN_DEVICES])
{
	if (!device || (device->role != LAN_IN_ROOM && device->role != LAN_PLAYING && device->role != LAN_JOINING))
		return 0;
	for (int index = 0; index < device->member_count; index++)
	{
		const member *who = &device->members[index];

		memset(&members[index], 0, sizeof(members[index]));
		members[index].id = who->id;
		memcpy(members[index].name, who->name, sizeof(who->name));
		members[index].players = who->players;
		members[index].first_slot = who->first_slot;
		members[index].own = who->id == device->id;
		members[index].gone = who->gone;
		members[index].status = members[index].own ? device->status : who->status;
		if (members[index].own)
			members[index].heard = true;
		else if (lan_hosting(device) || index == 0)
			members[index].heard = device->clock - who->heard <= SILENT_FRAMES;
		else
			members[index].heard = !who->silent;
	}
	return device->member_count;
}

bool lan_start(lan_device *device, const void *settings, size_t size)
{
	if (!lan_hosting(device) || size > LAN_SETTINGS_MOST
		|| (device->role != LAN_IN_ROOM && !(linked(device) && device->role == LAN_PLAYING)))
		return false;
	device->serial++;
	if (size)
		memcpy(device->settings, settings, size);
	device->settings_size = size;
	if (linked(device))
	{
		/* (another game while one goes on: the room's devices now) */
		uint32_t ids[LAN_DEVICES];
		int players[LAN_DEVICES];

		for (int index = 0; index < device->member_count; index++)
		{
			ids[index] = device->members[index].id;
			players[index] = device->members[index].players;
		}
		start_link(device, ids, players, device->member_count);
	}
	else
		start_game(device);
	for (int index = 1; index < device->member_count; index++)
		send_start(device, device->members[index].address);
	send_beacon(device);    /* (its room playing now: JOIN shows it so) */
	return true;
}

bool lan_game_started(lan_device *device, void *settings, size_t *size, int *first_slot)
{
	if (!device || !device->started_new || device->role != LAN_PLAYING)
		return false;
	device->started_new = false;
	if (settings)
		memcpy(settings, device->settings, device->settings_size);
	if (size)
		*size = device->settings_size;
	if (first_slot)
		*first_slot = device->first_slot;
	return true;
}

void lan_give_inputs(lan_device *device, const uint8_t *inputs)
{
	frame_entry *entry;
	const int bytes = device ? input_width(device) : 0;

	if (!device || device->role != LAN_PLAYING || device->given > device->next + (uint32_t)device->config.delay)
		return;
	entry = entry_for(device->local, device->given);
	for (int player = 0; player < device->players; player++)
		memcpy(entry->inputs + player * SLOT_BYTES, inputs + player * bytes, (size_t)bytes);
	if (lan_hosting(device))
		take_slots(device, &device->members[0], device->given, entry->inputs);
	device->given++;
	if (lan_hosting(device))
		put_frames_together(device);
	else
		send_input(device);    /* (at once, not at the next update: a frame sooner at the host) */
}

bool lan_next_frame(lan_device *device, uint8_t *inputs)
{
	const frame_entry *entry;
	const int bytes = device ? input_width(device) : 0;

	if (!device || device->role != LAN_PLAYING || device->next >= device->whole)
		return false;
	entry = entry_of(device->frames, device->next);
	if (!entry || !entry->whole)
		return false;
	for (int slot = 0; slot < LAN_PLAYERS; slot++)
		memcpy(inputs + slot * bytes, entry->inputs + slot * SLOT_BYTES, (size_t)bytes);
	device->next++;
	return true;
}

void lan_frame_hash(lan_device *device, uint32_t frame, uint64_t hash)
{
	if (!device || device->role != LAN_PLAYING)
		return;
	if (lan_hosting(device))
	{
		const int at = (int)(frame % RING);

		device->hashes[at] = hash;
		device->hash_frames[at] = frame;
		device->hash_set[at] = true;
		for (int index = 1; index < device->member_count; index++)
			compare_hash(device, &device->members[index]);
		return;
	}
	device->hash_frame = frame;
	device->hash = hash;
	device->hash_known = true;
}

uint32_t lan_frame_number(const lan_device *device)
{
	return device ? device->next : 0;
}

bool lan_waiting(const lan_device *device, char name[LAN_NAME_LENGTH + 1])
{
	if (device && device->role == LAN_PLAYING && linked(device))
	{
		/* (LAN_LINK: the first device of the game unheard for a third of a second, not yet gone) */
		for (int index = 0; index < device->game_count; index++)
		{
			const link_member *who = &device->game[index];

			if (who->id != device->id && !who->gone && device->clock - who->heard > SILENT_FRAMES)
			{
				if (name)
					memcpy(name, who->name, LAN_NAME_LENGTH + 1);
				return true;
			}
		}
		return false;
	}
	if (!device || device->role != LAN_PLAYING || device->next < device->whole || device->waiting_for == NOBODY
		|| device->waiting_for >= device->member_count)
		return false;
	if (name)
		memcpy(name, device->members[device->waiting_for].name, LAN_NAME_LENGTH + 1);
	return true;
}

uint16_t lan_port(const lan_device *device)
{
	return device ? lan_udp_port(device->socket) : 0;
}

lan_room lan_room_at(lan_address host)
{
	lan_room room;

	memset(&room, 0, sizeof(room));
	room.host = host;
	return room;
}

void lan_hold_sweep(lan_device *device, bool held)
{
	if (device)
		device->sweep_held = held;
}

/* ---- LAN_LINK's ---- */

void lan_set_status(lan_device *device, uint8_t status)
{
	if (device)
		device->status = status;
}

int lan_game_members(const lan_device *device, lan_member members[LAN_DEVICES])
{
	if (!device || !linked(device) || device->role != LAN_PLAYING)
		return 0;
	for (int index = 0; index < device->game_count; index++)
	{
		const link_member *who = &device->game[index];

		memset(&members[index], 0, sizeof(members[index]));
		members[index].id = who->id;
		memcpy(members[index].name, who->name, sizeof(who->name));
		members[index].players = who->players;
		members[index].first_slot = who->first_slot;
		members[index].own = who->id == device->id;
		members[index].gone = who->gone;
		members[index].heard = members[index].own || device->clock - who->heard <= SILENT_FRAMES;
		members[index].status = members[index].own ? device->status : who->status;
	}
	return device->game_count;
}

void lan_publish(lan_device *device, const void *block, size_t size)
{
	link_member *own;

	if (!device || !linked(device) || device->role != LAN_PLAYING || size > LAN_BLOCK_MOST
		|| !(own = link_member_of(device, device->id)))
		return;
	if (size)
		memcpy(own->block, block, size);
	own->block_size = size;
	own->status = device->status;
	for (int index = 0; index < device->game_count; index++)
		if (device->game[index].id != device->id && !device->game[index].gone)
			send_block(device, &device->game[index], own->block, size);
}

size_t lan_block(const lan_device *device, int index, void *block)
{
	if (!device || !linked(device) || device->role != LAN_PLAYING || index < 0 || index >= device->game_count)
		return 0;
	memcpy(block, device->game[index].block, device->game[index].block_size);
	return device->game[index].block_size;
}
