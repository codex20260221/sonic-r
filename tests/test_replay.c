/* Asset-free regressions linked to the production LoadReplayLog in save.c. */
#ifndef SONICR_REPLAY_TEST_IO
#error "Use tests/run_replay.py to compile the production loader with I/O instrumentation."
#endif
/* Only the production translation unit uses the forwarding/fault wrappers. */
#undef malloc
#undef free
#undef fread
#undef ferror
#undef fclose

#include "sonicr_globals.h"
#include "sonicr_functions.h"

#include <stdint.h>

enum { HEADER_WORDS = 15, HEADER_BYTES = HEADER_WORDS * 2, CAPACITY = 0x8000 };
static const char *fixture_path = "test-replay.dem";
static Player players[MAX_PLAYERS], expected_players[MAX_PLAYERS];
Player *g_playerBase = players;
FILE *g_fileHandle;
unsigned short g_taGhostSource[CAPACITY];
int g_replayCharIds[MAX_PLAYERS];
int g_gpDifficultyLevel, g_difficultyConfig, g_numViewports, g_numPlayers;
int g_numHumans, g_trackId, g_raceType, g_raceSubMode, g_weatherType;
int g_timeOfDay, g_ghostMaxFrames;

/* Other save.c entry points are unused. MSVC requires their references to
 * resolve even with /Gy /OPT:REF; GNU/Apple linkers discard those sections. */
int g_raceSpeedMult, g_screenWidth, g_screenHeight;
int g_ghostToggle, g_weatherConfig, g_catchUpToggle, g_guideToggle;
int g_minimapConfig, g_splitScreenMode, g_optResLow, g_optResHigh;
int g_optCfg_468, g_optCfg_46c, g_optCfg_470, g_renderQuality, g_softDoubleBuf;
int g_optCfg_47c, g_qualityLevel, g_resolutionLevel, g_optCfg_488, g_optCfg_48c;
int g_doubleWidthFlag, g_stereoEnabled, g_vocalsEnabled, g_optSfxVolume;
int g_optMusicVolume, g_musicEnabled, g_optCfgRomData[8];
char g_joystickSlots[4][282];
void Music_SetVolume(int level) { (void)level; }

static int *const scalars[] = {
    &g_gpDifficultyLevel, &g_difficultyConfig, &g_numViewports, &g_numPlayers,
    &g_numHumans, &g_trackId, &g_raceType, &g_raceSubMode, &g_weatherType,
    &g_timeOfDay, &g_ghostMaxFrames
};
static unsigned char fixture[HEADER_BYTES + (CAPACITY + 4) * 2 + 3];
static int cases;
static int active_allocations, close_calls, read_calls;
static int fail_allocation, short_read_call, fail_error, fail_close;

static void check(int condition, const char *message, size_t detail)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s (%zu)\n", message, detail);
        exit(1);
    }
}

void *replay_test_malloc(size_t size)
{
    if (fail_allocation)
        return NULL;
    void *pointer = malloc(size);
    if (pointer != NULL)
        ++active_allocations;
    return pointer;
}

void replay_test_free(void *pointer)
{
    if (pointer != NULL) {
        check(active_allocations > 0, "unexpected replay free", 0);
        --active_allocations;
    }
    free(pointer);
}

size_t replay_test_fread(void *buffer, size_t size, size_t count, FILE *stream)
{
    ++read_calls;
    if (read_calls == short_read_call && count != 0)
        --count;
    return fread(buffer, size, count, stream);
}

int replay_test_ferror(FILE *stream)
{
    return fail_error ? 1 : ferror(stream);
}

int replay_test_fclose(FILE *stream)
{
    ++close_calls;
    int result = fclose(stream);
    return fail_close ? EOF : result;
}

static void expect_resources_released(void)
{
    check(close_calls == 1, "replay stream not closed exactly once", close_calls);
    check(active_allocations == 0, "replay staging allocation leaked", active_allocations);
}

static unsigned short old_frame(size_t index)
{
    return (unsigned short)(0x5a00u ^ index);
}

static unsigned short new_frame(size_t index)
{
    return (unsigned short)(0x8001u + index * 37u);
}

static void reset_state(void)
{
    check(active_allocations == 0, "previous replay allocation leaked", active_allocations);
    close_calls = read_calls = 0;
    fail_allocation = short_read_call = fail_error = fail_close = 0;
    for (size_t i = 0; i < sizeof(scalars) / sizeof(scalars[0]); ++i)
        *scalars[i] = 100 + (int)i;
    memset(players, 0x5a, sizeof(players));
    memcpy(expected_players, players, sizeof(players));
    for (int i = 0; i < MAX_PLAYERS; ++i)
        g_replayCharIds[i] = 1000 + i;
    for (size_t i = 0; i < CAPACITY; ++i)
        g_taGhostSource[i] = old_frame(i);
}

static void expect_unchanged(size_t detail)
{
    for (size_t i = 0; i < sizeof(scalars) / sizeof(scalars[0]); ++i)
        check(*scalars[i] == 100 + (int)i, "failed load changed a game scalar", detail);
    check(memcmp(players, expected_players, sizeof(players)) == 0,
          "failed load changed player structures", detail);
    for (int i = 0; i < MAX_PLAYERS; ++i)
        check(g_replayCharIds[i] == 1000 + i, "failed load changed character IDs", detail);
    for (size_t i = 0; i < CAPACITY; ++i)
        check(g_taGhostSource[i] == old_frame(i), "failed load changed replay data", detail);
    /* g_fileHandle is the existing file-I/O bookkeeping, not replay state. */
}

static void put_word(unsigned char *out, unsigned short value)
{
    out[0] = (unsigned char)value;
    out[1] = (unsigned char)(value >> 8);
}

static void make_header(unsigned short header[HEADER_WORDS], int viewports, int frames)
{
    const unsigned short initial[HEADER_WORDS] = {
        1, 5, 1, 1, RACE_TYPE_GP, SUBMODE_NORMAL, DIFF_HARD,
        WEATHER_SNOW, TOD_NIGHT, 0, 1, 2, 3, 4, 0
    };
    memcpy(header, initial, sizeof(initial));
    header[0] = (unsigned short)viewports;
    header[2] = (unsigned short)viewports;
    header[14] = (unsigned short)frames;
}

static size_t make_fixture(const unsigned short header[HEADER_WORDS], size_t samples)
{
    check(samples <= CAPACITY + 4, "fixture capacity", samples);
    for (int i = 0; i < HEADER_WORDS; ++i)
        put_word(fixture + i * 2, header[i]);
    for (size_t i = 0; i < samples; ++i)
        put_word(fixture + HEADER_BYTES + i * 2, new_frame(i));
    return HEADER_BYTES + samples * 2;
}

static void write_fixture(size_t length)
{
    FILE *fp = fopen(fixture_path, "wb");
    check(fp != NULL, "open fixture", length);
    check(fwrite(fixture, 1, length, fp) == length, "write fixture", length);
    check(fclose(fp) == 0, "close fixture", length);
}

static void expect_failure(size_t length)
{
    write_fixture(length);
    reset_state();
    check(LoadReplayLog(fixture_path) == 0, "invalid replay reported success", length);
    expect_resources_released();
    expect_unchanged(length);
    ++cases;
}

static void expect_loaded(const char *path, const unsigned short header[HEADER_WORDS],
                          size_t samples, size_t length)
{
    reset_state();
    check(LoadReplayLog(path) == 1, "valid replay rejected", length);
    expect_resources_released();
    check(g_gpDifficultyLevel == 101, "previous difficulty was not saved", length);
    check(g_difficultyConfig == header[6], "difficulty mapping", length);
    check(g_numViewports == header[0] && g_numPlayers == header[1] &&
          g_numHumans == header[2], "player count mapping", length);
    int track = header[3] == 3 ? 4 : (header[3] == 4 ? 3 : header[3]);
    check(g_trackId == track, "original track convention", length);
    check(g_raceType == header[4] && g_raceSubMode == header[5], "race mode mapping", length);
    check(g_weatherType == header[7] && g_timeOfDay == header[8], "weather mapping", length);
    check(g_ghostMaxFrames == header[14], "frame count mapping", length);
    for (int i = 0; i < MAX_PLAYERS; ++i) {
        expected_players[i].charId = (short)header[9 + i];
        check(g_replayCharIds[i] == (int)(short)header[9 + i], "character sign extension", i);
    }
    check(memcmp(players, expected_players, sizeof(players)) == 0,
          "load changed unrelated player fields", length);
    for (size_t i = 0; i < CAPACITY; ++i) {
        unsigned short expected = i < samples
            ? (unsigned short)(fixture[HEADER_BYTES + i * 2] |
                               (unsigned short)fixture[HEADER_BYTES + i * 2 + 1] << 8)
            : old_frame(i);
        check(g_taGhostSource[i] == expected,
              "replay payload or untouched tail", i);
    }
    ++cases;
}

static void expect_success(const unsigned short header[HEADER_WORDS], size_t samples,
                           size_t length)
{
    write_fixture(length);
    expect_loaded(fixture_path, header, samples, length);
}

static void test_external(const char *path)
{
    unsigned short header[HEADER_WORDS];
    FILE *fp = fopen(path, "rb");
    check(fp != NULL, "open external replay", 0);
    size_t length = fread(fixture, 1, sizeof(fixture), fp);
    check(!ferror(fp) && feof(fp), "external replay exceeds fixture buffer", length);
    check(fclose(fp) == 0, "close external replay", length);
    check(length >= HEADER_BYTES, "external replay header", length);
    for (int i = 0; i < HEADER_WORDS; ++i)
        header[i] = (unsigned short)(fixture[i * 2] | (unsigned short)fixture[i * 2 + 1] << 8);
    size_t samples = (size_t)header[0] * header[14];
    check(samples <= CAPACITY && length == HEADER_BYTES + samples * 2,
          "external replay declared size", length);
    expect_loaded(path, header, samples, length);
    printf("external replay: %s: header=[", path);
    for (int i = 0; i < HEADER_WORDS; ++i)
        printf("%s%u", i == 0 ? "" : ",", (unsigned int)header[i]);
    printf("]: runtime track=%d, frames/viewport=%d, samples=%zu: PASS\n",
           g_trackId, g_ghostMaxFrames, samples);
}

static void test_overflow(void)
{
    unsigned short header[HEADER_WORDS];
    /* Baseline writes 65,538 bytes into the actual 65,536-byte destination. */
    make_header(header, 1, CAPACITY + 1);
    expect_failure(make_fixture(header, CAPACITY + 1));
    for (int vp = 2; vp <= 4; ++vp) {
        int frames = CAPACITY / vp + 1;
        make_header(header, vp, frames);
        expect_failure(make_fixture(header, (size_t)vp * frames));
    }
    make_header(header, 1, 0xffff);
    expect_failure(make_fixture(header, 0));
}

static void test_truncated(void)
{
    unsigned short header[HEADER_WORDS];
    /* Every header and payload prefix for each supported viewport count. */
    for (int vp = 1; vp <= 4; ++vp) {
        make_header(header, vp, 3);
        size_t length = make_fixture(header, (size_t)vp * 3);
        /* Start with a complete header: baseline failure must not depend on
         * the uninitialized values its short-header path also consumes. */
        expect_failure(length - 1);
        for (size_t prefix = 0; prefix + 1 < length; ++prefix)
            expect_failure(prefix);
    }
    make_header(header, 1, CAPACITY);
    size_t length = make_fixture(header, CAPACITY);
    expect_failure(length - 1);
    expect_failure(length - 2);
    remove(fixture_path);
    reset_state();
    check(LoadReplayLog(fixture_path) == 0, "missing replay reported success", 0);
    check(close_calls == 0 && active_allocations == 0, "missing file used resources", 0);
    expect_unchanged(0);
    ++cases;
}

static void test_metadata(void)
{
    static const struct { int field; unsigned short value; } invalid[] = {
        {0, 0}, {0, 5}, {0, 0xffff}, {1, 0}, {1, 6}, {1, 0xffff},
        {2, 0}, {2, 5}, {2, 2}, {3, 0}, {3, 6}, {3, 0xffff},
        {4, 5}, {5, 4}, {6, 3}, {7, 3}, {8, 4}
    };
    unsigned short header[HEADER_WORDS];
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        make_header(header, 1, 1);
        header[invalid[i].field] = invalid[i].value;
        expect_failure(make_fixture(header, 1));
    }
    make_header(header, 2, 1);
    header[1] = 1; /* More viewports than players. */
    expect_failure(make_fixture(header, 2));
    for (int i = 0; i < MAX_PLAYERS; ++i) {
        make_header(header, 1, 1);
        header[9 + i] = CHAR_COUNT;
        expect_failure(make_fixture(header, 1));
        header[9 + i] = 0xffff;
        expect_failure(make_fixture(header, 1));
    }
}

static void test_valid(void)
{
    unsigned short header[HEADER_WORDS];
    for (int vp = 1; vp <= 4; ++vp) {
        make_header(header, vp, CAPACITY / vp);
        header[1] = (unsigned short)vp;
        header[4] = RACE_TYPE_MULTIPLAYER;
        size_t samples = (size_t)vp * header[14];
        expect_success(header, samples, make_fixture(header, samples));
        make_header(header, vp, 0);
        expect_success(header, 0, make_fixture(header, 0));
    }
    for (int track = 1; track <= 5; ++track) {
        make_header(header, 1, 3);
        header[3] = (unsigned short)track;
        expect_success(header, 3, make_fixture(header, 3));
    }
    for (int mode = 0; mode <= RACE_TYPE_INTERNAL_4; ++mode) {
        make_header(header, 1, 1);
        header[4] = (unsigned short)mode;
        header[5] = (unsigned short)(mode % 4);
        header[6] = (unsigned short)(mode % 3);
        header[7] = (unsigned short)(mode % 3);
        header[8] = (unsigned short)(mode % 4);
        expect_success(header, 1, make_fixture(header, 1));
    }
    for (int count = 1; count <= MAX_PLAYERS; ++count) {
        make_header(header, 1, 1);
        header[1] = (unsigned short)count;
        expect_success(header, 1, make_fixture(header, 1));
    }
    for (int character = 0; character < CHAR_COUNT; ++character) {
        make_header(header, 1, 1);
        header[9 + character % MAX_PLAYERS] = (unsigned short)character;
        expect_success(header, 1, make_fixture(header, 1));
    }
    make_header(header, 1, 2);
    header[1] = 1;
    header[9] = CHAR_SUPER_SONIC;
    header[10] = 0xffff; /* Unused character words retain legacy sign extension. */
    header[11] = 0x8000;
    header[12] = 0x1234;
    header[13] = 0;
    size_t length = make_fixture(header, 2);
    fixture[length] = 0xa5;
    fixture[length + 1] = 0x12;
    fixture[length + 2] = 0xff;
    expect_success(header, 2, length + 3); /* Trailing bytes remain tolerated. */
}

static void test_failed_reload(void)
{
    unsigned short header[HEADER_WORDS];
    int saved_scalars[sizeof(scalars) / sizeof(scalars[0])];
    int saved_char_ids[MAX_PLAYERS];
    make_header(header, 1, 2);
    size_t length = make_fixture(header, 2);
    expect_success(header, 2, length);
    for (size_t i = 0; i < sizeof(scalars) / sizeof(scalars[0]); ++i)
        saved_scalars[i] = *scalars[i];
    memcpy(saved_char_ids, g_replayCharIds, sizeof(saved_char_ids));
    /* A failed load after a successful one must retain that successful state. */
    fixture[HEADER_BYTES] ^= 0xff;
    write_fixture(length - 1);
    close_calls = read_calls = 0;
    check(LoadReplayLog(fixture_path) == 0, "truncated reload reported success", length);
    expect_resources_released();
    for (size_t i = 0; i < sizeof(scalars) / sizeof(scalars[0]); ++i)
        check(*scalars[i] == saved_scalars[i], "truncated reload changed metadata", i);
    check(memcmp(saved_char_ids, g_replayCharIds, sizeof(saved_char_ids)) == 0,
          "truncated reload changed character IDs", length);
    check(memcmp(players, expected_players, sizeof(players)) == 0,
          "truncated reload changed players", length);
    for (size_t i = 0; i < CAPACITY; ++i)
        check(g_taGhostSource[i] == (i < 2 ? new_frame(i) : old_frame(i)),
              "truncated reload changed payload", i);
    ++cases;
}

static void test_io_failures(void)
{
    unsigned short header[HEADER_WORDS];
    make_header(header, 1, 3);
    size_t length = make_fixture(header, 3);
    write_fixture(length);
    for (int fault = 0; fault < 5; ++fault) {
        reset_state();
        switch (fault) {
            case 0: fail_allocation = 1; break;
            case 1: short_read_call = 1; break;
            case 2: short_read_call = 2; break;
            case 3: fail_error = 1; break;
            case 4: fail_close = 1; break;
        }
        check(LoadReplayLog(fixture_path) == 0, "I/O/allocation failure reported success", fault);
        expect_unchanged(fault);
        expect_resources_released();
        ++cases;
    }
}

int main(int argc, char **argv)
{
    const char *which = argc > 1 ? argv[1] : "all";
    if (strcmp(which, "external") == 0) {
        check(argc > 2, "external replay paths required", 0);
        for (int i = 2; i < argc; ++i)
            test_external(argv[i]);
        printf("replay loader: %d external cases passed\n", cases);
        return 0;
    }
    if (strcmp(which, "all") == 0 || strcmp(which, "overflow") == 0)
        test_overflow();
    if (strcmp(which, "all") == 0 || strcmp(which, "truncated") == 0)
        test_truncated();
    if (strcmp(which, "all") == 0)
        test_metadata();
    if (strcmp(which, "all") == 0 || strcmp(which, "valid") == 0)
        test_valid();
    if (strcmp(which, "all") == 0)
        test_failed_reload();
    if (strcmp(which, "all") == 0)
        test_io_failures();
    check(cases != 0, "unknown test case", 0);
    check(remove(fixture_path) == 0 || strcmp(which, "truncated") == 0,
          "remove fixture", 0);
    printf("replay loader: %d %s cases passed\n", cases, which);
    return 0;
}
