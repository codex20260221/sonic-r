/* Include the real receive implementation to inspect its private histories.
 * No source is extracted/reimplemented, no socket/thread/SDL window is opened. */
#include "sonicr_types.h"
static DWORD now_ms;
static DWORD fixture_time(void) { return now_ms; }
#define timeGetTime fixture_time
#include SONICR_TEST_NETWORK_SOURCE
#undef timeGetTime
#include "../source/sdl/src/net/net_interp.c"

static int host, cases, packet_ready, incoming_len;
static char incoming[NET_MAX_PACKET];
static Player source_players[NET_MAX_PLAYERS];
static NetDeltaSendState send_states[NET_MAX_PLAYERS];

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "line %d: %s\n", __LINE__, #condition); exit(1); \
} } while (0)

/* All transport operations are deterministic stubs. Unexpected sends fail the
 * test, and no real transport or matchmaker object is linked. */
int net_is_host(void) { return host; }
int net_is_active(void) { return 1; }
int net_recv(void *buf, int maxlen, int *from_slot)
{
    if (!packet_ready) return 0;
    CHECK(incoming_len <= maxlen);
    memcpy(buf, incoming, (size_t)incoming_len);
    *from_slot = host ? 1 : 0;
    packet_ready = 0;
    return incoming_len;
}
int net_broadcast(const void *p, int n) { (void)p; (void)n; CHECK(0); return -1; }
int net_send_to(int s, const void *p, int n) { (void)s; (void)p; (void)n; CHECK(0); return -1; }
int net_send_to_host(const void *p, int n) { (void)p; (void)n; CHECK(0); return -1; }
int net_slot_is_connected(int slot) { (void)slot; return 0; }
int net_register_client(int slot) { (void)slot; CHECK(0); return -1; }
void net_unregister_slot(int slot) { (void)slot; CHECK(0); }
void net_set_local_slot(int slot) { (void)slot; CHECK(0); }
int net_local_slot(void) { CHECK(0); return -1; }
void net_close(void) { CHECK(0); }
int ReadLocalInput(void) { CHECK(0); return 0; }
void platform_pump_events(void) { CHECK(0); }
const char *MatchmakerGetUsername(void) { CHECK(0); return ""; }
void DebugLog(char *fmt, ...) { (void)fmt; }
uint32_t platform_get_time_ms(void) { return (uint32_t)now_ms; }
void DeriveGroundState(Player *pl) { (void)pl; CHECK(0); }

typedef struct {
    Player players[MAX_PLAYERS];
    NetDeltaRecvState history[NET_MAX_PLAYERS];
    uint16_t snapshot_seq, input_seq[NET_MAX_PLAYERS];
    unsigned short inputs[MAX_PLAYERS], versions[MAX_PLAYERS];
    unsigned char version_seen[MAX_PLAYERS];
    int received[NET_MAX_PLAYERS], finish_counter, session_frame, packet_frame;
    DWORD host_time, slot_times[NET_MAX_PLAYERS];
    NetSnap prev[MAX_PLAYERS], curr[MAX_PLAYERS];
    uint32_t intervals[MAX_PLAYERS];
} ReceiveState;

static ReceiveState capture(void)
{
    ReceiveState state;
    memset(&state, 0, sizeof(state));
    memcpy(state.players, g_playerBase, sizeof(state.players));
    memcpy(state.history, s_deltaRecv, sizeof(state.history));
    memcpy(state.input_seq, s_lastRecvInputSeq, sizeof(state.input_seq));
    memcpy(state.inputs, s_netInputBuffer, sizeof(state.inputs));
    memcpy(state.versions, s_peerProtoVersion, sizeof(state.versions));
    memcpy(state.version_seen, s_peerProtoSeen, sizeof(state.version_seen));
    memcpy(state.received, g_netPlayerRecvd, sizeof(state.received));
    memcpy(state.slot_times, s_hostSlotLastRecvMs, sizeof(state.slot_times));
    memcpy(state.prev, s_prev, sizeof(state.prev));
    memcpy(state.curr, s_curr, sizeof(state.curr));
    memcpy(state.intervals, s_intervalMs, sizeof(state.intervals));
    state.snapshot_seq = s_lastRecvSnapSeq;
    state.finish_counter = g_finishOrderCounter;
    state.session_frame = g_netSessionFrame;
    state.packet_frame = g_gameDataPacketFrame;
    state.host_time = s_lastHostPacketMs;
    return state;
}

static void expect_unchanged(const ReceiveState *before)
{
    ReceiveState after = capture();
#define SAME(member) do { if (memcmp(&before->member, &after.member, sizeof(after.member))) { \
    fprintf(stderr, "rejected packet changed %s\n", #member); exit(1); } } while (0)
    SAME(history); SAME(snapshot_seq); SAME(input_seq); SAME(players);
    SAME(inputs); SAME(received); SAME(finish_counter); SAME(session_frame);
    SAME(packet_frame); SAME(host_time); SAME(slot_times); SAME(versions);
    SAME(version_seen); SAME(prev); SAME(curr); SAME(intervals);
#undef SAME
}

static void dispatch(const char *packet, int len)
{
    CHECK(len >= 0 && len <= NET_MAX_PACKET);
    memcpy(incoming, packet, (size_t)len);
    incoming_len = len;
    packet_ready = 1;
    ApplyNetworkPlayerState();
    CHECK(!packet_ready);
}

static void reset_fixture(int is_host)
{
    host = is_host;
    now_ms = 1000;
    NetSnapshotReset();
    memset(g_playerBase, 0, sizeof(Player) * MAX_PLAYERS);
    memset(source_players, 0, sizeof(source_players));
    memset(s_netInputBuffer, 0, sizeof(s_netInputBuffer));
    memset(g_netPlayerRecvd, 0, sizeof(int) * NET_MAX_PLAYERS);
    memset(s_hostSlotLastRecvMs, 0, sizeof(s_hostSlotLastRecvMs));
    s_lastHostPacketMs = 0;
    s_threadRunning = 0;
    g_netPlayerCount = NET_MAX_PLAYERS;
    g_localPlayerIndex = is_host ? 0 : 3;
    g_netSessionActive = 1;
    g_netGameStarted = 0;
    g_introCountdown = 10;
    g_netDisconnectFlag = 0;
    g_finishOrderCounter = 5;
    g_netSessionFrame = 77;
    g_gameDataPacketFrame = 78;
    for (int i = 0; i < MAX_PLAYERS; ++i) {
        g_playerBase[i].netActive = 1;
        g_playerBase[i].posX = 9000 + i;
        g_playerBase[i].charId = (short)i;
        g_playerBase[i].trackProgress = 1234;
    }
    for (int i = 0; i < NET_MAX_PLAYERS; ++i) {
        net_delta_reset_send(&send_states[i]);
        source_players[i].posX = 1000 + i * 100;
        source_players[i].posY = 2000 + i * 100;
        source_players[i].lapsCompleted = 2;
    }
}

static int snapshot(char *packet, int keyframe, uint16_t seq)
{
    wl32(packet, NET_MSG_SNAPSHOT);
    wl16(packet + 4, seq);
    wl16(packet + 6, 123);
    packet[8] = NET_MAX_PLAYERS;
    packet[9] = (char)keyframe;
    wl16(packet + 10, 1);
    int offset = SNAP_HEADER_SIZE;
    for (int i = 0; i < NET_MAX_PLAYERS; ++i) {
        int len = net_delta_encode(&send_states[i], &source_players[i], i, keyframe,
                                    (unsigned char *)packet + offset, NET_MAX_PACKET - offset);
        CHECK(len > 0);
        offset += len;
    }
    return offset;
}

static int prepare_snapshot(char *packet, int keyframe)
{
    reset_fixture(0);
    int len = snapshot(packet, 1, 10);
    dispatch(packet, len);
    CHECK(s_lastRecvSnapSeq == 10 && s_deltaRecv[3].valid);
    CHECK(g_playerBase[3].posX == 9003); /* local physics remains authoritative */
    now_ms += 40;
    for (int i = 0; i < NET_MAX_PLAYERS; ++i) {
        source_players[i].posX += 100;
        source_players[i].posY -= 100;
    }
    source_players[0].lapsCompleted = 3;
    return snapshot(packet, keyframe, 11);
}

static void expect_snapshot_committed(void)
{
    CHECK(s_lastRecvSnapSeq == 11 && g_netSessionFrame == 123);
    CHECK(g_playerBase[0].posX == 1100 && g_playerBase[1].posY == 2000);
    CHECK(g_playerBase[3].posX == 9003 && s_deltaRecv[3].state[0] == 1400);
    CHECK(g_finishOrderCounter == 6 && g_playerBase[0].trackProgress == -6);
    CHECK(s_curr[0].posX == 1100 && s_curr[0].recvTimeMs == now_ms);
    CHECK(s_lastHostPacketMs == now_ms);
}

static void test_snapshot_truncations(void)
{
    char packet[NET_MAX_PACKET];
    for (int keyframe = 0; keyframe <= 1; ++keyframe) {
        int length = prepare_snapshot(packet, keyframe);
        for (int cut = 0; cut < length; ++cut) {
            CHECK(prepare_snapshot(packet, keyframe) == length);
            ReceiveState before = capture();
            dispatch(packet, cut);
            expect_unchanged(&before);
            dispatch(packet, length);
            expect_snapshot_committed();
            ++cases;
        }
    }
}

static void test_snapshot_structure(void)
{
    char packet[NET_MAX_PACKET];
    for (int fault = 0; fault < 7; ++fault) {
        int len = prepare_snapshot(packet, 1);
        switch (fault) {
        case 0: packet[8] = NET_MAX_PLAYERS + 1; break;
        case 1: packet[SNAP_HEADER_SIZE] = NET_MAX_PLAYERS; break;
        case 2: packet[SNAP_HEADER_SIZE + 1 + NET_DELTA_KEYFRAME_BODY] = 0; break;
        case 3: packet[len++] = 0x55; break;
        case 4: packet[8] = NET_MAX_PLAYERS - 1; break;
        case 5: /* Missing remote predecessor must reject the entire delta. */
        case 6: /* The skipped local entry must also be fully decodable. */
            len = snapshot(packet, 0, 11);
            s_deltaRecv[fault == 5 ? 1 : 3].valid = 0;
            break;
        }
        ReceiveState before = capture();
        dispatch(packet, len);
        expect_unchanged(&before);
        ++cases;
    }
    /* Lobby snapshots update frame bookkeeping only after complete validation. */
    int len = prepare_snapshot(packet, 1);
    g_netGameStarted = 1;
    ReceiveState before = capture();
    dispatch(packet, len - 1);
    expect_unchanged(&before);
    ++cases;
}

static int client_input(char *packet, int keyframe, uint16_t seq)
{
    wl32(packet, NET_MSG_CLIENT_INPUT);
    wl16(packet + 4, seq);
    wl16(packet + 6, 0x1234);
    packet[8] = 1;
    packet[9] = (char)keyframe;
    wl16(packet + 10, 1);
    int len = net_delta_encode(&send_states[1], &source_players[1], 1, keyframe,
                               (unsigned char *)packet + 12, NET_MAX_PACKET - 12);
    CHECK(len > 0);
    return 12 + len;
}

static int prepare_input(char *packet, int keyframe)
{
    reset_fixture(1);
    int len = client_input(packet, 1, 10);
    dispatch(packet, len);
    CHECK(s_lastRecvInputSeq[1] == 10 && s_deltaRecv[1].valid);
    g_netPlayerRecvd[1] = 0;
    now_ms += 40;
    source_players[1].posX += 100;
    source_players[1].posY -= 100;
    source_players[1].lapsCompleted = 3;
    len = client_input(packet, keyframe, 11);
    wl16(packet + 6, 0x5678);
    wl16(packet + 10, 0); /* accepted legacy version marker */
    return len;
}

static void expect_input_committed(void)
{
    CHECK(s_lastRecvInputSeq[1] == 11 && s_netInputBuffer[1] == 0x5678);
    CHECK(g_playerBase[1].posX == 1200 && g_playerBase[1].posY == 2000);
    CHECK(g_netPlayerRecvd[1] == 1 && s_hostSlotLastRecvMs[1] == now_ms);
    CHECK(s_peerProtoVersion[1] == 0 && g_finishOrderCounter == 6);
    CHECK(s_curr[1].recvTimeMs == now_ms && s_curr[1].posX == 1200);
}

static void test_input_truncations(void)
{
    char packet[NET_MAX_PACKET];
    for (int keyframe = 0; keyframe <= 1; ++keyframe) {
        int length = prepare_input(packet, keyframe);
        for (int cut = 0; cut < length; ++cut) {
            if (cut == 12) continue; /* supported legacy input-only packet */
            CHECK(prepare_input(packet, keyframe) == length);
            ReceiveState before = capture();
            dispatch(packet, cut);
            expect_unchanged(&before);
            dispatch(packet, length);
            expect_input_committed();
            ++cases;
        }
    }
}

static void test_input_structure(void)
{
    char packet[NET_MAX_PACKET];
    for (int fault = 0; fault < 5; ++fault) {
        int len = prepare_input(packet, 1);
        switch (fault) {
        case 0: packet[12] = 2; break; /* header/body slot mismatch */
        case 1: packet[len++] = 0x55; break;
        case 2: packet[8] = NET_MAX_PLAYERS; break;
        case 3: packet[8] = 255; break;
        case 4:
            len = client_input(packet, 0, 11);
            s_deltaRecv[1].valid = 0;
            break;
        }
        ReceiveState before = capture();
        dispatch(packet, len);
        expect_unchanged(&before);
        ++cases;
    }
}

static void test_late_snapshot_failure(void)
{
    char packet[NET_MAX_PACKET];
    int len = prepare_snapshot(packet, 1);
    ReceiveState before = capture();
    /* Three remote entries, including a finish transition, are complete;
     * the fourth/local entry is incomplete. None may be published. */
    dispatch(packet, len - 1);
    expect_unchanged(&before);
    ++cases;
}

static void test_failed_decode_interpolation(void)
{
    char packet[NET_MAX_PACKET];
    (void)prepare_input(packet, 1);
    ReceiveState before = capture();
    dispatch(packet, 13); /* body slot only: decoder must return failure */
    CHECK(memcmp(s_curr, before.curr, sizeof(before.curr)) == 0);
    CHECK(memcmp(s_prev, before.prev, sizeof(before.prev)) == 0);
    CHECK(memcmp(s_intervalMs, before.intervals, sizeof(before.intervals)) == 0);
    expect_unchanged(&before);
    ++cases;
}

static void test_valid_snapshot_shapes(void)
{
    char packet[NET_MAX_PACKET];
    for (int count = 0; count <= NET_MAX_PLAYERS; ++count) {
        reset_fixture(0);
        (void)snapshot(packet, 1, 1);
        packet[8] = (char)count;
        wl16(packet + 10, 0); /* legacy snapshots use the same layout */
        dispatch(packet, SNAP_HEADER_SIZE + count * (1 + NET_DELTA_KEYFRAME_BODY));
        CHECK(s_lastRecvSnapSeq == 1 && g_netSessionFrame == 123);
        for (int i = 0; i < NET_MAX_PLAYERS; ++i) {
            CHECK(s_deltaRecv[i].valid == (i < count));
            int expected = i < count && i != g_localPlayerIndex ? 1000 + i * 100 : 9000 + i;
            CHECK(g_playerBase[i].posX == expected);
        }
        ++cases;
    }

    /* Arbitrary unique slot order is legal; simultaneous finishers retain
     * wire order, and the local player's finish bookkeeping stays local. */
    reset_fixture(0);
    (void)snapshot(packet, 1, 1);
    static const int order[] = {2, 0, 1, 3};
    int offset = SNAP_HEADER_SIZE;
    for (int i = 0; i < NET_MAX_PLAYERS; ++i) {
        int slot = order[i];
        source_players[slot].lapsCompleted = 3;
        offset += net_delta_encode(&send_states[slot], &source_players[slot], slot, 1,
                                    (unsigned char *)packet + offset, NET_MAX_PACKET - offset);
    }
    dispatch(packet, offset);
    CHECK(g_finishOrderCounter == 8);
    CHECK(g_playerBase[2].trackProgress == -6 && g_playerBase[0].trackProgress == -7);
    CHECK(g_playerBase[1].trackProgress == -8 && g_playerBase[3].trackProgress == 1234);
    ++cases;
}

static void test_valid(void)
{
    char packet[NET_MAX_PACKET];
    int len = prepare_snapshot(packet, 0);
    dispatch(packet, len);
    expect_snapshot_committed();
    ReceiveState before = capture();
    dispatch(packet, len); /* duplicate must not reapply a relative delta */
    expect_unchanged(&before);
    wl16(packet + 4, 9);
    dispatch(packet, len); /* reordered old snapshot */
    expect_unchanged(&before);
    ++cases;

    len = prepare_snapshot(packet, 1);
    s_lastRecvSnapSeq = 65534;
    wl16(packet + 4, 65535);
    dispatch(packet, len);
    CHECK(s_lastRecvSnapSeq == 65535);
    wl16(packet + 4, 0);
    dispatch(packet, len);
    CHECK(s_lastRecvSnapSeq == 0 && g_finishOrderCounter == 6);
    before = capture();
    wl16(packet + 4, 65535);
    dispatch(packet, len);
    expect_unchanged(&before);
    ++cases;

    len = prepare_input(packet, 0);
    dispatch(packet, len);
    expect_input_committed();
    before = capture();
    dispatch(packet, len);
    expect_unchanged(&before);
    ++cases;

    len = prepare_input(packet, 1);
    s_lastRecvInputSeq[1] = 65534;
    wl16(packet + 4, 65535);
    dispatch(packet, len);
    CHECK(s_lastRecvInputSeq[1] == 65535);
    wl16(packet + 4, 0);
    dispatch(packet, len);
    CHECK(s_lastRecvInputSeq[1] == 0 && g_finishOrderCounter == 6);
    before = capture();
    wl16(packet + 4, 65535);
    dispatch(packet, len);
    expect_unchanged(&before);
    ++cases;

    /* Keep the existing 12-byte input-only form and its bookkeeping. */
    (void)prepare_input(packet, 0);
    before = capture();
    dispatch(packet, 12);
    CHECK(s_lastRecvInputSeq[1] == 11 && s_netInputBuffer[1] == 0x5678);
    CHECK(s_hostSlotLastRecvMs[1] == now_ms && g_netPlayerRecvd[1] == 1);
    CHECK(memcmp(g_playerBase, before.players, sizeof(before.players)) == 0);
    CHECK(memcmp(s_deltaRecv, before.history, sizeof(before.history)) == 0);
    CHECK(memcmp(s_curr, before.curr, sizeof(before.curr)) == 0);
    CHECK(g_finishOrderCounter == before.finish_counter);
    ++cases;

    /* Preserve frame-only lobby handling for a fully validated snapshot. */
    len = prepare_snapshot(packet, 1);
    g_netGameStarted = 1;
    before = capture();
    dispatch(packet, len);
    CHECK(g_netSessionFrame == 123 && s_lastHostPacketMs == now_ms);
    CHECK(memcmp(g_playerBase, before.players, sizeof(before.players)) == 0);
    CHECK(memcmp(s_deltaRecv, before.history, sizeof(before.history)) == 0);
    CHECK(s_lastRecvSnapSeq == before.snapshot_seq);
    ++cases;
}

int main(int argc, char **argv)
{
    const char *which = argc > 1 ? argv[1] : "all";
    if (strcmp(which, "all") == 0 || strcmp(which, "snapshot") == 0) {
        test_snapshot_truncations();
        test_snapshot_structure();
    }
    if (strcmp(which, "all") == 0 || strcmp(which, "input") == 0) {
        test_input_truncations();
        test_input_structure();
    }
    if (strcmp(which, "all") == 0 || strcmp(which, "valid") == 0) {
        test_valid();
        test_valid_snapshot_shapes();
    }
    if (strcmp(which, "all") == 0 || strcmp(which, "snapshot-late") == 0)
        test_late_snapshot_failure();
    if (strcmp(which, "all") == 0 || strcmp(which, "decoder") == 0)
        test_failed_decode_interpolation();
    CHECK(cases != 0);
    printf("network receive: %d %s cases passed\n", cases, which);
    return 0;
}
