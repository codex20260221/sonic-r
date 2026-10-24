/* Asset-free regression tests for the production network interpolator. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "sonicr_types.h"
#include "sonicr_globals.h"
#include "net/net_interp.h"

static Player players[MAX_PLAYERS];
Player *g_playerBase = players;
int g_netSessionActive;
int g_localPlayerIndex;

static uint32_t now_ms;
static int ground_calls[MAX_PLAYERS];

uint32_t platform_get_time_ms(void)
{
    return now_ms;
}

void DeriveGroundState(Player *player)
{
    ++ground_calls[player - players];
}

static void check_int(int actual, int expected, const char *expression, int line)
{
    if (actual != expected) {
        fprintf(stderr, "line %d: %s: expected %d, got %d\n",
                line, expression, expected, actual);
        exit(1);
    }
}

#define CHECK_EQ(actual, expected) \
    check_int((actual), (expected), #actual, __LINE__)

static void reset_fixture(void)
{
    NetInterpReset();
    memset(players, 0, sizeof(players));
    memset(ground_calls, 0, sizeof(ground_calls));
    now_ms = 0;
    g_netSessionActive = 1;
    g_localPlayerIndex = 0;
}

static void record_x(int slot, uint32_t time_ms, int x)
{
    now_ms = time_ms;
    players[slot].posX = x;
    NetInterpRecord(slot);
}

static void test_local_ownership_after_slot_change(void)
{
    reset_fixture();
    g_localPlayerIndex = 1;
    record_x(2, 100, 1000);
    record_x(2, 140, 1200);
    now_ms = 180;
    NetInterpApply();
    CHECK_EQ(players[2].posX, 1200);

    /* Join-order changes can make a previously remote slot local. Even if
     * old history remains, applying it must never overwrite local physics. */
    g_netSessionActive = 0;
    g_localPlayerIndex = 2;
    now_ms = 10000;
    players[2].posX = 5000;
    players[2].angleYaw = 1234;
    memset(ground_calls, 0, sizeof(ground_calls));
    g_netSessionActive = 1;
    NetInterpApply();
    CHECK_EQ(players[2].posX, 5000);
    CHECK_EQ(players[2].angleYaw, 1234);
    CHECK_EQ(ground_calls[2], 0);

    for (int frame = 1; frame <= 3; ++frame) {
        now_ms += 40;
        players[2].posX += 100;
        NetInterpApply();
        CHECK_EQ(players[2].posX, 5000 + frame * 100);
        CHECK_EQ(ground_calls[2], 0);
    }
}

static void test_reset_between_races(void)
{
    reset_fixture();
    record_x(1, 100, 1000);
    record_x(1, 140, 1200);
    record_x(2, 100, 2000);
    record_x(2, 140, 2400);

    NetInterpReset();
    g_localPlayerIndex = 1;
    players[1].posX = 7000;
    players[2].posX = 9000;
    now_ms = 10000;
    NetInterpApply();
    CHECK_EQ(players[1].posX, 7000);
    CHECK_EQ(players[2].posX, 9000);
    CHECK_EQ(ground_calls[1], 0);
    CHECK_EQ(ground_calls[2], 0);

    /* The first new sample must not blend with the previous race. */
    NetInterpRecord(2);
    now_ms = 10020;
    NetInterpApply();
    CHECK_EQ(players[2].posX, 9000);
    CHECK_EQ(ground_calls[2], 1);

    record_x(2, 10040, 9400);
    now_ms = 10060;
    NetInterpApply();
    CHECK_EQ(players[2].posX, 9200);
    CHECK_EQ(ground_calls[2], 2);
}

static void test_remote_interpolation_and_angle_wrap(void)
{
    reset_fixture();
    players[0].posX = 777;
    now_ms = 100;
    players[1].posX = 1000;
    players[1].posY = 2000;
    players[1].posZ = -3000;
    players[1].angleYaw = 0xFFE;
    players[1].anglePitch = 2;
    players[1].angleRoll = 0xFFA;
    players[1].pitchCombo = 0xFFF;
    NetInterpRecord(1);

    now_ms = 140;
    players[1].posX = 1400;
    players[1].posY = 2400;
    players[1].posZ = -2600;
    players[1].angleYaw = 2;
    players[1].anglePitch = 0xFFE;
    players[1].angleRoll = 6;
    players[1].pitchCombo = 1;
    players[1].ringCount = 9;
    players[1].velX = 12345;
    NetInterpRecord(1);

    now_ms = 160;
    NetInterpApply();
    CHECK_EQ(players[1].posX, 1200);
    CHECK_EQ(players[1].posY, 2200);
    CHECK_EQ(players[1].posZ, -2800);
    CHECK_EQ(players[1].angleYaw, 0);
    CHECK_EQ(players[1].anglePitch, 0);
    CHECK_EQ(players[1].angleRoll, 0);
    CHECK_EQ(players[1].pitchCombo, 0);
    CHECK_EQ(players[1].ringCount, 9);
    CHECK_EQ(players[1].velX, 12345);
    CHECK_EQ(ground_calls[1], 1);
    CHECK_EQ(players[0].posX, 777);
    CHECK_EQ(ground_calls[0], 0);

    now_ms = 180;
    NetInterpApply();
    CHECK_EQ(players[1].posX, 1400);
    CHECK_EQ(players[1].angleYaw, 2);

    /* Preserve the existing half-interval extrapolation cap. */
    now_ms = 220;
    NetInterpApply();
    CHECK_EQ(players[1].posX, 1600);
    CHECK_EQ(players[1].angleYaw, 4);
}

static void test_inactive_session(void)
{
    reset_fixture();
    record_x(1, 100, 1000);
    record_x(1, 140, 1400);
    g_netSessionActive = 0;
    now_ms = 160;
    NetInterpApply();
    CHECK_EQ(players[1].posX, 1400);
    CHECK_EQ(ground_calls[1], 0);
}

static void test_repeated_sessions(void)
{
    reset_fixture();
    for (int session = 0; session < 6; ++session) {
        NetInterpReset();
        memset(ground_calls, 0, sizeof(ground_calls));
        g_localPlayerIndex = session % 3;
        int remote = (g_localPlayerIndex + 1) % 3;
        uint32_t start = 100 + (uint32_t)session * 200;
        players[g_localPlayerIndex].posX = 5000 + session;
        record_x(remote, start, 1000);
        record_x(remote, start + 40, 1400);
        now_ms = start + 60;
        NetInterpApply();
        CHECK_EQ(players[remote].posX, 1200);
        CHECK_EQ(ground_calls[remote], 1);
        CHECK_EQ(players[g_localPlayerIndex].posX, 5000 + session);
        CHECK_EQ(ground_calls[g_localPlayerIndex], 0);
        CHECK_EQ(ground_calls[(remote + 1) % 3], 0);
        g_netSessionActive = 0;
        NetInterpReset();
        g_netSessionActive = 1;
    }
}

int main(void)
{
    test_local_ownership_after_slot_change();
    test_reset_between_races();
    test_remote_interpolation_and_angle_wrap();
    test_inactive_session();
    test_repeated_sessions();
    puts("network interpolation: 5 tests passed");
    return 0;
}
