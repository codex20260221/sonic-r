/* Regression tests for the production SONICR.INF loader, without game assets.
 * The runner supplies a temporary working directory for the fixture files. */
#include "sonicr_types.h"
#include "sonicr_globals.h"
#include "sonicr_functions.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

int LoadGameSettings(void);

/* Globals used by LoadGameSettings. */
FILE *g_fileHandle;
int g_difficultyConfig, g_ghostToggle, g_weatherConfig, g_catchUpToggle;
int g_guideToggle, g_minimapConfig, g_splitScreenMode;
int g_optResLow, g_optResHigh, g_optCfg_468, g_optCfg_46c, g_optCfg_470;
int g_renderQuality, g_softDoubleBuf, g_optCfg_47c, g_qualityLevel;
int g_resolutionLevel, g_optCfg_488, g_optCfg_48c, g_doubleWidthFlag;
int g_stereoEnabled, g_vocalsEnabled, g_optSfxVolume, g_optMusicVolume;
int g_musicEnabled;
int g_optCfgRomData[8];

/* PE/COFF linkers can retain references from unused save.c entry points.
 * Supply their data without replacing any production settings code. */
int g_raceSpeedMult, g_screenWidth, g_screenHeight;
int g_gpDifficultyLevel, g_numViewports, g_numPlayers, g_numHumans;
int g_trackId, g_raceType, g_raceSubMode, g_weatherType, g_timeOfDay;
int g_ghostMaxFrames, g_replayCharIds[MAX_PLAYERS];
unsigned short g_taGhostSource[0x8000];
char g_joystickSlots[4][282];
Player *g_playerBase;

static int *const fields[] = {
    &g_difficultyConfig, &g_ghostToggle, &g_weatherConfig, &g_catchUpToggle,
    &g_guideToggle, &g_minimapConfig, &g_splitScreenMode,
    &g_optResLow, &g_optResHigh, &g_optCfg_468, &g_optCfg_46c, &g_optCfg_470,
    &g_renderQuality, &g_softDoubleBuf, &g_optCfg_47c, &g_qualityLevel,
    &g_resolutionLevel, &g_optCfg_488, &g_optCfg_48c, &g_doubleWidthFlag,
    &g_stereoEnabled, &g_vocalsEnabled, &g_optSfxVolume, &g_optMusicVolume,
    &g_optCfgRomData[0], &g_optCfgRomData[1], &g_optCfgRomData[2],
    &g_optCfgRomData[3], &g_optCfgRomData[4], &g_optCfgRomData[5],
    &g_optCfgRomData[6], &g_optCfgRomData[7]
};

static int volume_calls;
static int last_volume;

void Music_SetVolume(int level)
{
    ++volume_calls;
    last_volume = level;
}

static void check(int condition, const char *message, int detail)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s (%d)\n", message, detail);
        exit(1);
    }
}

static void reset_settings(void)
{
    for (size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); ++i) {
        *fields[i] = 1000 + (int)i;
    }
    g_musicEnabled = 77;
    volume_calls = 0;
    last_volume = -123;
}

static void expect_unchanged(int length)
{
    for (size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); ++i) {
        check(*fields[i] == 1000 + (int)i,
              "partial/missing settings changed an option", length);
    }
    check(g_musicEnabled == 77, "partial/missing settings changed music state", length);
    check(volume_calls == 0, "partial/missing settings changed audio volume", length);
}

/* The file format is 40 little-endian 32-bit words, including 8 reserved words.
 * Build it byte-wise so the fixture does not depend on host byte order. */
static void write_word(unsigned char *p, int value)
{
    uint32_t v = (uint32_t)value;
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16);
    p[3] = (unsigned char)(v >> 24);
}

static void make_record(unsigned char record[164], int volume)
{
    for (int i = 0; i < 41; ++i) {
        write_word(record + i * 4, 7 + i);
    }
    write_word(record + 23 * 4, volume);
}

static void write_prefix(const unsigned char *record, size_t length)
{
    FILE *fp = fopen("SONICR.INF", "wb");
    check(fp != NULL, "create settings fixture", (int)length);
    check(fwrite(record, 1, length, fp) == length, "write settings fixture", (int)length);
    check(fclose(fp) == 0, "close settings fixture", (int)length);
}

static void test_partial(const unsigned char *record, size_t length)
{
    reset_settings();
    write_prefix(record, length);
    check(LoadGameSettings() == 1, "preserve loader return convention", (int)length);
    expect_unchanged((int)length);
}

static void test_complete(int disk_volume, int expected_volume, size_t length)
{
    unsigned char record[164];
    make_record(record, disk_volume);
    reset_settings();
    write_prefix(record, length);
    check(LoadGameSettings() == 1, "complete settings load", disk_volume);
    for (size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); ++i) {
        int expected = i == 23 ? expected_volume : 7 + (int)i;
        check(*fields[i] == expected, "complete settings field mapping", (int)i);
    }
    check(g_musicEnabled == (expected_volume != 0), "music enabled state", disk_volume);
    check(volume_calls == 1 && last_volume == expected_volume,
          "music volume callback", disk_volume);
}

int main(void)
{
    _Static_assert(sizeof(int) == 4, "SONICR.INF requires 32-bit int fields");
    unsigned char record[164];
    make_record(record, 5);

    /* Run a known, initialized prefix first: the old loader sets difficulty
     * to 7 from these four bytes. Failure does not depend on unread stack data. */
    test_partial(record, 4);
    for (size_t length = 0; length < 160; ++length) {
        if (length != 4) {
            test_partial(record, length);
        }
    }

    check(remove("SONICR.INF") == 0, "remove settings fixture", 0);
    reset_settings();
    check(LoadGameSettings() == 1, "missing file return convention", 0);
    expect_unchanged(-1);

    test_complete(0, 0, 160);   /* legacy music toggle: off */
    test_complete(1, 1, 160);   /* legacy music toggle: on, lowest volume */
    test_complete(5, 5, 160);
    test_complete(8, 8, 160);
    test_complete(-1, 0, 160);
    test_complete(9, 8, 160);
    test_complete(5, 5, 164);   /* preserve tolerance of extra trailing data */

    puts("settings: 160 short records, missing file, and 7 complete records passed");
    return 0;
}
