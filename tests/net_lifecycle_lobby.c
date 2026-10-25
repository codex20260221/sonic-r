/* Compile both complete production lobby loops with the fixture clock. */
#include "sonicr_types.h"
#ifdef _MSC_VER
/* The full screen TU includes unrelated integer-to-float renderer calls. */
#pragma warning(disable: 4244)
#endif
DWORD lifecycle_time_ms(void);
#define timeGetTime lifecycle_time_ms
#include SONICR_TEST_LOBBY_SOURCE
