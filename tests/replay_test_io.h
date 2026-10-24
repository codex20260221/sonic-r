/* Force-include for the replay harness only: forward real C-library calls,
 * with deterministic allocation and I/O failures selected by test_replay.c. */
#ifndef SONICR_REPLAY_TEST_IO
#define SONICR_REPLAY_TEST_IO
#include <stdio.h>
#include <stdlib.h>

void *replay_test_malloc(size_t size);
void replay_test_free(void *pointer);
size_t replay_test_fread(void *buffer, size_t size, size_t count, FILE *stream);
int replay_test_ferror(FILE *stream);
int replay_test_fclose(FILE *stream);

#define malloc replay_test_malloc
#define free replay_test_free
#define fread replay_test_fread
#define ferror replay_test_ferror
#define fclose replay_test_fclose
#endif
