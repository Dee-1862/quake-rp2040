/* RP2040 sound output: id's SNDDMA calls. Not wired to the I2S speaker
 * yet (plan Phase 5: mixer on core 1 at 11025 Hz into a DMA ring), so it
 * reports no device and the engine runs silent. */
#include "quakedef.h"

qboolean SNDDMA_Init(void) { return false; }
int SNDDMA_GetDMAPos(void) { return 0; }
void SNDDMA_Submit(void) {}
void SNDDMA_Shutdown(void) {}
