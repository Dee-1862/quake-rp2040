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

/* Main-CPU flash map for the split game (Agent R): the server half runs on
 * the main RP2040, which has its own 16 MB flash.
 *
 *   0x000000  quake_main, the display image embedded in it   3 MB (QOG_MAIN_APP_MAX_SIZE, checked post-build)
 *   0x300000  server data region                             5 MB (pak and baked image for this address)
 *   0x800000  BSP FAT volume                                 8 MB (fs/fs_geom.h FWOG_FS_FLASH_OFFSET, never touch)
 *
 * The main app is linked by the SDK at the start of flash (fwog_main_app adds
 * no offset). The server data is written with a data-only UF2 through main's
 * BOOTSEL drive: it covers only 0x10300000 to 0x107FFFFF, so the app and the
 * FAT volume keep their bytes. tools/mkboard.py --main writes it. */
#define QOG_MAIN_DATA_OFFSET    0x00300000u
#define QOG_MAIN_FAT_OFFSET     0x00800000u
#define QOG_MAIN_APP_MAX_SIZE   QOG_MAIN_DATA_OFFSET
#define QOG_MAIN_DATA_MAX_SIZE  (QOG_MAIN_FAT_OFFSET - QOG_MAIN_DATA_OFFSET)
#define QOG_MAIN_DATA_XIP_ADDR  (QOG_XIP_BASE + QOG_MAIN_DATA_OFFSET)

#endif
