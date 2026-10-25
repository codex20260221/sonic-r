	/**
 * net_transport.c — Cross-platform UDP network transport
 *
 * Replaces DirectPlay with UDP sockets.
 * Single UDP socket, non-blocking I/O, up to 4 players.
 * Supports BSD sockets (POSIX) and winsock2 (Windows).
 */

#include "net_transport.h"
#include "endian_util.h"
#include "matchmaker.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>   /* abort() — see assert_may_send */
#include <errno.h>
#include <sys/types.h>   /* ssize_t (MinGW provides this on Windows too) */

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

typedef SOCKET net_socket_t;
#define NET_INVALID_SOCKET INVALID_SOCKET
#define NET_CLOSESOCKET    closesocket
#define NET_LAST_ERROR()   WSAGetLastError()
#define NET_WOULD_BLOCK(e) ((e) == WSAEWOULDBLOCK)
#elif defined(SONICR_DC)
#include <kos/fs.h>
#include <kos/mutex.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#ifndef O_NONBLOCK
#define O_NONBLOCK 0x0400
#endif

typedef int net_socket_t;
#define NET_INVALID_SOCKET (-1)
#define NET_CLOSESOCKET    close
#define NET_LAST_ERROR()   errno
#define NET_WOULD_BLOCK(e) ((e) == EAGAIN || (e) == EWOULDBLOCK)
#else
#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>

typedef int net_socket_t;
#define NET_INVALID_SOCKET (-1)
#define NET_CLOSESOCKET    close
#define NET_LAST_ERROR()   errno
#define NET_WOULD_BLOCK(e) ((e) == EAGAIN || (e) == EWOULDBLOCK)
#endif

/* =====================================================================
 * Internal state
 * ===================================================================== */

static net_socket_t s_socket = NET_INVALID_SOCKET;     /* UDP socket */
static int s_active = 0;                               /* 1 if transport is up */
static int s_isHost = 0;                               /* 1 if we are the host */

/* Per-player address table (host uses this to know where to send) */
static struct sockaddr_in s_playerAddr[NET_MAX_PLAYERS];
static int                s_playerValid[NET_MAX_PLAYERS]; /* pending/established below */
static int                s_playerCount = 0;
/* 0 = free, 1 = pending JOIN, 2 = established endpoint. */
#define NET_PEER_PENDING 1
#define NET_PEER_ESTABLISHED 2
static uint64_t s_playerGeneration[NET_MAX_PLAYERS];
static uint64_t s_generationCounter, s_transportGeneration;
static int s_joinable;

/* The receive thread reserves endpoints; the main thread commits/frees them
 * and sends packets. Keep address, admission and generation changes together. */
#ifdef _WIN32
static SRWLOCK s_peerMutex = SRWLOCK_INIT;
#define PEER_LOCK() AcquireSRWLockExclusive(&s_peerMutex)
#define PEER_UNLOCK() ReleaseSRWLockExclusive(&s_peerMutex)
#elif defined(SONICR_DC)
static mutex_t s_peerMutex = MUTEX_INITIALIZER;
#define PEER_LOCK() mutex_lock(&s_peerMutex)
#define PEER_UNLOCK() mutex_unlock(&s_peerMutex)
#else
static pthread_mutex_t s_peerMutex = PTHREAD_MUTEX_INITIALIZER;
#define PEER_LOCK() pthread_mutex_lock(&s_peerMutex)
#define PEER_UNLOCK() pthread_mutex_unlock(&s_peerMutex)
#endif

/* Host address (client stores this) */
static struct sockaddr_in s_hostAddr;

/* Local player slot */
static int s_localSlot = 0;

/* Discovery socket (separate from game socket) */
static net_socket_t s_discoverSocket = NET_INVALID_SOCKET;

#ifdef _WIN32
static int s_winsockInitialized = 0;
#endif

/* =====================================================================
 * Helpers
 * ===================================================================== */

static int socket_is_valid(net_socket_t s)
{
    return s != NET_INVALID_SOCKET;
}

static void socket_close_if_valid(net_socket_t *s)
{
    if (socket_is_valid(*s)) {
        NET_CLOSESOCKET(*s);
        *s = NET_INVALID_SOCKET;
    }
}

static int set_nonblocking(net_socket_t fd)
{
#ifdef _WIN32
    u_long mode = 1;
    return ioctlsocket(fd, FIONBIO, &mode);
#elif defined(SONICR_DC)
    return fs_fcntl(fd, F_SETFL, O_NONBLOCK);
#else
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#endif
}

static int addrs_equal(const struct sockaddr_in *a, const struct sockaddr_in *b)
{
    return a->sin_addr.s_addr == b->sin_addr.s_addr &&
           a->sin_port == b->sin_port;
}

/* Find which player slot sent a packet, or -1 if unknown */
static int find_slot(const struct sockaddr_in *addr)
{
    for (int i = 0; i < NET_MAX_PLAYERS; i++) {
        if (s_playerValid[i] && addrs_equal(&s_playerAddr[i], addr))
            return i;
    }
    return -1;
}

static uint64_t next_generation(void)
{
    if (++s_generationCounter == 0) ++s_generationCounter;
    return s_generationCounter;
}

static void net_close_locked(void)
{
    socket_close_if_valid(&s_socket);
    socket_close_if_valid(&s_discoverSocket);
    memset(s_playerValid, 0, sizeof(s_playerValid));
    memset(s_playerGeneration, 0, sizeof(s_playerGeneration));
    s_transportGeneration = next_generation();
    s_playerCount = s_active = s_isHost = s_localSlot = s_joinable = 0;
}

/* =====================================================================
 * Public API
 * ===================================================================== */

int net_transport_init(void)
{
    PEER_LOCK();
#ifdef _WIN32
    if (!s_winsockInitialized) {
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
            fprintf(stderr, "net: WSAStartup failed\n");
            PEER_UNLOCK();
            return -1;
        }
        s_winsockInitialized = 1;
    }
#endif

    net_close_locked();
    memset(s_playerAddr, 0, sizeof(s_playerAddr));
    PEER_UNLOCK();
    return 0;
}

static int net_host_start_locked(int port)
{
    if (s_active && s_isHost) return 0;
    if (s_active) net_close_locked();

    s_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (!socket_is_valid(s_socket)) {
        fprintf(stderr, "net: socket() failed (err %d)\n", NET_LAST_ERROR());
        return -1;
    }

    /* Allow address reuse for quick restart */
    int reuse = 1;
    setsockopt(s_socket, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, sizeof(reuse));

    /* Enable broadcast for discovery replies */
    int bcast = 1;
    setsockopt(s_socket, SOL_SOCKET, SO_BROADCAST, (const char *)&bcast, sizeof(bcast));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons((unsigned short)port);

    if (bind(s_socket, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "net: bind(%d) failed (err %d)\n", port, NET_LAST_ERROR());
        socket_close_if_valid(&s_socket);
        return -1;
    }

    set_nonblocking(s_socket);

    s_isHost = 1;
    s_active = 1;
    s_localSlot = 0;   /* host is always slot 0 */

    /* Register self in player table */
    memset(&s_playerAddr[0], 0, sizeof(s_playerAddr[0]));
    s_playerValid[0] = NET_PEER_ESTABLISHED;
    s_playerGeneration[0] = s_transportGeneration = next_generation();
    s_joinable = 1;
    s_playerCount = 1;

    fprintf(stderr, "net: hosting on port %d\n", port);
    return 0;
}

int net_host_start(int port)
{
    PEER_LOCK();
    int result = net_host_start_locked(port);
    PEER_UNLOCK();
    return result;
}

static int net_client_connect_locked(const char *host_ip, int port)
{
    if (s_active) net_close_locked();

    s_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (!socket_is_valid(s_socket)) {
        fprintf(stderr, "net: socket() failed (err %d)\n", NET_LAST_ERROR());
        return -1;
    }

    /* Bind to any port so we can receive replies */
    struct sockaddr_in local;
    memset(&local, 0, sizeof(local));
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = INADDR_ANY;
    local.sin_port = 0;  /* OS assigns ephemeral port */
    if (bind(s_socket, (struct sockaddr *)&local, sizeof(local)) < 0) {
        fprintf(stderr, "net: client bind failed (err %d)\n", NET_LAST_ERROR());
        socket_close_if_valid(&s_socket);
        return -1;
    }

    set_nonblocking(s_socket);

    /* Store host address */
    memset(&s_hostAddr, 0, sizeof(s_hostAddr));
    s_hostAddr.sin_family = AF_INET;
    s_hostAddr.sin_port = htons((unsigned short)port);
    if (inet_pton(AF_INET, host_ip, &s_hostAddr.sin_addr) != 1) {
        fprintf(stderr, "net: invalid host IP '%s'\n", host_ip);
        socket_close_if_valid(&s_socket);
        return -1;
    }

    s_isHost = 0;
    s_active = 1;
    s_localSlot = -1;  /* assigned by host later */
    s_playerAddr[0] = s_hostAddr;
    s_playerValid[0] = NET_PEER_ESTABLISHED;
    s_playerGeneration[0] = s_transportGeneration = next_generation();

    fprintf(stderr, "net: connecting to %s:%d\n", host_ip, port);
    return 0;
}

int net_client_connect(const char *host_ip, int port)
{
    PEER_LOCK();
    int result = net_client_connect_locked(host_ip, port);
    PEER_UNLOCK();
    return result;
}

/* A client has no business putting anything on the wire before the host has
 * assigned it a slot; JOIN_REQ is the one legitimate exception, since it is
 * what asks for the slot. Keep this local programming invariant in addition
 * to the host's defensive receive checks. net_broadcast delegates here for
 * clients, so this covers both send paths. */
static void assert_may_send(const void *data, int len)
{
    if (s_localSlot >= 0) return;
    if (len >= 4 && rl32u(data) == (uint32_t)NET_MSG_JOIN_REQ) return;

    fprintf(stderr,
            "FATAL: client sent header 0x%08lx (%d bytes) before SLOT_ASSIGN. "
            "Only NET_MSG_JOIN_REQ may precede slot assignment.\n",
            /* uint32_t is unsigned int under clang and long unsigned int under
             * SH4 GCC, so neither %x nor %lx suits both. Widen to the type the
             * conversion names and every toolchain agrees. */
            (unsigned long)((len >= 4) ? rl32u(data) : 0u), len);
    abort();
}

static int net_send_to_host_locked(const void *data, int len)
{
    if (!s_active || !socket_is_valid(s_socket)) return -1;

    if (s_isHost) {
        /* Host sending to self — no-op, data is already local */
        return len;
    }

    assert_may_send(data, len);

    ssize_t n = sendto(s_socket, data, (size_t)len, 0,
                        (struct sockaddr *)&s_hostAddr, sizeof(s_hostAddr));
    return (int)n;
}

int net_send_to_host(const void *data, int len)
{
    PEER_LOCK();
    int result = net_send_to_host_locked(data, len);
    PEER_UNLOCK();
    return result;
}

static int net_broadcast_locked(const void *data, int len)
{
    if (!s_active || !socket_is_valid(s_socket)) return -1;

    if (s_isHost) {
        /* Host: send to each connected client (skip slot 0 = self) */
        for (int i = 1; i < NET_MAX_PLAYERS; i++) {
            if (s_playerValid[i] != NET_PEER_ESTABLISHED) continue;
            sendto(s_socket, data, (size_t)len, 0,
                   (struct sockaddr *)&s_playerAddr[i],
                   sizeof(s_playerAddr[i]));
        }
        return 0;
    } else {
        /* Client: send to host (host relays) */
        return net_send_to_host_locked(data, len);
    }
}

int net_broadcast(const void *data, int len)
{
    PEER_LOCK();
    int result = net_broadcast_locked(data, len);
    PEER_UNLOCK();
    return result;
}

static int net_send_to_locked(int player_slot, const void *data, int len)
{
    if (!s_active || !socket_is_valid(s_socket)) return -1;
    if (player_slot < 0 || player_slot >= NET_MAX_PLAYERS) return -1;
    if (s_playerValid[player_slot] != NET_PEER_ESTABLISHED) return -1;

    if (player_slot == s_localSlot) return len; /* sending to self */

    ssize_t n = sendto(s_socket, data, (size_t)len, 0,
                        (struct sockaddr *)&s_playerAddr[player_slot],
                        sizeof(s_playerAddr[player_slot]));
    return (int)n;
}

int net_send_to(int player_slot, const void *data, int len)
{
    PEER_LOCK();
    int result = net_send_to_locked(player_slot, data, len);
    PEER_UNLOCK();
    return result;
}

int net_join_request_valid_length(int len)
{
    enum { LEGACY = 4 + MM_MAX_USERNAME, EXTENDED = LEGACY + 16 + 1 };
    return len == LEGACY || len == EXTENDED || len == ((EXTENDED + 3) & ~3);
}

int net_recv_packet(void *buf, int maxlen, int *from_slot, uint64_t *generation)
{
    if (from_slot) *from_slot = -1;
    if (generation) *generation = 0;
    if (maxlen < 4) return 0;
    PEER_LOCK();
    net_socket_t socket = s_socket;
    uint64_t session = s_transportGeneration;
    int active = s_active;
    PEER_UNLOCK();
    if (!active || !socket_is_valid(socket)) return 0;

    /* One spare byte detects truncation portably, without publishing a
     * partially received datagram as a valid small control packet. */
    char packet[NET_MAX_PACKET + 1];
    struct sockaddr_in sender;
    socklen_t sender_len = sizeof(sender);
    ssize_t n = recvfrom(socket, packet, sizeof(packet), 0,
                          (struct sockaddr *)&sender, &sender_len);
    if (n < 0) {
        int error = NET_LAST_ERROR();
#ifdef _WIN32
        if (error == WSAEMSGSIZE) return 0;
#else
        if (error == EMSGSIZE) return 0;
#endif
        return NET_WOULD_BLOCK(error) ? 0 : -1;
    }
    if (n < 4 || n > NET_MAX_PACKET || n > maxlen) return 0;
    uint32_t header = rl32u(packet);
    PEER_LOCK();
    if (!s_active || s_socket != socket || s_transportGeneration != session) {
        PEER_UNLOCK();
        return 0;
    }
    if (s_isHost && header == NET_DISCOVER_MAGIC) {
        if (n == 4) {
            char _rbuf[4];
            wl32(_rbuf, NET_DISCOVER_REPLY);
            sendto(socket, _rbuf, 4, 0,
                   (struct sockaddr *)&sender, sender_len);
        }
        PEER_UNLOCK();
        return 0;
    }
    int slot = find_slot(&sender);
    if (s_isHost && slot < 0) {
        if (!s_joinable || header != NET_MSG_JOIN_REQ || !net_join_request_valid_length((int)n)) {
            PEER_UNLOCK();
            return 0;
        }
        for (int i = 1; i < NET_MAX_PLAYERS; i++) {
            if (!s_playerValid[i]) {
                s_playerAddr[i] = sender;
                s_playerGeneration[i] = next_generation();
                s_playerValid[i] = NET_PEER_PENDING;
                s_playerCount++;
                slot = i;
                break;
            }
        }
    }
    if (slot < 0 || (s_isHost && s_playerValid[slot] != NET_PEER_ESTABLISHED &&
                    (header != NET_MSG_JOIN_REQ || !net_join_request_valid_length((int)n)))) {
        PEER_UNLOCK();
        return 0;
    }
    if (from_slot) *from_slot = slot;
    if (generation) *generation = s_playerGeneration[slot];
    PEER_UNLOCK();
    memcpy(buf, packet, (size_t)n);
    return (int)n;
}

int net_recv(void *buf, int maxlen, int *from_slot)
{
    return net_recv_packet(buf, maxlen, from_slot, NULL);
}

int net_peer_is_current(int slot, uint64_t generation)
{
    PEER_LOCK();
    int valid = s_active && slot >= 0 && slot < NET_MAX_PLAYERS &&
                s_playerValid[slot] && generation != 0 && s_playerGeneration[slot] == generation;
    PEER_UNLOCK();
    return valid;
}

void net_set_joinable(int joinable)
{
    PEER_LOCK();
    s_joinable = !!joinable;
    PEER_UNLOCK();
}

int net_register_client(int slot)
{
    PEER_LOCK();
    int result = -1;
    if (s_isHost && slot >= 0 && slot < NET_MAX_PLAYERS && s_playerValid[slot]) {
        s_playerValid[slot] = NET_PEER_ESTABLISHED;
        result = slot;
    }
    PEER_UNLOCK();
    return result;
}

int net_discover_send(int port)
{
    /* Create a temporary broadcast socket */
    if (!socket_is_valid(s_discoverSocket)) {
        s_discoverSocket = socket(AF_INET, SOCK_DGRAM, 0);
        if (!socket_is_valid(s_discoverSocket)) return -1;

        int bcast = 1;
        setsockopt(s_discoverSocket, SOL_SOCKET, SO_BROADCAST, (const char *)&bcast, sizeof(bcast));
        set_nonblocking(s_discoverSocket);

        /* Bind to any port */
        struct sockaddr_in local;
        memset(&local, 0, sizeof(local));
        local.sin_family = AF_INET;
        local.sin_addr.s_addr = INADDR_ANY;
        local.sin_port = 0;
        bind(s_discoverSocket, (struct sockaddr *)&local, sizeof(local));
    }

    struct sockaddr_in bcast_addr;
    memset(&bcast_addr, 0, sizeof(bcast_addr));
    bcast_addr.sin_family = AF_INET;
    bcast_addr.sin_addr.s_addr = INADDR_BROADCAST;
    bcast_addr.sin_port = htons((unsigned short)port);

    char _mbuf[4];
    wl32(_mbuf, NET_DISCOVER_MAGIC);
    ssize_t n = sendto(s_discoverSocket, _mbuf, 4, 0,
                        (struct sockaddr *)&bcast_addr, sizeof(bcast_addr));
    return (n > 0) ? 0 : -1;
}

int net_discover_check(char *host_ip, int host_ip_len)
{
    if (!socket_is_valid(s_discoverSocket)) return 0;

    struct sockaddr_in sender;
    socklen_t sender_len = sizeof(sender);

    char _dbuf[4];
    ssize_t n = recvfrom(s_discoverSocket, _dbuf, 4, 0,
                          (struct sockaddr *)&sender, &sender_len);
    if (n < 4) return 0;

    if (rl32u(_dbuf) == NET_DISCOVER_REPLY) {
        const char *ip = inet_ntoa(sender.sin_addr);
        if ((int)strlen(ip) < host_ip_len) {
            strcpy(host_ip, ip);
            /* Done with discovery socket */
            socket_close_if_valid(&s_discoverSocket);
            return 1;
        }
    }
    return 0;
}

int net_is_active(void)
{
    PEER_LOCK();
    int result = s_active;
    PEER_UNLOCK();
    return result;
}

int net_is_host(void)
{
    PEER_LOCK();
    int result = s_isHost;
    PEER_UNLOCK();
    return result;
}

int net_local_slot(void)
{
    PEER_LOCK();
    int result = s_localSlot;
    PEER_UNLOCK();
    return result;
}

void net_set_local_slot(int slot)
{
    PEER_LOCK();
    s_localSlot = slot;
    PEER_UNLOCK();
}

void net_close(void)
{
    PEER_LOCK();
    net_close_locked();
    PEER_UNLOCK();
    fprintf(stderr, "net: closed\n");
}

void net_unregister_slot(int slot)
{
    if (slot <= 0 || slot >= NET_MAX_PLAYERS) return;
    PEER_LOCK();
    if (!s_playerValid[slot]) { PEER_UNLOCK(); return; }
    fprintf(stderr, "net: freed slot %d (%s:%d)\n",
            slot, inet_ntoa(s_playerAddr[slot].sin_addr),
            ntohs(s_playerAddr[slot].sin_port));
    memset(&s_playerAddr[slot], 0, sizeof(s_playerAddr[slot]));
    s_playerValid[slot] = 0;
    s_playerGeneration[slot] = 0;
    if (s_playerCount > 0) s_playerCount--;
    PEER_UNLOCK();
}

int net_slot_is_connected(int slot)
{
    if (slot < 0 || slot >= NET_MAX_PLAYERS) return 0;
    PEER_LOCK();
    int result = s_playerValid[slot] == NET_PEER_ESTABLISHED;
    PEER_UNLOCK();
    return result;
}

#ifdef SONICR_DC

int net_probe_ping(const char *ip, int port, int timeout_ms)
{
    (void)ip; (void)port; (void)timeout_ms;
    return -1;
}

#elif defined(_WIN32)
#include <iphlpapi.h>
#include <icmpapi.h>

int net_probe_ping(const char *ip, int port, int timeout_ms)
{
    (void)port;
    if (!ip || !ip[0] || timeout_ms <= 0) return -1;

    IPAddr addr = inet_addr(ip);
    if (addr == INADDR_NONE) return -1;

    HANDLE icmp = IcmpCreateFile();
    if (icmp == INVALID_HANDLE_VALUE) return -1;

    char  sendData[8];
    memcpy(sendData, "sonicr!!", 8);
    char  replyBuf[sizeof(ICMP_ECHO_REPLY) + sizeof(sendData) + 16];
    DWORD nReplies = IcmpSendEcho(icmp, addr,
                                  sendData, sizeof(sendData),
                                  NULL,
                                  replyBuf, sizeof(replyBuf),
                                  (DWORD)timeout_ms);
    IcmpCloseHandle(icmp);
    if (nReplies == 0) return -1;

    PICMP_ECHO_REPLY r = (PICMP_ECHO_REPLY)replyBuf;
    if (r->Status != IP_SUCCESS) return -1;
    return (int)r->RoundTripTime;
}

#else /* POSIX — unprivileged ICMP datagram socket */

static unsigned int probe_now_ms(void)
{
    extern unsigned int timeGetTime(void);
    return timeGetTime();
}

static unsigned short icmp_checksum(const void *data, int len)
{
    const unsigned short *p = (const unsigned short *)data;
    unsigned int sum = 0;
    while (len > 1) { sum += *p++; len -= 2; }
    if (len) sum += *(const unsigned char *)p;
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (unsigned short)~sum;
}

int net_probe_ping(const char *ip, int port, int timeout_ms)
{
    (void)port;
    if (!ip || !ip[0] || timeout_ms <= 0) return -1;

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_ICMP);
    if (sock < 0) return -1;

    struct timeval tv;
    tv.tv_sec  = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    struct sockaddr_in dst;
    memset(&dst, 0, sizeof(dst));
    dst.sin_family      = AF_INET;
    dst.sin_addr.s_addr = inet_addr(ip);
    if (dst.sin_addr.s_addr == INADDR_NONE) {
        close(sock);
        return -1;
    }

    static unsigned short s_icmpSeq = 0;
    s_icmpSeq++;

    /* ICMP echo request: 8-byte header + 8-byte payload. The kernel
     * rewrites the id field for IPPROTO_ICMP datagram sockets, so we
     * leave it 0 and match replies by sequence + payload only. */
    struct {
        unsigned char  type;
        unsigned char  code;
        unsigned short checksum;
        unsigned short id;
        unsigned short seq;
        char           payload[8];
    } pkt;

    pkt.type     = 8;     /* ICMP_ECHO */
    pkt.code     = 0;
    pkt.checksum = 0;
    pkt.id       = 0;
    pkt.seq      = htons(s_icmpSeq);
    memcpy(pkt.payload, "sonicr!!", 8);
    pkt.checksum = icmp_checksum(&pkt, sizeof(pkt));

    unsigned int start = probe_now_ms();
    if (sendto(sock, &pkt, sizeof(pkt), 0,
               (struct sockaddr *)&dst, sizeof(dst)) < 0) {
        close(sock);
        return -1;
    }

    char rbuf[256];
    struct sockaddr_in src;
    socklen_t srclen = sizeof(src);
    ssize_t n = recvfrom(sock, rbuf, sizeof(rbuf), 0,
                         (struct sockaddr *)&src, &srclen);
    close(sock);
    if (n < (ssize_t)sizeof(pkt)) return -1;

    return (int)(probe_now_ms() - start);
}

#endif
