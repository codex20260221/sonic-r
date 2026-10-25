/* Force-include only when testing the production terrain loader. */
#ifndef SONICR_TERRAIN_TEST_IO
#define SONICR_TERRAIN_TEST_IO
#include <stdio.h>
#include <stdlib.h>

FILE *terrain_test_fopen(const char *path, const char *mode);
size_t terrain_test_fread(void *buffer, size_t size, size_t count, FILE *file);
int terrain_test_fseek(FILE *file, long offset, int whence);
long terrain_test_ftell(FILE *file);
int terrain_test_ferror(FILE *file);
int terrain_test_fclose(FILE *file);
_Noreturn void terrain_test_exit(int status);

#define fopen terrain_test_fopen
#define fread terrain_test_fread
#define fseek terrain_test_fseek
#define ftell terrain_test_ftell
#define ferror terrain_test_ferror
#define fclose terrain_test_fclose
#define exit terrain_test_exit
#endif
