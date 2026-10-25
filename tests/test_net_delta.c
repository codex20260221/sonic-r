/* Asset-free tests linked to the production delta codec. */
#include "net/net_delta.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int cases;
#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "line %d: %s\n", __LINE__, #condition); exit(1); \
} } while (0)

static void test_truncated(int keyframe)
{
    Player source = {0}, destination, original_player;
    NetDeltaSendState send;
    NetDeltaRecvState recv, original_recv;
    unsigned char packet[256];
    net_delta_reset_send(&send);
    net_delta_reset_recv(&recv);
    source.posX = 1000;
    source.posY = 2000;
    int len = net_delta_encode(&send, &source, 1, 1, packet, sizeof(packet));
    memset(&destination, 0x5a, sizeof(destination));
    CHECK(net_delta_decode(&recv, packet, len, 1, &destination) == len);
    source.posX += 100;
    source.posY -= 100;
    source.ringCount = 7;
    source.lap3Time = 123456;
    len = net_delta_encode(&send, &source, 1, keyframe, packet, sizeof(packet));
    original_recv = recv;
    original_player = destination;
    for (int truncated = 0; truncated < len; ++truncated) {
        recv = original_recv;
        destination = original_player;
        CHECK(net_delta_decode(&recv, packet, truncated, keyframe, &destination) == 0);
        if (memcmp(&recv, &original_recv, sizeof(recv)) != 0) {
            fprintf(stderr, "truncated %s changed history at %d/%d bytes\n",
                    keyframe ? "keyframe" : "delta", truncated, len);
            exit(1);
        }
        CHECK(memcmp(&destination, &original_player, sizeof(destination)) == 0);
        /* A retransmission must still decode against the unmodified predecessor. */
        CHECK(net_delta_decode(&recv, packet, len, keyframe, &destination) == len);
        CHECK(destination.posX == 1100 && destination.posY == 1900);
        CHECK(destination.ringCount == 7 && destination.lap3Time == 123456);
        ++cases;
    }
}

static void test_valid_wire(void)
{
    Player source = {0}, destination = {0};
    NetDeltaSendState send;
    NetDeltaRecvState recv;
    unsigned char packet[256], expected[1 + NET_DELTA_KEYFRAME_BODY] = {0};
    net_delta_reset_send(&send);
    net_delta_reset_recv(&recv);
    source.posX = 1000;
    source.posY = 2000;
    expected[0] = 1;
    expected[1] = 0xe8; expected[2] = 3;
    expected[5] = 0xd0; expected[6] = 7;
    int len = net_delta_encode(&send, &source, 1, 1, packet, sizeof(packet));
    CHECK(len == sizeof(expected) && memcmp(packet, expected, sizeof(expected)) == 0);
    CHECK(net_delta_decode(&recv, expected, sizeof(expected), 1, &destination) == len);
    CHECK(destination.posX == 1000 && destination.posY == 2000 && recv.valid);
    ++cases;

    static const unsigned char delta[] = {1, 3, 0, 0, 0, 0, 0, 100, 0, 156, 255};
    source.posX = 1100;
    source.posY = 1900;
    len = net_delta_encode(&send, &source, 1, 0, packet, sizeof(packet));
    CHECK(len == sizeof(delta) && memcmp(packet, delta, sizeof(delta)) == 0);
    CHECK(net_delta_decode(&recv, delta, sizeof(delta), 0, &destination) == len);
    CHECK(destination.posX == 1100 && destination.posY == 1900);
    ++cases;

    static const unsigned char overflow[] = {
        1, 1, 0, 0, 0, 0, 0x10, 1, 0, 0xa0, 0x86, 1, 0
    };
    source.posX = 100000;
    len = net_delta_encode(&send, &source, 1, 0, packet, sizeof(packet));
    CHECK(len == sizeof(overflow) && memcmp(packet, overflow, sizeof(overflow)) == 0);
    CHECK(net_delta_decode(&recv, overflow, sizeof(overflow), 0, &destination) == len);
    CHECK(destination.posX == 100000 && destination.posY == 1900);
    ++cases;

    /* A delta without a baseline must leave both outputs intact. */
    net_delta_reset_recv(&recv);
    NetDeltaRecvState original = recv;
    Player original_player = destination;
    CHECK(net_delta_decode(&recv, delta, sizeof(delta), 0, &destination) == 0);
    CHECK(memcmp(&recv, &original, sizeof(recv)) == 0);
    CHECK(memcmp(&destination, &original_player, sizeof(destination)) == 0);
    ++cases;
}

static void test_overflow_truncation(void)
{
    Player source = {0}, destination = {0};
    NetDeltaSendState send;
    NetDeltaRecvState recv;
    unsigned char packet[256];
    net_delta_reset_send(&send);
    net_delta_reset_recv(&recv);
    int len = net_delta_encode(&send, &source, 2, 1, packet, sizeof(packet));
    CHECK(net_delta_decode(&recv, packet, len, 1, &destination) == len);
    source.posX = 100000;
    source.angleYaw = -1000;
    source.yOffset = -100000;
    source.itemEffectState = -1;
    len = net_delta_encode(&send, &source, 2, 0, packet, sizeof(packet));
    NetDeltaRecvState original = recv;
    Player original_player = destination;
    for (int cut = 0; cut < len; ++cut) {
        recv = original;
        destination = original_player;
        CHECK(net_delta_decode(&recv, packet, cut, 0, &destination) == 0);
        CHECK(memcmp(&recv, &original, sizeof(recv)) == 0);
        CHECK(memcmp(&destination, &original_player, sizeof(destination)) == 0);
        ++cases;
    }
    CHECK(net_delta_decode(&recv, packet, len, 0, &destination) == len);
    CHECK(destination.posX == 100000 && destination.angleYaw == -1000);
    CHECK(destination.yOffset == -100000 && destination.itemEffectState == -1);
    ++cases;
}

static void test_staging(void)
{
    Player source = {0}, direct, staged;
    NetDeltaSendState send;
    NetDeltaRecvState recv, staged_recv;
    unsigned char packet[256];
    net_delta_reset_send(&send);
    net_delta_reset_recv(&recv);
    net_delta_reset_recv(&staged_recv);
    memset(&direct, 0x5a, sizeof(direct));
    staged = direct;
    source.posX = -12345;
    source.angleYaw = -1000;
    source.itemEffectState = -1;
    for (int keyframe = 1; keyframe >= 0; --keyframe) {
        source.posX += 100;
        source.ringCount = (short)(7 + keyframe);
        int len = net_delta_encode(&send, &source, 1, keyframe, packet, sizeof(packet));
        NetDeltaRecvState before = staged_recv;
        CHECK(net_delta_decode(&recv, packet, len, keyframe, &direct) == len);
        CHECK(net_delta_decode(&staged_recv, packet, len, keyframe, NULL) == len);
        CHECK(memcmp(&recv, &staged_recv, sizeof(recv)) == 0);
        CHECK(net_delta_decode(&before, packet, len, keyframe, &staged) == len);
        CHECK(memcmp(&direct, &staged, sizeof(direct)) == 0);
        CHECK(staged.charId == 0x5a5a && staged.posX == source.posX);
        ++cases;
    }
}

int main(int argc, char **argv)
{
    const char *which = argc > 1 ? argv[1] : "all";
    if (strcmp(which, "all") == 0 || strcmp(which, "atomic") == 0) {
        test_truncated(0);
        test_truncated(1);
        test_overflow_truncation();
    }
    if (strcmp(which, "all") == 0 || strcmp(which, "valid") == 0)
        test_valid_wire();
    if (strcmp(which, "all") == 0)
        test_staging();
    CHECK(cases != 0);
    printf("network delta: %d %s cases passed\n", cases, which);
    return 0;
}
