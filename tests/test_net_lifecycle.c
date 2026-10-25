#define SONICR_TEST_LIFECYCLE
#include "test_net_authority.c"
#undef main
#include "platform.h"
#include "r_state.h"
#include <setjmp.h>

int NetworkScreen(void);
int NetworkScreenReentry(void);
int CreateNetworkSession(const char *, const char *);
extern const char *g_cmdHostIP;
extern int g_netSavedCharId, g_netSavedTrackIdx, g_netSavedModeIdx;
static int menu_frames, menu_exit, menu_role, menu_active;
static net_socket_t menu_peer;
static int server_port;
static int stage;
static jmp_buf menu_checkpoint;
static unsigned char menu_models[32 * 0x44];
int g_saveBlock[554];
void *g_tpagePixelBuf[52];

DWORD lifecycle_time_ms(void) { return now_ms; }
int lifecycle_host_start(int port) { CHECK(port == NET_PORT_DEFAULT); return net_host_start(0); }
int lifecycle_client_connect(const char *ip, int port)
{ CHECK(port == NET_PORT_DEFAULT); return net_client_connect(ip, server_port); }
const char *MatchmakerGetUsername(void) { return "lifecycle_host"; }
void platform_pump_events(void) { }

/* Assets, graphics/audio and public services are excluded from the fixture. */
int platform_net_init(void) { return 0; }
void LoadTPageRGB(int page, const char *path) { (void)page; (void)path; }
void R_FreezeTexture(int page) { (void)page; }
int platform_get_region(void) { return NET_REGION_UNKNOWN; }
int MatchmakerInit(void) { return 0; }
int MatchmakerHasToken(void) { return 0; }
void MatchmakerClearToken(void) { }
void MatchmakerClearSession(void) { }
int MatchmakerListSessions(MatchmakerSessionList *out) { (void)out; CHECK(0); return 0; }
int MatchmakerKeepAlive(int count, const char *status) { (void)count; (void)status; CHECK(0); return 0; }
int64_t MatchmakerGetSessionId(void) { return -1; }
int ScanTypedLetter(void) { return -1; }
void ColorizeTpageHiColor(int r, int g, int b) { (void)r; (void)g; (void)b; }
void SetupMenuTexturesD3D(void) { }
void FinalizeMenuTexturesD3D(void) { }
void ProcessTpageStates(void) { }
void BeginFrame(void) { }
void EndFrame(void) { }
void RenderBackground(void) { }
void RenderFadeOverlay(void) { }
void RenderWavingMenuBackground(void) { }
void FlipD3D(void) { }
void UpdateFrameTimers(void) { }
void UpdateVertexLighting(Player *p) { (void)p; }
void AnimateVehicleGlow(Player *p) { (void)p; }
void RenderResultsScreen(int mode, int offset) { (void)mode; (void)offset; }
void RenderCharacterOnPodium(int x, int y, int z, int a, int b, int c, Player *p)
{ (void)x; (void)y; (void)z; (void)a; (void)b; (void)c; (void)p; }
void Draw3DModelD3D(int x, int y, int z, int a, int b, int c, intptr_t model, int flag)
{ (void)x; (void)y; (void)z; (void)a; (void)b; (void)c; (void)model; (void)flag; }
void DrawTexturedQuad(int x, int y, int z, int w, int h, int page,
                      int u, int v, int uw, int vh, unsigned color)
{ (void)x; (void)y; (void)z; (void)w; (void)h; (void)page; (void)u; (void)v; (void)uw; (void)vh; (void)color; }
int DrawGlyphString(int x, int y, int *ids, int count)
{ (void)y; (void)ids; (void)count; return x; }
void R_DrawQuad2DSolid(float x0, float y0, float x1, float y1, float z, uint32_t color)
{ (void)x0; (void)y0; (void)x1; (void)y1; (void)z; (void)color; }
void R_SetBlendMode(R_BlendMode mode) { (void)mode; }
void PlaySoundEffect(int id, int vol, int pan) { (void)id; (void)vol; (void)pan; }
void UpnpClosePort(int port) { (void)port; }
void PollAllInputDevices(void) { }
void UpdateCDPlayback(int track) { (void)track; }
void UpdateFade(void)
{
    if (g_fadeState == FADE_IN) { g_fadeState = 0; g_fadeLevel = 0; }
    else if (g_fadeState == FADE_OUT) g_fadeLevel = -256;
}

void ReadInput(void)
{
    CHECK(menu_active && ++menu_frames < 100);
    memset(g_diKeyboardState, 0, 256);
    g_inputBits = 0;
    if (stage == 0) {
        g_diKeyboardState[menu_role ? 0x3b : 0x3c] = 1;
        stage = 1;
    } else if (stage == 1 && net_is_active()) {
        char packet[NET_MAX_PACKET];
        struct sockaddr_in address = bound_address(s_socket);
        if (menu_role) {
            make_join(packet, "lifecycle_client");
            raw_send(menu_peer, &address, packet, 56);
        } else {
            int length = control_packet(packet, NET_MSG_SLOT_ASSIGN);
            raw_send(menu_peer, &address, packet, length);
        }
        for (int i = 0; i < 100; ++i) {
            pump();
            struct sockaddr_in peer_address = bound_address(menu_peer);
            int slot = find_slot(&peer_address);
            if (menu_role ? (slot > 0 && net_slot_is_connected(slot)) : s_haveSlotAssign) break;
            usleep(1000);
        }
        struct sockaddr_in peer_address = bound_address(menu_peer);
        int slot = find_slot(&peer_address);
        CHECK(menu_role ? (slot > 0 && net_slot_is_connected(slot)) : s_haveSlotAssign);
        stage = 2;
    } else if (stage == 2) {
        if (menu_exit == 0) now_ms += 181000;
        else if (menu_exit == 1) g_diKeyboardState[0x01] = 1;
        else if (menu_exit == 2) {
            char packet[NET_MAX_PACKET];
            struct sockaddr_in address = bound_address(s_socket);
            if (menu_role) {
                wl32(packet, NET_MSG_START_ACK); raw_send(menu_peer, &address, packet, 4);
                g_diKeyboardState[0x3b] = 1;
            } else {
                int length = control_packet(packet, NET_MSG_START_GAME);
                wl32(packet + 4, 2); raw_send(menu_peer, &address, packet, length);
            }
            stage = 3;
        }
    }
}

void WaitForFrameCap(void)
{
    now_ms += 34;
    if (s_threadRunning) usleep(1000);
    if (menu_exit == 3 && stage >= 2) longjmp(menu_checkpoint, 1);
}

static int invoke_menu(int reentry, int role, int exit_kind)
{
    g_cmdHostIP = "127.0.0.1";
    g_netLanOnly = 1; g_dpLobbyObject = 0;
    g_netSavedCharId = g_netSavedTrackIdx = g_netSavedModeIdx = 0;
    for (int i = 0; i < 10; ++i) g_animDataPtrs[i] = NULL;
    g_charAnimTables = NULL;
    g_objectStructArray = (void *)menu_models;
    menu_frames = 0; menu_exit = exit_kind; menu_role = role; stage = 0; menu_active = 1;
    int result = SCREEN_BACK;
    if (setjmp(menu_checkpoint) == 0) result = reentry ? NetworkScreenReentry() : NetworkScreen();
    menu_active = 0;
    return result;
}

static void prepare_menu(int role)
{
    real_mode = 1;
    CHECK(net_transport_init() == 0);
    reset_fixture(role);
    menu_peer = raw_peer();
    server_port = ntohs(bound_address(menu_peer).sin_port);
}

static void test_menu_exit(int reentry, int role, int exit_kind)
{
    prepare_menu(role);
    int result = invoke_menu(reentry, role, exit_kind);
    printf("lobby %s %s %s: result=%d active=%d peers=%d ready=0x%x\n",
           reentry ? "reentry" : "entry", role ? "host" : "client",
           exit_kind == 0 ? "timeout" : exit_kind == 1 ? "escape" : "handoff",
           result, net_is_active(), s_playerCount, s_levelReadyMask);
    if (exit_kind == 2) {
        CHECK(result == SCREEN_OK && net_is_active() && g_netSessionActive);
        CHECK(role ? (net_slot_is_connected(1) && g_netPlayerCount == 2) : s_haveSlotAssign);
    } else {
        CHECK(result == (exit_kind ? SCREEN_BACK : SCREEN_TITLE));
        CHECK(!net_is_active() && !socket_is_valid(s_socket) && !socket_is_valid(s_discoverSocket));
        CHECK(!s_threadRunning && s_playerCount == 0);
        if (!role) expect_raw_header(menu_peer, NET_MSG_LEAVE, 4);
    }
    CloseDirectPlaySession();
    NET_CLOSESOCKET(menu_peer); real_mode = 0; ++cases;
}

static void test_recreation(int reentry)
{
    prepare_menu(1);
    CHECK(invoke_menu(reentry, 1, 0) == SCREEN_TITLE);
    net_socket_t departed = menu_peer;
    menu_peer = raw_peer();
    (void)invoke_menu(reentry, 1, 3);
    struct sockaddr_in peer_address = bound_address(menu_peer);
    int slot = find_slot(&peer_address);
    int expected = 1;
    for (int i = 1; i < NET_MAX_PLAYERS; ++i) if (net_slot_is_connected(i)) expected |= 1 << i;
    printf("recreation: new client slot=%d players=%d expected=0x%x actual=0x%x\n",
           slot, g_netPlayerCount, expected, 1 | (1 << slot));
    CHECK(slot == 1 && g_netPlayerCount == 2 && expected == 3);
    CHECK(!strcmp(net_get_slot_name(0), "lifecycle_host") && !strcmp(net_get_slot_name(1), "lifecycle_client"));
    CHECK(!net_get_slot_name(2)[0] && !net_get_slot_name(3)[0]);
    CHECK(!s_startAckMask && !s_levelReadyMask && !s_levelGoReceived);
    char packet[4]; wl32(packet, NET_MSG_START_ACK);
    struct sockaddr_in address = bound_address(s_socket);
    raw_send(menu_peer, &address, packet, 4);
    InitNetworkGame(); CHECK(s_startAckMask == 3);
    wl32(packet, NET_MSG_LEVEL_READY); raw_send(menu_peer, &address, packet, 4);
    NetLevelSyncBarrier(); CHECK(s_levelReadyMask == 3);
    expect_raw_header(menu_peer, NET_MSG_LEVEL_GO, 4);
    CloseDirectPlaySession(); NET_CLOSESOCKET(menu_peer); NET_CLOSESOCKET(departed); real_mode = 0; ++cases;
}

static void test_close_reset(void)
{
    prepare_menu(1);
    (void)invoke_menu(0, 1, 3);
    s_startAckMask = s_levelReadyMask = 15; s_levelGoReceived = 1;
    g_netReadyFlag = 1; g_netReceivedLobbyData = 1; g_netLobbyPlayerSlot = 1;
    g_netSessionActive = g_netSessionAlive = 1;
    g_netExpectedPlayers = 4;
    for (int i = 0; i < NET_MAX_PLAYERS; ++i) g_netPlayerAlive[i] = 1;
    AuthorityState roster = capture_authority();
    net_socket_t old_socket = s_socket;
    s_discoverSocket = raw_peer();
    net_socket_t old_discovery = s_discoverSocket;
    uint64_t old_generation = s_playerGeneration[1];
    char join[56]; make_join(join, "obsolete");
    net_enqueue(join, sizeof(join), 1, s_playerGeneration[1]);
    CloseDirectPlaySession();
    printf("close reset: ack=0x%x ready=0x%x go=%d queued=%d\n",
           s_startAckMask, s_levelReadyMask, s_levelGoReceived, s_queueHead != s_queueTail);
    CHECK(!s_startAckMask && !s_levelReadyMask && !s_levelGoReceived);
    CHECK(s_queueHead == s_queueTail && !g_netReadyFlag && !g_netReceivedLobbyData && g_netLobbyPlayerSlot == 0xff);
    CHECK(!g_netSessionActive && !g_netSessionAlive);
    CHECK(!g_netExpectedPlayers);
    for (int i = 0; i < NET_MAX_PLAYERS; ++i) CHECK(!g_netPlayerAlive[i]);
    CHECK(!net_peer_is_current(1, old_generation));
    CHECK(g_netPlayerCount == roster.players);
    CHECK(!memcmp(g_playerBase, roster.receive.players, sizeof(roster.receive.players)));
    CHECK(!memcmp(s_netSlotNames, roster.names, sizeof(roster.names)));
    CHECK(!memcmp(g_netPlayerDecorations, roster.decorations, sizeof(roster.decorations)));
    struct sockaddr_in unused;
    socklen_t size = sizeof(unused);
    CHECK(getsockname(old_socket, (struct sockaddr *)&unused, &size) < 0);
    CHECK(getsockname(old_discovery, (struct sockaddr *)&unused, &size) < 0);
    /* Teardown is idempotent; it must not destroy the results roster. */
    CloseDirectPlaySession();
    CHECK(g_netPlayerCount == roster.players && !memcmp(s_netSlotNames, roster.names, sizeof(roster.names)));
    NET_CLOSESOCKET(menu_peer); real_mode = 0; ++cases;
}

static void test_abandoned_queue(int reentry)
{
    prepare_menu(1);
    (void)invoke_menu(reentry, 1, 3);
    uint64_t old_generation = s_playerGeneration[1];
    net_set_slot_name(3, "obsolete_roster");
    char join[56]; make_join(join, "obsolete_join");
    net_enqueue(join, sizeof(join), 1, old_generation);
    /* Enter another lobby without relying on the previous caller to close it.
     * Its entry must retire the old endpoint and its queued data itself. */
    (void)invoke_menu(reentry, 1, 3);
    CHECK(g_netPlayerCount == 2 && !net_get_slot_name(3)[0]);
    CHECK(!net_peer_is_current(1, old_generation));
    AuthorityState before = capture_authority();
    net_enqueue(join, sizeof(join), 1, old_generation);
    pump(); unchanged(&before, "prior-session JOIN changed recreated lobby");
    CloseDirectPlaySession(); NET_CLOSESOCKET(menu_peer); real_mode = 0;
}

static void test_resource_cycles(void)
{
    /* Warm up the platform and thread runtime before counting OS handles. */
    test_menu_exit(0, 1, 1);
#ifdef _WIN32
    DWORD initial_handles, final_handles;
    CHECK(GetProcessHandleCount(GetCurrentProcess(), &initial_handles));
#endif
    for (int cycle = 0; cycle < 12; ++cycle) {
        test_menu_exit(cycle & 1, 1, cycle & 1);
        CHECK(!s_threadRunning && !s_playerCount && s_queueHead == s_queueTail);
        for (int i = 0; i < NET_MAX_PLAYERS; ++i) CHECK(!net_slot_is_connected(i));
    }
#ifdef _WIN32
    CHECK(GetProcessHandleCount(GetCurrentProcess(), &final_handles));
    printf("resource cycles: handle delta=%ld\n", (long)final_handles - (long)initial_handles);
    CHECK(final_handles == initial_handles);
#endif
}

int main(int argc, char **argv)
{
    const char *which = argc > 1 ? argv[1] : "all";
    setvbuf(stdout, NULL, _IONBF, 0);
    if (!strcmp(which, "all") || !strcmp(which, "recreate"))
        for (int cycle = 0; cycle < 8; ++cycle) test_recreation(cycle & 1);
    if (!strcmp(which, "all") || !strcmp(which, "reset")) test_close_reset();
    if (!strcmp(which, "all") || !strcmp(which, "queue"))
        for (int reentry = 0; reentry <= 1; ++reentry) test_abandoned_queue(reentry);
    if (!strcmp(which, "all") || !strcmp(which, "resources")) test_resource_cycles();
    for (int reentry = 0; reentry <= 1; ++reentry)
        for (int role = 0; role <= 1; ++role)
            for (int kind = 0; kind < 3; ++kind)
                if (!strcmp(which, "all") || !strcmp(which, kind == 0 ? "timeout" : kind == 1 ? "escape" : "handoff"))
                    test_menu_exit(reentry, role, kind);
    CHECK(cases != 0);
    printf("network lifecycle: %d %s cases passed\n", cases, which);
    return 0;
}
