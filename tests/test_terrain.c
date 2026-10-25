/* Synthetic files exercise the real LoadTerrain / ParseTerrainHeader. */
#ifndef SONICR_TERRAIN_TEST_IO
#error "Compile with tests/run_terrain.py"
#endif
#undef fopen
#undef fread
#undef fseek
#undef ftell
#undef ferror
#undef fclose
#undef exit
#include "sonicr_globals.h"
#include "sonicr_functions.h"
#include <setjmp.h>
#include <limits.h>
#include <stdint.h>

enum { CAPACITY = 80492, HEADER = 92, VALID_SIZE = 2360 };
int g_trackId;
void *g_terVertexTable, *g_terFaceTable, *g_trackSurfaceData, *g_terCollisionMesh;
void *g_terEdgeList, *g_terLoopTable, *g_terUnknown74, *g_terGridIndex, *g_terGridData;
void *g_itemStateTable;
int g_terScalar5bc, g_terScalar5c0, g_terScalar5c4, g_terScalar5c8, g_terScalar5cc;
int g_terLoopCount, g_terScalar5d4, g_terScalar5d8, g_terCollectibleCount;
int g_aiGridOriginX, g_aiGridOriginZ, g_aiGridCellWidth, g_aiGridCellHeight;
int g_terCollisionMeshSize;
/* Resolve unused track_load.c references on PE/COFF; these paths must not run. */
int g_tpageHeight[1], g_tpageWidth[1], g_tpageCount;
void *g_tpagePixelBuf[1], *g_soundBuffers[64];
int g_soundActive[64], g_timeOfDay, g_weatherType, g_bitsPerPixel;
FILE *g_fileHandle;
void DebugLog(char *fmt, ...) { (void)fmt; abort(); }
void SFX_Stop(int slot) { (void)slot; abort(); }
void ClaimTpage(int tpage, int w, int h, int state)
{ (void)tpage; (void)w; (void)h; (void)state; abort(); }
void R_SetTpageGreen6(int tpage, int on) { (void)tpage; (void)on; abort(); }
void R_SetNoColorKey(int tpage) { (void)tpage; abort(); }
void R_MarkTextureDirty(int tpage) { (void)tpage; abort(); }
void R_SetTpageRGBA8(int tpage, unsigned char *rgba) { (void)tpage; (void)rgba; abort(); }
static void **const pointers[] = {
    &g_terVertexTable, &g_terFaceTable, &g_trackSurfaceData, &g_terCollisionMesh,
    &g_terEdgeList, &g_terLoopTable, &g_terUnknown74, &g_terGridIndex, &g_terGridData,
    &g_itemStateTable
};
static int *const scalars[] = {
    &g_terScalar5bc, &g_terScalar5c0, &g_terScalar5c4, &g_terScalar5c8, &g_terScalar5cc,
    &g_terLoopCount, &g_terScalar5d4, &g_terScalar5d8, &g_terCollectibleCount,
    &g_aiGridOriginX, &g_aiGridOriginZ, &g_aiGridCellWidth, &g_aiGridCellHeight
};
static const int offsets[10] = {92, 116, 132, 148, 164, 176, 200, 268, 2316, 2324};
static const int counts[9] = {4, 1, 1, 1, 2, 1, 2, 3, 3};
static unsigned char fixture[CAPACITY + 1];
static FILE *stream;
static const char *external_path;
static jmp_buf rejected;
static int cases, close_calls, read_calls, seek_calls, allow_overflow;
static int fail_open, fail_seek, fail_tell, short_read, fail_error, fail_close;
static long reported_size;

static void check(int ok, const char *message)
{
    if (!ok) {
        fprintf(stderr, "FAIL: terrain case %d: %s\n", cases + 1, message);
        exit(1);
    }
}

static void put16(size_t offset, unsigned value)
{
    fixture[offset] = (unsigned char)value;
    fixture[offset + 1] = (unsigned char)(value >> 8);
}

static void put32(size_t offset, uint32_t value)
{
    for (int i = 0; i < 4; ++i) fixture[offset + i] = (unsigned char)(value >> (i * 8));
}

static int get32(size_t offset)
{
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i) v |= (uint32_t)fixture[offset + i] << (i * 8);
    return (int32_t)v;
}

FILE *terrain_test_fopen(const char *path, const char *mode)
{
    (void)path;
    check(strcmp(mode, "rb") == 0, "terrain must open read-only");
    if (fail_open) return NULL;
    if (external_path != NULL) stream = fopen(external_path, mode);
    return stream;
}

size_t terrain_test_fread(void *buffer, size_t size, size_t count, FILE *file)
{
    ++read_calls;
    if (!allow_overflow)
        check(size == 1 && count <= CAPACITY, "read exceeds fixed terrain buffer");
    if (short_read && count != 0) --count;
    return fread(buffer, size, count, file);
}

int terrain_test_fseek(FILE *file, long offset, int whence)
{
    if (++seek_calls == fail_seek) return -1;
    return fseek(file, offset, whence);
}

long terrain_test_ftell(FILE *file)
{
    return fail_tell ? reported_size : ftell(file);
}

int terrain_test_ferror(FILE *file) { return fail_error ? 1 : ferror(file); }

int terrain_test_fclose(FILE *file)
{
    ++close_calls;
    int result = fclose(file);
    stream = NULL;
    return fail_close ? EOF : result;
}

_Noreturn void terrain_test_exit(int status)
{
    check(status == EXIT_FAILURE, "invalid terrain must exit with failure");
    longjmp(rejected, 1);
}

static void valid_fixture(void)
{
    memset(fixture, 0, sizeof(fixture));
    for (int i = 0; i < 10; ++i) put32(i * 4, offsets[i]);
    for (int i = 0; i < 9; ++i) put32(40 + i * 4, counts[i]);
    put32(76, (uint32_t)-100); put32(80, (uint32_t)-200);
    put32(84, 10); put32(88, 20);
    for (int i = 0; i < 12; ++i) put16(92 + i * 2, (unsigned)(i * 3));
    for (int i = 0; i < 4; ++i) put16(116 + i * 2, (unsigned)(i * 6));
    put16(132 + 2, 1); /* one face */
    put32(132 + 8, 100);
    fixture[148 + 2] = 2; /* two wall vertices */
    put16(176 + 2, 2); /* two loop rows */
    put16(176 + 14, 10); put16(176 + 16, 10); put16(176 + 18, 1);
    put16(268 + 1023 * 2, 2); /* final grid cell is empty */
    put16(2316, 0); put16(2318, 0xc000); put16(2320, 0xffff);
    for (int i = 0; i < 3; ++i) {
        put16(2324 + i * 12 + 2, 0xffff); /* absent second item mesh */
        put16(2324 + i * 12 + 8, 1);
    }
    fail_open = fail_seek = fail_tell = short_read = fail_error = fail_close = 0;
    external_path = NULL;
    g_trackId = TRACK_RESORT_ISLAND;
}

static int load_and_parse(void)
{
    if (setjmp(rejected) != 0) return 0;
    LoadTerrain();
    ParseTerrainHeader();
    return 1;
}

static void test_parse_before_load(void)
{
    if (setjmp(rejected) == 0) {
        ParseTerrainHeader();
        check(0, "parsed terrain before a successful load");
    }
    for (int i = 0; i < 10; ++i) check(*pointers[i] == NULL, "published unloaded pointer");
    for (int i = 0; i < 13; ++i) check(*scalars[i] == 0, "published unloaded scalar");
    ++cases;
}

static void run_case(size_t size, int accept)
{
    void *old_pointers[10];
    int old_scalars[13], old_mesh_size = g_terCollisionMeshSize;
    for (int i = 0; i < 10; ++i) old_pointers[i] = *pointers[i];
    for (int i = 0; i < 13; ++i) old_scalars[i] = *scalars[i];
    close_calls = read_calls = seek_calls = 0;
    if (external_path == NULL) {
        stream = tmpfile();
        check(stream != NULL, "create temporary fixture");
        check(fwrite(fixture, 1, size, stream) == size, "write fixture");
        rewind(stream);
    }
    int accepted = load_and_parse();
    check(accepted == accept, accept ? "valid terrain rejected" : "invalid terrain accepted");
    int opened = !fail_open && g_trackId >= TRACK_RESORT_ISLAND && g_trackId <= TRACK_RADIANT_EMERALD;
    check(close_calls == opened, "close opened terrain exactly once");
    if (accepted) {
        unsigned char *base = (unsigned char *)g_terVertexTable - get32(0);
        for (int i = 0; i < 10; ++i)
            check(*pointers[i] == base + get32(i * 4), "published pointer differs from file");
        for (int i = 0; i < 13; ++i)
            check(*scalars[i] == get32(40 + i * 4), "published scalar differs from file");
        check(g_terCollisionMeshSize == get32(16) - get32(12), "collision mesh extent");
        check(memcmp(base, fixture, size) == 0, "loaded bytes changed on little-endian host");
    } else {
        for (int i = 0; i < 10; ++i) check(*pointers[i] == old_pointers[i], "published invalid pointer");
        for (int i = 0; i < 13; ++i) check(*scalars[i] == old_scalars[i], "published invalid scalar");
        check(g_terCollisionMeshSize == old_mesh_size, "published invalid mesh size");
    }
    if (stream != NULL) { fclose(stream); stream = NULL; }
    ++cases;
}

static void test_header(void)
{
    for (int word = 0; word < 10; ++word) {
        static const uint32_t bad[] = {0, 91, 93, CAPACITY + 1, UINT32_MAX, 0x80000000u};
        for (size_t j = 0; j < sizeof(bad) / sizeof(*bad); ++j) {
            valid_fixture(); put32(word * 4, bad[j]); run_case(VALID_SIZE, 0);
        }
    }
    for (int word = 10; word < 19; ++word) {
        valid_fixture(); put32(word * 4, UINT32_MAX); run_case(VALID_SIZE, 0);
        valid_fixture(); put32(word * 4, INT_MAX); run_case(VALID_SIZE, 0);
    }
    for (int word = 21; word < 23; ++word) {
        static const uint32_t bad[] = {0, UINT32_MAX, INT_MAX};
        for (size_t j = 0; j < sizeof(bad) / sizeof(*bad); ++j) {
            valid_fixture(); put32(word * 4, bad[j]); run_case(VALID_SIZE, 0);
        }
    }
    valid_fixture(); put32(76, INT_MIN); run_case(VALID_SIZE, 0);
    valid_fixture(); put32(80, INT_MAX); run_case(VALID_SIZE, 0);
    valid_fixture(); put32(4, 90); run_case(VALID_SIZE, 0); /* descending */
    valid_fixture(); put32(28, 2316); run_case(VALID_SIZE, 0); /* short grid index */
    for (int track = 1; track <= 5; ++track) {
        valid_fixture(); g_trackId = track;
        put32(72, track == TRACK_RADICAL_CITY || track == TRACK_REACTIVE_FACTORY ? 2 : 1);
        run_case(VALID_SIZE, 0); /* fixed animation item groups */
    }
}

static void test_references(void)
{
    static const struct { size_t offset; unsigned value; } bad[] = {
        {116, 24}, {118, 1}, {132, 0xffff}, {134, 0}, {134, 2},
        {148, 0xffff}, {150, 3}, {176, 0xffff}, {178, 0}, {178, 3},
        {180, 1}, {182, 1}, {194, 3}, {192, 0},
        {268, 0xffff}, {268, 3}, {2316, 1}, {2318, 0xc001},
        {2318, 0xffff}, {2324, 1}, {2326, 0xfffe}, {2326, 1}
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(*bad); ++i) {
        valid_fixture(); put16(bad[i].offset, bad[i].value); run_case(VALID_SIZE, 0);
    }
    valid_fixture(); put16(2318, 0); put16(2320, 0); run_case(VALID_SIZE, 0);
}

static void test_io(void)
{
    static const int invalid_tracks[] = {-1, TRACK_NONE, TRACK_RADIANT_EMERALD + 1};
    for (size_t i = 0; i < sizeof(invalid_tracks) / sizeof(*invalid_tracks); ++i) {
        valid_fixture(); g_trackId = invalid_tracks[i]; run_case(VALID_SIZE, 0);
        check(read_calls == 0, "read for an invalid track");
    }
    valid_fixture(); fail_open = 1; run_case(VALID_SIZE, 0);
    for (int i = 1; i <= 2; ++i) { valid_fixture(); fail_seek = i; run_case(VALID_SIZE, 0); }
    static const long sizes[] = {-1, 0, 91, CAPACITY + 1, LONG_MAX};
    for (size_t i = 0; i < sizeof(sizes) / sizeof(*sizes); ++i) {
        valid_fixture(); fail_tell = 1; reported_size = sizes[i]; run_case(VALID_SIZE, 0);
        check(read_calls == 0, "read after invalid reported size");
    }
    valid_fixture(); short_read = 1; run_case(VALID_SIZE, 0);
    valid_fixture(); fail_error = 1; run_case(VALID_SIZE, 0);
    valid_fixture(); fail_close = 1; run_case(VALID_SIZE, 0);
    valid_fixture(); run_case(VALID_SIZE, 1); /* successful reload after failures */
}

int main(int argc, char **argv)
{
    const char *which = argc > 1 ? argv[1] : "all";
    if (!strcmp(which, "external")) {
        for (int i = 2; i < argc; ++i) {
            valid_fixture();
            FILE *file = fopen(argv[i], "rb");
            check(file != NULL, "open external terrain read-only");
            size_t size = fread(fixture, 1, sizeof(fixture), file);
            check(!ferror(file) && size <= CAPACITY, "read external terrain");
            check(fclose(file) == 0, "close external comparison stream");
            external_path = argv[i]; run_case(size, 1);
        }
    } else {
        if (!strcmp(which, "all")) {
            test_parse_before_load();
        }
        if (!strcmp(which, "all") || !strcmp(which, "valid")) {
            for (int i = 1; i <= 5; ++i) {
                valid_fixture(); g_trackId = i;
                put32(72, i == TRACK_RADICAL_CITY || i == TRACK_REACTIVE_FACTORY ? 3 : 2);
                run_case(VALID_SIZE, 1);
            }
            valid_fixture(); memset(fixture + VALID_SIZE, 0xa5, CAPACITY - VALID_SIZE);
            run_case(CAPACITY, 1);
            valid_fixture(); run_case(VALID_SIZE, 1); /* long-to-short reload */
            check(*((unsigned char *)g_terVertexTable - HEADER + CAPACITY - 1) == 0xa5,
                  "short reload wrote beyond the file length");
        }
        if (!strcmp(which, "all") || !strcmp(which, "overflow")) {
            valid_fixture(); allow_overflow = !strcmp(which, "overflow");
            run_case(CAPACITY + 1, 0); allow_overflow = 0;
        }
        if (!strcmp(which, "all") || !strcmp(which, "truncated")) {
            for (size_t i = 0; i < VALID_SIZE; ++i) { valid_fixture(); run_case(i, 0); }
        }
        if (!strcmp(which, "all") || !strcmp(which, "header")) test_header();
        if (!strcmp(which, "all") || !strcmp(which, "references")) test_references();
        if (!strcmp(which, "all") || !strcmp(which, "io")) test_io();
    }
    printf("terrain: %d %s cases passed\n", cases, which);
    return 0;
}
