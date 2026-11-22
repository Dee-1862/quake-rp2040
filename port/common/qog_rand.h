#ifndef QOG_RAND_H
#define QOG_RAND_H

/* See qog_rand.c. RAND_MAX stays 0x7FFF, as on MSVC. */
int qog_rand(void);
void qog_srand(unsigned seed);
extern unsigned qog_rand_calls;

#endif
