/* Compile the complete production session-creation code. Only test socket
 * ports are redirected; the unrelated game entry point is discarded at link. */
#define main lifecycle_game_main
#define net_host_start lifecycle_host_start
#define net_client_connect lifecycle_client_connect
#include SONICR_TEST_MAIN_SOURCE
