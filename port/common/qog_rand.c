/* One random number generator for every port.
 *
 * The engine calls rand() for gameplay (monster chase directions, QuakeC's
 * random()) and for effects. Each C library has its own rand(), so the PC
 * (MSVC) and the board (newlib) would make different choices. The builds map
 * rand to qog_rand with a compiler define (-Drand=qog_rand), so the engine
 * source is unchanged and every port gets the same sequence: MSVC's own
 * linear congruential generator, which is what the PC used before.
 *
 * qog_rand_calls counts calls, which qog_statehash prints so a run that
 * diverges can be traced to the first difference in random number use. */
#include "qog_rand.h"

static unsigned s_seed = 1;
unsigned qog_rand_calls;

int qog_rand(void) {
    qog_rand_calls++;
    s_seed = s_seed * 214013u + 2531011u;
    return (int)((s_seed >> 16) & 0x7FFF);
}

void qog_srand(unsigned seed) {
    s_seed = seed;
    qog_rand_calls = 0;
}
