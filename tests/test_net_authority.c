/* Real transport plus the existing production-dispatch fixture. All socket
 * binds and datagrams in this harness are restricted to loopback. */
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif
#undef INADDR_ANY
#define INADDR_ANY htonl(INADDR_LOOPBACK)
#define net_recv authority_socket_recv
#define net_recv_packet authority_socket_recv_packet
#include SONICR_TEST_TRANSPORT_SOURCE
#undef net_recv
#undef net_recv_packet
int net_recv(void *buf, int maxlen, int *from_slot);
int net_recv_packet(void *buf, int maxlen, int *from_slot, uint64_t *generation);
static void fixture_transport_reset(int is_host);
#define SONICR_TEST_REAL_TRANSPORT
#define main transactional_receive_main
#include "test_net_receive.c"
#undef main

static int real_mode;
void platform_pump_events(void) { }

static void fixture_transport_reset(int is_host)
{
    if (!real_mode) {
        CHECK(net_transport_init() == 0);
        s_isHost = is_host;
        s_active = 1;
        s_localSlot = is_host ? 0 : 3;
        s_playerCount = is_host ? NET_MAX_PLAYERS : 1;
        for (int i = 0; i < NET_MAX_PLAYERS; ++i) {
            s_playerValid[i] = is_host || i == 0;
#ifdef SONICR_TEST_GENERATIONS
            s_playerGeneration[i] = (uint64_t)i + 1;
#endif
            if (is_host) CHECK(net_register_client(i) == i);
        }
    }
    s_haveSlotAssign = !is_host;
    g_currentPlayerIdx = g_localPlayerIndex;
    memset(g_netPlayerDecorations, 0, NET_MAX_PLAYERS * NET_DECO_STRIDE);
    memset(s_netSlotNames, 0, sizeof(s_netSlotNames));
    memset(s_netSlotPlatform, 0, sizeof(s_netSlotPlatform));
    memset(s_netSlotRegion, 0, sizeof(s_netSlotRegion));
    memset(g_netLobbyConfigBuf, 0, sizeof(int) * 12);
    memset(g_netGameInfoDest, 0, sizeof(int) * 12);
    memset(g_netPlayerAlive, 0, sizeof(int) * NET_MAX_PLAYERS);
    g_netReceivedLobbyData = g_netReadyFlag = 0;
    s_startAckMask = s_levelReadyMask = 1;
    s_levelGoReceived = 0;
    g_menuPlayer.charId = (short)g_localPlayerIndex;
    g_numViewports = NET_MAX_PLAYERS;
    for (int i = 0; i < NET_MAX_PLAYERS; ++i) {
        char *entry = g_netPlayerDecorations + i * NET_DECO_STRIDE;
        *(int *)(entry + NET_DECO_DPID) = i;
        *(unsigned short *)(entry + NET_DECO_SLOT) = (unsigned short)i;
    }
}

int net_recv(void *buf, int maxlen, int *from_slot)
{
    if (packet_ready) {
        CHECK(incoming_len <= maxlen);
        memcpy(buf, incoming, (size_t)incoming_len);
        *from_slot = incoming_slot;
        packet_ready = 0;
        return incoming_len;
    }
    return real_mode ? authority_socket_recv(buf, maxlen, from_slot) : 0;
}

int net_recv_packet(void *buf, int maxlen, int *from_slot, uint64_t *generation)
{
#ifdef SONICR_TEST_GENERATIONS
    if (!packet_ready && real_mode)
        return authority_socket_recv_packet(buf, maxlen, from_slot, generation);
    int len = net_recv(buf, maxlen, from_slot);
    *generation = len > 0 && *from_slot >= 0 && *from_slot < NET_MAX_PLAYERS ? s_playerGeneration[*from_slot] : 0;
    return len;
#else
    *generation = 1;
    return net_recv(buf, maxlen, from_slot);
#endif
}

typedef struct {
    ReceiveState receive;
    char decorations[NET_MAX_PLAYERS * NET_DECO_STRIDE];
    char names[NET_MAX_PLAYERS][MM_MAX_USERNAME];
    char platforms[NET_MAX_PLAYERS][NET_PLATFORM_LEN];
    uint8_t regions[NET_MAX_PLAYERS];
    int config[12], game[12], alive[NET_MAX_PLAYERS];
    int players, local, current, assigned, ready, lobby, ack, level, go, active;
    int transport_slots[NET_MAX_PLAYERS], transport_count, local_slot;
    struct sockaddr_in addresses[NET_MAX_PLAYERS];
    int track, mode, weather, time, disconnect, fade, speed, finished;
#ifdef SONICR_TEST_GENERATIONS
    uint64_t generations[NET_MAX_PLAYERS];
#endif
} AuthorityState;

static AuthorityState capture_authority(void)
{
    AuthorityState state;
    memset(&state, 0, sizeof(state));
    state.receive = capture();
    memcpy(state.decorations, g_netPlayerDecorations, sizeof(state.decorations));
    memcpy(state.names, s_netSlotNames, sizeof(state.names));
    memcpy(state.platforms, s_netSlotPlatform, sizeof(state.platforms));
    memcpy(state.regions, s_netSlotRegion, sizeof(state.regions));
    memcpy(state.config, g_netLobbyConfigBuf, sizeof(state.config));
    memcpy(state.game, g_netGameInfoDest, sizeof(state.game));
    memcpy(state.alive, g_netPlayerAlive, sizeof(state.alive));
    state.players = g_netPlayerCount; state.local = g_localPlayerIndex;
    state.current = g_currentPlayerIdx; state.assigned = s_haveSlotAssign;
    state.ready = g_netReadyFlag; state.lobby = g_netReceivedLobbyData;
    state.ack = s_startAckMask; state.level = s_levelReadyMask; state.go = s_levelGoReceived;
    state.active = g_netSessionActive;
    memcpy(state.transport_slots, s_playerValid, sizeof(state.transport_slots));
    state.transport_count = s_playerCount; state.local_slot = s_localSlot;
    memcpy(state.addresses, s_playerAddr, sizeof(state.addresses));
    state.track = g_netTrackIndex; state.mode = g_netRaceSubModeIndex;
    state.weather = g_netWeatherType; state.time = g_netPlayerMode;
    state.disconnect = g_netDisconnectFlag; state.fade = g_fadeState;
    state.speed = g_fadeSpeed; state.finished = g_raceFinished;
#ifdef SONICR_TEST_GENERATIONS
    memcpy(state.generations, s_playerGeneration, sizeof(state.generations));
#endif
    return state;
}

static void unchanged(const AuthorityState *before, const char *why)
{
    AuthorityState after = capture_authority();
    if (memcmp(before, &after, sizeof(after))) {
        fprintf(stderr, "UNSAFE ACCEPT: %s\n", why);
        expect_unchanged(&before->receive);
        CHECK(0);
    }
    ++cases;
}

static void test_input_authority(void)
{
    char packet[NET_MAX_PACKET];
    for (int owner = 1; owner < NET_MAX_PLAYERS; ++owner) {
        for (int victim = 0; victim < NET_MAX_PLAYERS; ++victim) {
            if (victim == owner) continue;
            for (int body = 0; body < 2; ++body) {
                reset_fixture(1);
                int len = client_input(packet, 1, 1);
                packet[8] = (char)victim; packet[12] = (char)victim;
                incoming_slot = owner;
                AuthorityState before = capture_authority();
                dispatch(packet, body ? len : 12);
                unchanged(&before, "client input attributed to another slot");
            }
        }
    }
}

static void test_snapshot_authority(void)
{
    char packet[NET_MAX_PACKET];
    for (int slot = -1; slot < NET_MAX_PLAYERS; ++slot) {
        if (slot == 0) continue;
        int len = prepare_snapshot(packet, 1);
        incoming_slot = slot;
        AuthorityState before = capture_authority();
        dispatch(packet, len);
        unchanged(&before, "snapshot from a sender other than the host");
    }
}

static void test_control_authority(void)
{
    char packet[NET_MAX_PACKET] = {0};
    reset_fixture(1);
    /* Valid track/mode/weather/time, but a client has no config authority. */
    wl32(packet, 0xfff02000u); wl32(packet + 4, 0x00010004u);
    AuthorityState before = capture_authority();
    dispatch(packet, 48); unchanged(&before, "client overwrote the host's lobby configuration");

    reset_fixture(0);
    wl32(packet, NET_MSG_LEVEL_GO); incoming_slot = 1;
    before = capture_authority();
    dispatch(packet, 4); unchanged(&before, "GO from an unrelated sender");
}

static void test_malformed_controls(void)
{
    char packet[NET_MAX_PACKET] = {0};
    const unsigned headers[] = {NET_MSG_START_ACK, NET_MSG_LEVEL_READY, NET_MSG_LEAVE};
    for (size_t i = 0; i < sizeof(headers) / sizeof(*headers); ++i) {
        reset_fixture(1); wl32(packet, headers[i]);
        AuthorityState before = capture_authority(); dispatch(packet, 5);
        unchanged(&before, "malformed session control changed session state");
    }
}

static int control_packet(char *packet, unsigned header)
{
    memset(packet, 0, NET_MAX_PACKET);
    wl32(packet, header);
    switch (header) {
    case NET_MSG_SLOT_ASSIGN:
        wl32(packet + 4, 1); strcpy(packet + 8, "host");
        strcpy(packet + 8 + MM_MAX_USERNAME, "client");
        return 8 + NET_MAX_PLAYERS * (MM_MAX_USERNAME + NET_PLATFORM_LEN + 1);
    case NET_MSG_PEER_NAME:
        wl32(packet + 4, 2); strcpy(packet + 8, "peer");
        return 8 + MM_MAX_USERNAME + NET_PLATFORM_LEN + 1;
    case NET_MSG_START_GAME:
        wl32(packet + 4, 4);
        wl16(packet + 8, 4); wl16(packet + 10, 1);
        wl16(packet + 12, WEATHER_SNOW); wl16(packet + 14, TOD_NIGHT);
        for (int i = 0; i < NET_MAX_PLAYERS; ++i) wl16(packet + 16 + i * 2, (uint16_t)i);
        return 24;
    case NET_MSG_CHAR_CHANGE: wl16(packet + 4, 1); wl16(packet + 6, 2); return 8;
    case 0xfff02000u: wl16(packet + 4, 4); wl16(packet + 6, 1); return 48;
    case 0xff00000fu: wl32(packet + 8, 1); wl16(packet + 12, 0x1234); return 16;
    case 0xfff0004fu: wl32(packet + 8, 1); return 12;
    case 0xfff0003fu: wl32(packet + 4, 123); return 68;
    default: return 4;
    }
}

static int client_control(unsigned header)
{
    return header == NET_MSG_LEAVE || header == NET_MSG_START_ACK ||
           header == NET_MSG_LEVEL_READY || header == NET_MSG_CHAR_CHANGE ||
           header == 0xff00000fu || header == 0xfff0004fu;
}

static void control_fixture(unsigned header)
{
    reset_fixture(client_control(header));
    if (client_control(header)) g_netGameStarted = 1;
    if (header == NET_MSG_SLOT_ASSIGN) { s_localSlot = -1; s_haveSlotAssign = 0; }
    if (header == NET_MSG_START_GAME || header == 0xfff02000u) {
        g_netSessionActive = 0;
        g_localPlayerIndex = g_currentPlayerIdx = s_localSlot = 1;
        g_menuPlayer.charId = 1;
    }
}

static void test_control_lengths(void)
{
    static const unsigned headers[] = {
        NET_MSG_SLOT_ASSIGN, NET_MSG_PEER_NAME, NET_MSG_START_GAME, NET_MSG_CHAR_CHANGE,
        NET_MSG_START_ACK, NET_MSG_LEVEL_READY, NET_MSG_LEVEL_GO, NET_MSG_LEAVE,
        0xfff02000u, 0xff00000fu, 0xfff0004fu, 0xfff0003fu
    };
    char packet[NET_MAX_PACKET];
    for (size_t h = 0; h < sizeof(headers) / sizeof(*headers); ++h) {
        int length = control_packet(packet, headers[h]);
        for (int cut = 0; cut <= length + 1; ++cut) {
            if (cut == length ||
                (headers[h] == NET_MSG_SLOT_ASSIGN && cut == 8 + NET_MAX_PLAYERS * MM_MAX_USERNAME) ||
                (headers[h] == NET_MSG_PEER_NAME && cut == 8 + MM_MAX_USERNAME) ||
                (headers[h] == NET_MSG_START_GAME && (cut == 8 || cut == 16))) continue;
            control_fixture(headers[h]);
            AuthorityState before = capture_authority();
            dispatch(packet, cut); unchanged(&before, "truncated or overlong session control");
        }
    }
}

static void test_control_fields(void)
{
    char packet[NET_MAX_PACKET];
    for (int fault = 0; fault < 12; ++fault) {
        control_fixture(NET_MSG_START_GAME);
        int length = control_packet(packet, NET_MSG_START_GAME);
        switch (fault) {
        case 0: wl32(packet + 4, UINT32_MAX); break;
        case 1: wl32(packet + 4, 0); break;
        case 2: wl32(packet + 4, 1); break;
        case 3: wl32(packet + 4, NET_MAX_PLAYERS + 1); break;
        case 4: wl16(packet + 8, 0xffff); break;
        case 5: wl16(packet + 8, 5); break;
        case 6: wl16(packet + 10, 2); break;
        case 7: wl16(packet + 12, WEATHER_SNOW + 1); break;
        case 8: wl16(packet + 14, TOD_NIGHT + 1); break;
        case 9: wl16(packet + 16, CHAR_COUNT); break;
        case 10: wl16(packet + 18, 0xffff); break;
        case 11: g_localPlayerIndex = 0; break;
        }
        AuthorityState before = capture_authority(); dispatch(packet, length);
        unchanged(&before, "invalid START metadata was partially published");
    }
    for (int slot = -1; slot <= NET_MAX_PLAYERS; ++slot) {
        if (slot == 1) continue;
        control_fixture(NET_MSG_SLOT_ASSIGN);
        int length = control_packet(packet, NET_MSG_SLOT_ASSIGN);
        wl32(packet + 4, (uint32_t)slot);
        if (slot > 0 && slot < NET_MAX_PLAYERS) s_localSlot = 1;
        AuthorityState before = capture_authority(); dispatch(packet, length);
        unchanged(&before, "invalid or conflicting slot assignment");
    }
    for (int field = 0; field < 4; ++field) {
        control_fixture(0xfff02000u);
        int length = control_packet(packet, 0xfff02000u);
        wl16(packet + 4 + field * 2, 0x7fff);
        AuthorityState before = capture_authority(); dispatch(packet, length);
        unchanged(&before, "invalid lobby preview/configuration index");
    }
    for (int kind = 0; kind < 3; ++kind) {
        unsigned header = kind == 0 ? NET_MSG_CHAR_CHANGE : kind == 1 ? 0xff00000fu : 0xfff0004fu;
        for (int victim = 0; victim < NET_MAX_PLAYERS; ++victim) {
            if (victim == 1) continue;
            control_fixture(header);
            int length = control_packet(packet, header);
            if (kind == 0) wl16(packet + 4, (uint16_t)victim);
            else wl32(packet + 8, (uint32_t)victim);
            AuthorityState before = capture_authority(); dispatch(packet, length);
            unchanged(&before, "client control claimed a different player's slot");
        }
    }
}

static void test_valid_controls(void)
{
    char packet[NET_MAX_PACKET];
    static const unsigned simple[] = {NET_MSG_START_ACK, NET_MSG_LEVEL_READY, NET_MSG_LEVEL_GO,
                                     NET_MSG_CHAR_CHANGE, 0xff00000fu, 0xfff0004fu, 0xfff0003fu};
    for (size_t i = 0; i < sizeof(simple) / sizeof(*simple); ++i) {
        unsigned header = simple[i];
        control_fixture(header); int length = control_packet(packet, header);
        dispatch(packet, length);
        if (header == NET_MSG_START_ACK) CHECK(s_startAckMask == 3);
        if (header == NET_MSG_LEVEL_READY) CHECK(s_levelReadyMask == 3);
        if (header == NET_MSG_LEVEL_GO) CHECK(s_levelGoReceived == 1);
        if (header == NET_MSG_CHAR_CHANGE) CHECK(g_playerBase[1].charId == 2 && g_playerBase[0].charId == 0);
        if (header == 0xff00000fu) CHECK(s_netInputBuffer[1] == 0x1234);
        if (header == 0xfff0004fu) CHECK(g_netPlayerAlive[1] == 1 && g_netPlayerAlive[0] == 0);
        if (header == 0xfff0003fu) CHECK(g_playerBase[0].lap1Time == 123);
        ++cases;
    }
    for (int length = 8; length <= 24; length += 8) {
        control_fixture(NET_MSG_START_GAME); (void)control_packet(packet, NET_MSG_START_GAME);
        dispatch(packet, length);
        CHECK(g_netSessionActive && g_netPlayerCount == 4 && s_haveSlotAssign);
        if (length >= 16) CHECK(g_netTrackIndex == 4 && g_netRaceSubModeIndex == 1 && g_netPlayerMode == TOD_NIGHT);
        AuthorityState before = capture_authority(); dispatch(packet, length);
        unchanged(&before, "duplicate START reset a running race");
        ++cases;
    }
    static const int assignments[] = {8 + NET_MAX_PLAYERS * MM_MAX_USERNAME,
                                     8 + NET_MAX_PLAYERS * (MM_MAX_USERNAME + NET_PLATFORM_LEN + 1)};
    for (size_t i = 0; i < sizeof(assignments) / sizeof(*assignments); ++i) {
        control_fixture(NET_MSG_SLOT_ASSIGN); (void)control_packet(packet, NET_MSG_SLOT_ASSIGN);
        dispatch(packet, assignments[i]);
        CHECK(s_haveSlotAssign && net_local_slot() == 1 && g_localPlayerIndex == 1);
        ++cases;
    }
    control_fixture(0xfff02000u); int length = control_packet(packet, 0xfff02000u);
    dispatch(packet, length); CHECK(g_netReceivedLobbyData && g_netLobbyConfigBuf[1] == 0x10004); ++cases;
    for (int extended = 0; extended <= 1; ++extended) {
        control_fixture(NET_MSG_PEER_NAME); length = control_packet(packet, NET_MSG_PEER_NAME);
        dispatch(packet, extended ? length : 8 + MM_MAX_USERNAME);
        CHECK(!strcmp(net_get_slot_name(2), "peer")); ++cases;
    }
    control_fixture(NET_MSG_LEAVE); length = control_packet(packet, NET_MSG_LEAVE);
    dispatch(packet, length);
    CHECK(!net_slot_is_connected(1) && !g_playerBase[1].netActive && !g_playerBase[1].netConnected);
    CHECK(net_slot_is_connected(2) && g_playerBase[2].netActive && g_netPlayerCount == NET_MAX_PLAYERS);
    AuthorityState before = capture_authority(); dispatch(packet, length);
    unchanged(&before, "duplicate LEAVE changed another participant");
    ++cases;
}

static net_socket_t raw_peer(void)
{
    net_socket_t peer = socket(AF_INET, SOCK_DGRAM, 0);
    CHECK(socket_is_valid(peer));
    struct sockaddr_in address;
    memset(&address, 0, sizeof(address)); address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    CHECK(bind(peer, (struct sockaddr *)&address, sizeof(address)) == 0);
    CHECK(set_nonblocking(peer) == 0);
    return peer;
}

static struct sockaddr_in bound_address(net_socket_t socket)
{
    struct sockaddr_in address;
    socklen_t size = sizeof(address);
    CHECK(getsockname(socket, (struct sockaddr *)&address, &size) == 0);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    return address;
}

static void raw_send(net_socket_t peer, const struct sockaddr_in *to, const char *packet, int len)
{
    CHECK(sendto(peer, packet, len, 0, (const struct sockaddr *)to, sizeof(*to)) == len);
}

static void pump(void)
{
    for (int i = 0; i < 4; ++i) {
        ApplyNetworkPlayerState();
        if (s_threadRunning) usleep(1000);
    }
}

static void start_host(void)
{
    real_mode = 1;
    CHECK(net_transport_init() == 0);
    CHECK(net_host_start(0) == 0);
    reset_fixture(1);
    g_netPlayerCount = 1; g_netGameStarted = 1; g_netSessionActive = 0;
    memset(g_netPlayerDecorations + NET_DECO_STRIDE, 0, (NET_MAX_PLAYERS - 1) * NET_DECO_STRIDE);
}

static void test_loopback_unexpected(void)
{
    char packet[NET_MAX_PACKET] = {0};
    start_host();
    net_socket_t peer = raw_peer();
    struct sockaddr_in address = bound_address(s_socket);
    AuthorityState before = capture_authority();
    wl32(packet, 0xdeadbeefu);
    puts("loopback: sending an unexpected first packet to the production host");
    raw_send(peer, &address, packet, 4);
    pump();
    unchanged(&before, "unexpected UDP sender reserved a slot or changed host state");
    NET_CLOSESOCKET(peer); net_close(); real_mode = 0;
}

static void receive_raw(char *packet, int *len, int *slot, uint64_t *generation)
{
    *len = 0;
    for (int i = 0; i < 100 && *len == 0; ++i) {
#ifdef SONICR_TEST_GENERATIONS
        *len = authority_socket_recv_packet(packet, NET_MAX_PACKET, slot, generation);
#else
        *generation = 1;
        *len = authority_socket_recv(packet, NET_MAX_PACKET, slot);
#endif
        CHECK(*len >= 0);
        if (*len == 0) usleep(1000);
    }
    CHECK(*len > 0);
}

static void test_stale_queued_join(void)
{
    char join[4 + MM_MAX_USERNAME] = {0}, old_join[NET_MAX_PACKET], new_join[NET_MAX_PACKET];
    int old_len, old_slot, new_len, new_slot;
    uint64_t old_generation, new_generation;
    start_host();
    net_socket_t old_peer = raw_peer(), new_peer = raw_peer();
    struct sockaddr_in address = bound_address(s_socket);
    wl32(join, NET_MSG_JOIN_REQ); strcpy(join + 4, "old_peer");
    raw_send(old_peer, &address, join, sizeof(join)); pump();
    CHECK(net_slot_is_connected(1));
    /* The receive thread can have a duplicate JOIN queued when the main
     * thread handles LEAVE, while another endpoint reserves that slot. */
    raw_send(old_peer, &address, join, sizeof(join));
    receive_raw(old_join, &old_len, &old_slot, &old_generation);
    char leave[4]; wl32(leave, NET_MSG_LEAVE);
    incoming_slot = 1; dispatch(leave, sizeof(leave));
    strcpy(join + 4, "new_peer");
    raw_send(new_peer, &address, join, sizeof(join));
    receive_raw(new_join, &new_len, &new_slot, &new_generation);
    CHECK(old_slot == 1 && new_slot == 1);
    AuthorityState before = capture_authority();
#ifdef SONICR_TEST_GENERATIONS
    CHECK(old_generation != new_generation);
    net_enqueue(old_join, old_len, old_slot, old_generation);
#else
    (void)old_generation; (void)new_generation;
    net_enqueue(old_join, old_len, old_slot);
#endif
    s_threadRunning = 1;
    ApplyNetworkPlayerState();
    s_threadRunning = 0;
    unchanged(&before, "queued old-endpoint JOIN authorized a replacement endpoint");
    incoming_slot = new_slot; dispatch(new_join, new_len);
    CHECK(net_slot_is_connected(1) && !strcmp(net_get_slot_name(1), "new_peer"));
    NET_CLOSESOCKET(old_peer); NET_CLOSESOCKET(new_peer); net_close(); real_mode = 0;
}

static void make_join(char *packet, const char *name)
{
    memset(packet, 0, 56); wl32(packet, NET_MSG_JOIN_REQ);
    strcpy(packet + 4, name); strcpy(packet + 4 + MM_MAX_USERNAME, "windows");
}

static void expect_raw_header(net_socket_t peer, unsigned header, int expected_len)
{
    char packet[NET_MAX_PACKET];
    for (int i = 0; i < 200; ++i) {
        int len = (int)recvfrom(peer, packet, sizeof(packet), 0, NULL, NULL);
        if (len >= 4 && rl32u(packet) == header) { CHECK(len == expected_len); return; }
        usleep(1000);
    }
    CHECK(0);
}

static void test_loopback_admission(void)
{
    char packet[1024];
    start_host(); net_socket_t peer = raw_peer();
    struct sockaddr_in address = bound_address(s_socket);
    make_join(packet, "peer");
    for (int len = 0; len < 4 + MM_MAX_USERNAME; ++len) {
        AuthorityState before = capture_authority();
        raw_send(peer, &address, packet, len); pump();
        unchanged(&before, "malformed JOIN reserved a player slot");
    }
    static const int oversized[] = {NET_MAX_PACKET + 1, 1024};
    for (size_t i = 0; i < sizeof(oversized) / sizeof(*oversized); ++i) {
        memset(packet, 0, sizeof(packet)); make_join(packet, "peer");
        AuthorityState before = capture_authority(); raw_send(peer, &address, packet, oversized[i]); pump();
        unchanged(&before, "oversized/truncated UDP datagram was admitted");
    }
    wl32(packet, NET_DISCOVER_MAGIC); raw_send(peer, &address, packet, 4); pump();
    CHECK(g_netPlayerCount == 1 && s_playerCount == 1);
    expect_raw_header(peer, NET_DISCOVER_REPLY, 4); ++cases;
    static const int joins[] = {4 + MM_MAX_USERNAME, 4 + MM_MAX_USERNAME + 17, 56};
    for (size_t i = 0; i < sizeof(joins) / sizeof(*joins); ++i) {
        make_join(packet, "peer"); raw_send(peer, &address, packet, joins[i]); pump();
        CHECK(net_slot_is_connected(1) && g_netPlayerCount == 2);
        expect_raw_header(peer, NET_MSG_SLOT_ASSIGN, 8 + NET_MAX_PLAYERS * (MM_MAX_USERNAME + NET_PLATFORM_LEN + 1));
        wl32(packet, NET_MSG_LEAVE); raw_send(peer, &address, packet, 4); pump();
        CHECK(!net_slot_is_connected(1) && g_netPlayerCount == 1); ++cases;
    }
    NET_CLOSESOCKET(peer); net_close(); real_mode = 0;
}

static void test_loopback_host_lifecycle(int threaded)
{
    char packet[NET_MAX_PACKET], join[56];
    start_host();
    net_socket_t client = raw_peer(), second = raw_peer(), outsider = raw_peer();
    struct sockaddr_in address = bound_address(s_socket);
    if (threaded) NetRecvThread_Start();
    make_join(join, "client"); raw_send(client, &address, join, sizeof(join)); pump();
    CHECK(net_slot_is_connected(1));
    make_join(join, "second"); raw_send(second, &address, join, sizeof(join)); pump();
    CHECK(net_slot_is_connected(2) && g_netPlayerCount == 3);
    make_join(join, "client"); raw_send(client, &address, join, sizeof(join)); pump();
    CHECK(g_netPlayerCount == 3 && net_slot_is_connected(2)); ++cases;
    wl32(packet, NET_MSG_START_ACK);
    raw_send(client, &address, packet, 4); raw_send(second, &address, packet, 4);
    InitNetworkGame();
    CHECK(g_netSessionActive && s_startAckMask == 7);
    wl32(packet, NET_MSG_LEVEL_READY);
    raw_send(client, &address, packet, 4); raw_send(second, &address, packet, 4);
    NetLevelSyncBarrier();
    CHECK(s_levelReadyMask == 7);
    expect_raw_header(client, NET_MSG_LEVEL_GO, 4); ++cases;
    int length = client_input(packet, 1, 1);
    raw_send(client, &address, packet, length); pump();
    CHECK(g_playerBase[1].posX == source_players[1].posX && s_lastRecvInputSeq[1] == 1); ++cases;
    AuthorityState before = capture_authority();
    raw_send(outsider, &address, packet, length); pump(); unchanged(&before, "unknown endpoint updated a player");
    packet[8] = packet[12] = 0;
    raw_send(client, &address, packet, length); pump(); unchanged(&before, "established endpoint updated the host slot");
    wl32(packet, NET_MSG_LEAVE); packet[4] = 0;
    raw_send(client, &address, packet, 5); pump(); unchanged(&before, "malformed LEAVE disconnected a legitimate client");
    raw_send(outsider, &address, packet, 4); pump(); unchanged(&before, "outsider LEAVE changed the session");
    make_join(join, "outsider"); raw_send(outsider, &address, join, sizeof(join)); pump();
    unchanged(&before, "out-of-session JOIN entered an active race");
    wl32(packet, NET_MSG_LEAVE); raw_send(client, &address, packet, 4); pump();
    CHECK(!net_slot_is_connected(1) && !g_playerBase[1].netActive);
    CHECK(net_slot_is_connected(2) && g_netPlayerCount == 3); ++cases;
    raw_send(second, &address, packet, 4); pump();
    CHECK(!net_slot_is_connected(2) && g_netPlayerCount == 1); ++cases;
    CloseDirectPlaySession(); CHECK(!net_is_active());
    NET_CLOSESOCKET(client); NET_CLOSESOCKET(second); NET_CLOSESOCKET(outsider); real_mode = 0;
}

static void test_loopback_client(void)
{
    char packet[NET_MAX_PACKET];
    real_mode = 1; CHECK(net_transport_init() == 0);
    net_socket_t server = raw_peer(), outsider = raw_peer();
    struct sockaddr_in server_address = bound_address(server);
    CHECK(net_client_connect("127.0.0.1", ntohs(server_address.sin_port)) == 0);
    struct sockaddr_in address = bound_address(s_socket);
    reset_fixture(0); s_haveSlotAssign = 0; g_netSessionActive = 0;
    int length = control_packet(packet, NET_MSG_SLOT_ASSIGN);
    AuthorityState before = capture_authority();
    raw_send(outsider, &address, packet, length); pump(); unchanged(&before, "outsider assigned a client slot");
    raw_send(server, &address, packet, length); pump();
    CHECK(s_haveSlotAssign && g_localPlayerIndex == 1 && net_local_slot() == 1); ++cases;
    length = control_packet(packet, NET_MSG_START_GAME);
    raw_send(server, &address, packet, length); pump();
    CHECK(g_netSessionActive && g_netTrackIndex == 4);
    expect_raw_header(server, NET_MSG_START_ACK, 4); ++cases;
    length = snapshot(packet, 1, 1);
    before = capture_authority();
    raw_send(outsider, &address, packet, length); pump(); unchanged(&before, "outsider snapshot changed client state");
    raw_send(server, &address, packet, length); pump();
    CHECK(s_lastRecvSnapSeq == 1 && g_playerBase[0].posX == source_players[0].posX); ++cases;
    wl32(packet, NET_MSG_LEVEL_GO); s_levelGoReceived = 0;
    before = capture_authority(); raw_send(outsider, &address, packet, 4); pump();
    unchanged(&before, "outsider GO changed client load state");
    raw_send(server, &address, packet, 4); pump(); CHECK(s_levelGoReceived); ++cases;
    CloseDirectPlaySession(); CHECK(!net_is_active() && !s_haveSlotAssign);
    expect_raw_header(server, NET_MSG_LEAVE, 4); ++cases;
    NET_CLOSESOCKET(server); NET_CLOSESOCKET(outsider); real_mode = 0;
}

static void test_leave_during_barrier(void)
{
    reset_fixture(1);
    net_unregister_slot(2); net_unregister_slot(3); g_netPlayerCount = 2;
    char packet[4]; wl32(packet, NET_MSG_LEAVE);
    memcpy(incoming, packet, 4); incoming_len = 4; incoming_slot = 1; packet_ready = 1;
    puts("barrier: established peer leaves while the host waits for readiness");
    NetLevelSyncBarrier();
    CHECK(g_netPlayerCount == 1 && !net_slot_is_connected(1)); ++cases;
}

static void test_legacy_wait_authority(void)
{
    for (int fault = 0; fault < 3; ++fault) {
        reset_fixture(0);
        char bad[17] = {0}, good[16] = {0};
        wl32(bad, 0xff0000f0u); wl16(bad + 4, 42);
        wl32(good, 0xff0000f0u); wl16(good + 4, 123);
        for (int i = 0; i < NET_MAX_PLAYERS; ++i) wl16(good + 6 + i * 2, (uint16_t)(100 + i));
        int bad_slot = fault == 2 ? 1 : 0;
        int bad_length = fault == 0 ? 17 : fault == 1 ? 15 : 16;
        /* Exercise the dispatcher's role check too, independently of the
         * transport's rejection of client-side non-host addresses. */
        s_playerValid[1] = s_playerValid[0];
        s_queueHead = s_queueTail = 0;
#ifdef SONICR_TEST_GENERATIONS
        net_enqueue(bad, bad_length, bad_slot, s_playerGeneration[bad_slot]);
        net_enqueue(good, sizeof(good), 0, s_playerGeneration[0]);
#else
        net_enqueue(bad, bad_length, bad_slot);
        net_enqueue(good, sizeof(good), 0);
#endif
        s_threadRunning = 1;
        WaitForNetworkData();
        s_threadRunning = 0;
        CHECK(g_netSessionFrame == 123 && g_gameDataPacketFrame == 123);
        for (int i = 0; i < NET_MAX_PLAYERS; ++i)
            CHECK(g_perPlayerInput[i] == 100 + i && g_netRecvInput[i] == 100 + i);
        CHECK(s_queueHead == s_queueTail);
        ++cases;
    }
}

static void test_disconnect_watchdogs(void)
{
    char packet[NET_MAX_PACKET];
    int length = prepare_snapshot(packet, 1);
    dispatch(packet, length);
    DWORD heartbeat = s_lastHostPacketMs;
    g_introCountdown = 0; now_ms = heartbeat + 5000;
    ApplyNetworkPlayerState(); CHECK(!g_netDisconnectFlag); ++cases;
    now_ms = heartbeat + 5001;
    ApplyNetworkPlayerState();
    CHECK(g_netDisconnectFlag && g_fadeState == FADE_OUT && !s_haveSlotAssign); ++cases;

    reset_fixture(1);
    net_unregister_slot(3); g_netPlayerCount = 3;
    now_ms = 7001;
    s_hostSlotLastRecvMs[1] = 1000; s_hostSlotLastRecvMs[2] = 7000;
    SendNetworkHostData();
    CHECK(!net_slot_is_connected(1) && !g_playerBase[1].netActive);
    CHECK(net_slot_is_connected(2) && g_playerBase[2].netActive && !g_netDisconnectFlag); ++cases;
    now_ms = 12001;
    SendNetworkHostData();
    CHECK(!net_slot_is_connected(2) && g_netDisconnectFlag && g_fadeState == FADE_OUT); ++cases;
}

int main(int argc, char **argv)
{
    const char *which = argc > 1 ? argv[1] : "all";
    setvbuf(stdout, NULL, _IONBF, 0);
#ifdef _WIN32
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
    if (!strcmp(which, "all") || !strcmp(which, "input")) test_input_authority();
    if (!strcmp(which, "all") || !strcmp(which, "snapshot")) test_snapshot_authority();
    if (!strcmp(which, "all") || !strcmp(which, "control")) test_control_authority();
    if (!strcmp(which, "all") || !strcmp(which, "malformed")) {
        test_malformed_controls(); test_control_lengths();
    }
    if (!strcmp(which, "all") || !strcmp(which, "fields")) test_control_fields();
    if (!strcmp(which, "all") || !strcmp(which, "stale")) test_stale_queued_join();
    if (!strcmp(which, "all") || !strcmp(which, "loopback") || !strcmp(which, "crash"))
        test_loopback_unexpected();
    if (!strcmp(which, "all") || !strcmp(which, "loopback")) {
        test_loopback_admission(); test_loopback_host_lifecycle(0);
        test_loopback_host_lifecycle(1); test_loopback_client();
    }
    if (!strcmp(which, "all") || !strcmp(which, "barrier")) test_leave_during_barrier();
    if (!strcmp(which, "all") || !strcmp(which, "wait")) test_legacy_wait_authority();
    if (!strcmp(which, "all") || !strcmp(which, "valid")) {
        test_disconnect_watchdogs();
        test_valid_controls();
        char packet[NET_MAX_PACKET];
        int len = prepare_input(packet, 1); dispatch(packet, len); expect_input_committed(); ++cases;
        len = prepare_snapshot(packet, 1); dispatch(packet, len); expect_snapshot_committed(); ++cases;
    }
    CHECK(cases != 0);
    printf("network authority: %d %s cases passed\n", cases, which);
    return 0;
}
