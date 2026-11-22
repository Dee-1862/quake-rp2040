/* Display-CPU flash map for QuakeOG.
 *
 *   0x000000  BSP serial bootloader     128 KB  (never touch; BSP rule)
 *   0x020000  BSP app metadata            4 KB  (written only by the bootloader)
 *   0x021000  QuakeOG application      ~3.9 MB  (QOG_APP_MAX_SIZE, checked post-build)
 *   0x400000  game data region          ~12 MB  (QOGD image, see qog_data.h)
 *   0xFF0000  save slots                 64 KB  (QOG_SAVE_SIZE)
 *
 * Why this works: a display update erases only the sectors the new image
 * needs (wiliOGbsp bootloader/bl_receiver.c, "Erase only the sectors this
 * image needs"), so a code update never touches the data region as long as
 * the app stays under QOG_APP_MAX_SIZE.
 *
 * tools/mkdata.py hard-codes QOG_DATA_OFFSET too. Change both together. */
#ifndef QOG_FLASHMAP_H
#define QOG_FLASHMAP_H

#define QOG_XIP_BASE        0x10000000u
#define QOG_FLASH_SIZE      (16u * 1024u * 1024u)

#define QOG_APP_OFFSET      0x00021000u
#define QOG_DATA_OFFSET     0x00400000u
#define QOG_SAVE_OFFSET     0x00FF0000u

#define QOG_APP_MAX_SIZE    (QOG_DATA_OFFSET - QOG_APP_OFFSET)
#define QOG_DATA_MAX_SIZE   (QOG_SAVE_OFFSET - QOG_DATA_OFFSET)
#define QOG_SAVE_SIZE       (QOG_FLASH_SIZE - QOG_SAVE_OFFSET)

#define QOG_DATA_XIP_ADDR   (QOG_XIP_BASE + QOG_DATA_OFFSET)

#endif
