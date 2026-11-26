/* QuakeOG, the display client's power poll, see power_qog.c. */
#ifndef QOG_POWER_H
#define QOG_POWER_H
#include <stdint.h>
#include "power/power_poll.h"

/* fwog_power_poll() with red counted toward power off only while it is the
   only button held. Built with QOG_POWER_RED_ALONE (apps/quake quake_display).
   other apps keep the BSP's FWOG_POWER_DEFAULT and fwog_power_poll(). */
fwog_power_t qog_power_poll(uint32_t now_ms);

#ifdef QOG_POWER_RED_ALONE
#define QOG_POWER_POLL(ms) qog_power_poll(ms)
#else
#define QOG_POWER_POLL(ms) fwog_power_poll(ms)
#endif

#endif
