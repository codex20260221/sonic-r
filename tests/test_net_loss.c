/* Reuse the asset-free transport/clock fixture and real packet dispatcher.
 * The renamed entry point keeps every original regression compiled, unchanged. */
#define main transactional_receive_tests
#include "test_net_receive.c"
#undef main

enum { TRACE_LENGTH = 96, PERIOD = KEYFRAME_INTERVAL + 1 };
typedef struct {
    char data[NET_MAX_PACKET];
    int len, tick, slot, keyframe;
    uint16_t seq;
    NetDeltaRecvState expected[NET_MAX_PLAYERS];
} WirePacket;
static WirePacket traces[NET_MAX_PLAYERS][TRACE_LENGTH];
static int deliveries, scenarios, profile, unsafe_accepts, wrong_states;
static unsigned long long wire_hash = 14695981039346656037ULL;

static void source_at(int tick)
{
    for (int slot = 0; slot < NET_MAX_PLAYERS; ++slot) {
        Player *pl = &g_playerBase[slot];
        memset(pl, 0, sizeof(*pl));
        pl->posX = 1000 + slot * 10000 + tick * 100;
        pl->posY = 2000 + tick * 17;
        pl->posZ = -3000 - tick * 31;
        pl->velX = 100 + tick;
        pl->angleYaw = (tick * 13) & 0xfff;
        pl->ringCount = (short)(tick % 100);
        pl->lapsCompleted = 2;
        pl->itemEffectState = -1;
    }
}

/* Snapshots use the actual production sender, including its countdown.
 * Inputs use the same production codec and unchanged 12-byte input envelope;
 * their independent per-client sequence/cadence is generated explicitly. */
static void make_trace(int input, int slot, uint16_t first_seq)
{
    NetDeltaRecvState golden[NET_MAX_PLAYERS] = {0};
    reset_fixture(input);
    s_snapshotSeq = first_seq;
    NetDeltaSendState input_sender;
    net_delta_reset_send(&input_sender);
    for (int tick = 0; tick < TRACE_LENGTH; ++tick) {
        WirePacket *p = &traces[input ? slot : 0][tick];
        memset(p, 0, sizeof(*p));
        source_at(tick);
        p->tick = tick;
        p->slot = input ? slot : -1;
        p->seq = (uint16_t)(first_seq + tick);
        if (input) {
            p->keyframe = tick % PERIOD == 0;
            wl32(p->data, NET_MSG_CLIENT_INPUT);
            wl16(p->data + 4, p->seq);
            wl16(p->data + 6, (uint16_t)(tick + 1));
            p->data[8] = (char)slot;
            p->data[9] = (char)p->keyframe;
            wl16(p->data + 10, 1);
            p->len = 12 + net_delta_encode(&input_sender, &g_playerBase[slot], slot,
                            p->keyframe, (unsigned char *)p->data + 12, NET_MAX_PACKET - 12);
        } else {
            g_netSessionFrame = tick;
            p->len = net_build_snapshot(p->data, sizeof(p->data));
            p->keyframe = p->data[9] & 1;
            CHECK(p->keyframe == (tick % PERIOD == 0));
        }
        CHECK(p->len > 12);
        for (int b = 0; b < p->len; ++b) {
            wire_hash ^= (unsigned char)p->data[b];
            wire_hash *= 1099511628211ULL;
        }
        int offset = 12;
        int count = input ? 1 : NET_MAX_PLAYERS;
        for (int i = 0; i < count; ++i) {
            int idx = (unsigned char)p->data[offset];
            int consumed = net_delta_decode(&golden[idx], (unsigned char *)p->data + offset,
                                            p->len - offset, p->keyframe, NULL);
            CHECK(consumed > 0);
            offset += consumed;
        }
        CHECK(offset == p->len);
        memcpy(p->expected, golden, sizeof(golden));
    }
    reset_fixture(input);
    if (input)
        s_lastRecvInputSeq[slot] = (uint16_t)(first_seq - 1);
    else
        s_lastRecvSnapSeq = (uint16_t)(first_seq - 1);
}

typedef struct {
    int last_tick, last_actual_tick, dropped, accepted, rejected;
    int unsafe, wrong, max_error, max_update_gap;
    int last_bad_tick, max_recovery_wait;
} Stream;

static void deliver(WirePacket *p, Stream *s, int malformed)
{
    ReceiveState before = capture();
    int accept = !malformed && p->tick > s->last_tick &&
                 (p->keyframe || p->tick == s->last_tick + 1);
    incoming_slot = p->slot < 0 ? 0 : p->slot;
    now_ms += 10;
    dispatch(p->data, p->len - malformed);
    uint16_t actual_seq = p->slot < 0 ? s_lastRecvSnapSeq : s_lastRecvInputSeq[p->slot];
    uint16_t old_seq = p->slot < 0 ? before.snapshot_seq : before.input_seq[p->slot];
    int actual_accept = actual_seq == p->seq && old_seq != actual_seq;
    ++deliveries;
    if (actual_accept) {
        ++s->accepted;
        int gap = p->tick - s->last_actual_tick;
        if (s->last_actual_tick >= 0 && gap > s->max_update_gap) s->max_update_gap = gap;
        s->last_actual_tick = p->tick;
        if (!accept) ++s->unsafe;
        int wrong = 0;
        for (int slot = 0; slot < NET_MAX_PLAYERS; ++slot) {
            if (p->slot >= 0 && slot != p->slot) continue;
            if (memcmp(&s_deltaRecv[slot], &p->expected[slot], sizeof(s_deltaRecv[slot]))) wrong = 1;
            int error = abs(s_deltaRecv[slot].state[0] - p->expected[slot].state[0]);
            if (error > s->max_error) s->max_error = error;
            if (!profile) {
                CHECK(memcmp(&s_deltaRecv[slot], &p->expected[slot], sizeof(s_deltaRecv[slot])) == 0);
                if (slot != g_localPlayerIndex) {
                    CHECK(g_playerBase[slot].posX == p->expected[slot].state[0]);
                    CHECK(s_curr[slot].posX == p->expected[slot].state[0]);
                }
            }
        }
        s->wrong += wrong;
    } else {
        ++s->rejected;
    }
    if (!profile) {
        if (!accept) expect_unchanged(&before);
        CHECK(actual_accept == accept);
        CHECK(g_finishOrderCounter == before.finish_counter);
        CHECK(memcmp(&g_playerBase[g_localPlayerIndex], &before.players[g_localPlayerIndex],
                     sizeof(Player)) == 0);
        if (p->slot >= 0) {
            CHECK(s_lastRecvSnapSeq == before.snapshot_seq);
            for (int other = 1; other < NET_MAX_PLAYERS; ++other) {
                if (other == p->slot) continue;
                CHECK(s_lastRecvInputSeq[other] == before.input_seq[other]);
                CHECK(memcmp(&s_deltaRecv[other], &before.history[other], sizeof(s_deltaRecv[other])) == 0);
                CHECK(memcmp(&g_playerBase[other], &before.players[other], sizeof(Player)) == 0);
                CHECK(s_hostSlotLastRecvMs[other] == before.slot_times[other]);
            }
        } else {
            CHECK(memcmp(s_lastRecvInputSeq, before.input_seq, sizeof(before.input_seq)) == 0);
        }
    }
    if (accept) s->last_tick = p->tick;
    if (accept && p->keyframe && s->last_bad_tick >= 0) {
        int wait = p->tick - s->last_bad_tick;
        if (wait > s->max_recovery_wait) s->max_recovery_wait = wait;
        s->last_bad_tick = -1;
    }
}

static void run_scenario(int input, int pattern, uint16_t first_seq)
{
    static const char *names[] = {"isolated", "burst-5", "duplicates", "reorder",
        "reorder-retransmit", "stale", "malformed-delta", "initial-keyframe-loss",
        "periodic-keyframe-loss", "burst-including-keyframe", "malformed-keyframe",
        "two-keyframes-lost"};
    static const int update_gaps[] = {31, 31, 1, 30, 1, 1, 31, 1, 32, 62, 32, 63};
    make_trace(input, 1, first_seq);
    WirePacket *packets = traces[input ? 1 : 0];
    Stream s = {.last_tick = -1, .last_actual_tick = -1, .last_bad_tick = -1};
    for (int tick = 0; tick < TRACE_LENGTH; ++tick) {
        if ((pattern == 0 && tick == 1) ||
            (pattern == 1 && tick >= 1 && tick <= 5) ||
            (pattern == 7 && tick == 0) ||
            (pattern == 8 && tick == PERIOD) ||
            (pattern == 9 && tick >= 1 && tick <= PERIOD + 1) ||
            (pattern == 11 && (tick == PERIOD || tick == 2 * PERIOD))) {
            ++s.dropped;
            s.last_bad_tick = tick;
            continue;
        }
        if ((pattern == 3 || pattern == 4) && tick == 1) {
            deliver(&packets[2], &s, 0);
            deliver(&packets[1], &s, 0);
            if (pattern == 4) deliver(&packets[2], &s, 0);
            ++tick;
            continue;
        }
        int malformed = (pattern == 6 && tick == 1) || (pattern == 10 && tick == PERIOD);
        if (malformed) s.last_bad_tick = tick;
        deliver(&packets[tick], &s, malformed);
        if (pattern == 2) deliver(&packets[tick], &s, 0);
        if (pattern == 5 && tick == 40) {
            deliver(&packets[0], &s, 0);
            deliver(&packets[1], &s, 0);
            deliver(&packets[PERIOD], &s, 0);
        }
    }
    CHECK(s.last_tick == TRACE_LENGTH - 1);
    if (!profile) {
        CHECK(s.unsafe == 0 && s.wrong == 0);
        CHECK(s.max_update_gap == update_gaps[pattern]);
        CHECK(s.max_recovery_wait <= PERIOD);
    }
    unsafe_accepts += s.unsafe;
    wrong_states += s.wrong;
    ++scenarios;
    printf("%s %-24s start=%u: dropped=%d accepted=%d rejected=%d unsafe=%d wrong=%d "
           "max-error=%d max-update-gap=%d recovery-after-last-loss=%d ticks\n",
           input ? "input" : "snapshot", names[pattern],
           (unsigned)first_seq, s.dropped, s.accepted, s.rejected, s.unsafe, s.wrong,
           s.max_error, s.max_update_gap, s.max_recovery_wait);
}

static void test_independent_clients(void)
{
    for (int slot = 1; slot < NET_MAX_PLAYERS; ++slot)
        make_trace(1, slot, (uint16_t)(slot * 100 + 1));
    reset_fixture(1);
    s_lastRecvSnapSeq = 456;
    Stream streams[NET_MAX_PLAYERS] = {0};
    for (int slot = 1; slot < NET_MAX_PLAYERS; ++slot)
        streams[slot].last_tick = streams[slot].last_actual_tick = -1;
    for (int tick = 0; tick < TRACE_LENGTH; ++tick) {
        for (int slot = 1; slot < NET_MAX_PLAYERS; ++slot) {
            if (slot == 1 && tick == 1) continue;
            deliver(&traces[slot][tick], &streams[slot], 0);
        }
    }
    CHECK(s_lastRecvSnapSeq == 456);
    CHECK(streams[2].accepted == TRACE_LENGTH && streams[3].accepted == TRACE_LENGTH);
    CHECK(streams[1].last_tick == TRACE_LENGTH - 1);
    ++scenarios;
}

static void test_every_loss_phase(void)
{
    for (int input = 0; input <= 1; ++input) {
        for (int phase = 1; phase <= PERIOD; ++phase) {
            make_trace(input, 1, 1);
            Stream s = {.last_tick = -1, .last_actual_tick = -1, .last_bad_tick = -1};
            WirePacket *packets = traces[input ? 1 : 0];
            for (int tick = 0; tick < TRACE_LENGTH; ++tick) {
                if (tick == phase) { s.last_bad_tick = tick; continue; }
                deliver(&packets[tick], &s, 0);
            }
            CHECK(s.last_tick == TRACE_LENGTH - 1 && s.max_recovery_wait <= PERIOD);
            CHECK(s.max_update_gap <= PERIOD + 1);
            ++scenarios;
        }
    }
    puts("all 31 isolated-loss phases: snapshot + input PASS");
}

static void edge_dispatch(const char *packet, int len)
{
    now_ms += 10;
    dispatch(packet, len);
    ++deliveries;
}

static void test_input_only_bridge(void)
{
    char packet[NET_MAX_PACKET], input_only[12];
    for (int gap = 0; gap <= 1; ++gap) {
        reset_fixture(1);
        int len = client_input(packet, 1, 10);
        edge_dispatch(packet, len);
        if (gap) {
            source_players[1].posX += 100;
            (void)client_input(packet, 0, 11); /* dropped state packet */
        }
        memcpy(input_only, packet, sizeof(input_only));
        input_only[9] = 0;
        wl16(input_only + 4, (uint16_t)(gap ? 12 : 11));
        ReceiveState before = capture();
        edge_dispatch(input_only, sizeof(input_only));
        CHECK(memcmp(s_deltaRecv, before.history, sizeof(before.history)) == 0);
        CHECK(memcmp(g_playerBase, before.players, sizeof(before.players)) == 0);
        source_players[1].posX += 100;
        len = client_input(packet, 0, (uint16_t)(gap ? 13 : 12));
        before = capture();
        edge_dispatch(packet, len);
        if (gap) expect_unchanged(&before);
        else CHECK(g_playerBase[1].posX == source_players[1].posX);

        len = client_input(packet, 1, 31);
        before = capture();
        edge_dispatch(packet, len - 1);
        expect_unchanged(&before);
        edge_dispatch(packet, len);
        CHECK(g_playerBase[1].posX == source_players[1].posX);
        source_players[1].posX += 100;
        len = client_input(packet, 0, 32);
        edge_dispatch(packet, len);
        CHECK(g_playerBase[1].posX == source_players[1].posX);
        ++scenarios;
    }
}

static void test_partial_keyframe_recovery(void)
{
    for (int count = 0; count < NET_MAX_PLAYERS; ++count) {
        make_trace(0, 1, 1);
        WirePacket *p = traces[0];
        edge_dispatch(p[0].data, p[0].len);
        /* Skip a predecessor, then recover only a subset of slots. */
        WirePacket partial = p[PERIOD];
        partial.data[8] = (char)count;
        partial.len = 12 + count * (1 + NET_DELTA_KEYFRAME_BODY);
        ReceiveState before = capture();
        edge_dispatch(partial.data, partial.len);
        for (int slot = count; slot < NET_MAX_PLAYERS; ++slot) {
            CHECK(!s_deltaRecv[slot].valid);
            CHECK(memcmp(&g_playerBase[slot], &before.players[slot], sizeof(Player)) == 0);
        }
        before = capture();
        edge_dispatch(p[PERIOD + 1].data, p[PERIOD + 1].len);
        expect_unchanged(&before);
        edge_dispatch(p[2 * PERIOD].data, p[2 * PERIOD].len);
        CHECK(memcmp(s_deltaRecv, p[2 * PERIOD].expected, sizeof(s_deltaRecv)) == 0);
        ++scenarios;
    }
}

int main(int argc, char **argv)
{
    const char *which = argc > 1 ? argv[1] : "all";
    profile = strcmp(which, "profile") == 0;
    int all = strcmp(which, "all") == 0 || profile;
    for (int input = 0; input <= 1; ++input) {
        if (!all && strcmp(which, input ? "input" : "snapshot") != 0) continue;
        for (int pattern = 0; pattern < 12; ++pattern) run_scenario(input, pattern, 1);
        run_scenario(input, 0, 65520);
    }
    if (!profile && (all || strcmp(which, "edges") == 0)) {
        test_independent_clients();
        test_every_loss_phase();
        test_input_only_bridge();
        test_partial_keyframe_recovery();
    }
    CHECK(scenarios != 0);
    printf("network loss: %d scenarios, %d deliveries, unsafe=%d wrong=%d, keyframe-period=%d, "
           "wire-fingerprint=%016llx\n", scenarios, deliveries, unsafe_accepts, wrong_states, PERIOD, wire_hash);
    return unsafe_accepts || wrong_states ? 1 : 0;
}
