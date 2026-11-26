/* Display-CPU flash map for QuakeOG.
 *
 *   0x000000  BSP serial bootloader     128 KB  (never touch; BSP rule)
 *   0x020000  BSP app metadata            4 KB  (written only by the bootloader)
 *   0x021000  QuakeOG application      892 KB  (QOG_APP_MAX_SIZE 913,408 B, checked post-build)
 *   0x100000  game data region        14.94 MB  (pak and baked image, or a QOGD image)
 *   0xFF0000  save slots                 64 KB  (QOG_SAVE_SIZE)
 *
 * Why this works: a display update erases only the sectors the new image
 * needs (wiliOGbsp bootloader/bl_receiver.c, "Erase only the sectors this
 * image needs"), so a code update never touches the data region as long as
 * the app stays under QOG_APP_MAX_SIZE. Every display app's build runs
 * tools/check_app_size.py, which fails when it does not.
 *
 * Earlier work, the data moved from 0x400000 to 0x100000 so start and episode 1
 * fit (13.19 MB without sounds and progs.dat, earlier work). The display app is
 * about 360 KB.
 *
 * tools/mkdata.py reads these offsets from this file (one source of truth
 * for the tools), port/pc/sys_pc.c maps the PC's packs at QOG_DATA_XIP_ADDR. */
#ifndef QOG_FLASHMAP_H
#define QOG_FLASHMAP_H

#define QOG_XIP_BASE        0x10000000u
#define QOG_FLASH_SIZE      (16u * 1024u * 1024u)

#define QOG_APP_OFFSET      0x00021000u
#define QOG_DATA_OFFSET     0x00100000u
#define QOG_SAVE_OFFSET     0x00FF0000u

#define QOG_APP_MAX_SIZE    (QOG_DATA_OFFSET - QOG_APP_OFFSET)
#define QOG_DATA_MAX_SIZE   (QOG_SAVE_OFFSET - QOG_DATA_OFFSET)
#define QOG_SAVE_SIZE       (QOG_FLASH_SIZE - QOG_SAVE_OFFSET)

#define QOG_DATA_XIP_ADDR   (QOG_XIP_BASE + QOG_DATA_OFFSET)

/* Main-CPU flash map for the split game, the server half runs on
 * the main RP2040, which has its own 16 MB flash.
 *
 *   0x000000  quake_main, the display image embedded in it   2 MB (QOG_MAIN_APP_MAX_SIZE, checked post-build)
 *   0x200000  server data region                             6 MB (pak and baked image for this address)
 *   0x800000  BSP FAT volume                                 8 MB (fs/fs_geom.h FWOG_FS_FLASH_OFFSET, never touch)
 *
 * The main app is linked by the SDK at the start of flash (fwog_main_app adds
 * no offset). The server data is written with a data-only UF2 through main's
 * BOOTSEL drive, it covers only 0x10200000 to 0x107FFFFF, so the app and the
 * FAT volume keep their bytes. tools/mkboard.py --main writes it and fails
 * when the image would reach the FAT volume.
 *
 * Earlier work, the data moved from 0x300000 to 0x200000, so episode 1 with its
 * sounds fits (5.75 MB, earlier work). quake_main is about 1.3 MB. */
#define QOG_MAIN_DATA_OFFSET    0x00200000u
#define QOG_MAIN_FAT_OFFSET     0x00800000u
#define QOG_MAIN_APP_MAX_SIZE   QOG_MAIN_DATA_OFFSET
#define QOG_MAIN_DATA_MAX_SIZE  (QOG_MAIN_FAT_OFFSET - QOG_MAIN_DATA_OFFSET)
#define QOG_MAIN_DATA_XIP_ADDR  (QOG_XIP_BASE + QOG_MAIN_DATA_OFFSET)

/* The regions must not overlap, the display bootloader's 128 KB and the app
 * metadata stay below the app, the app below the data, the data below the
 * saves, and on main the app below the data and the data below the FAT
 * volume (8 MB in, 8 MB long, the BSP's fs/fs_geom.h). Regions start on
 * 64 KB erase blocks. */
#if QOG_APP_OFFSET < 0x00021000u || QOG_DATA_OFFSET <= QOG_APP_OFFSET || QOG_SAVE_OFFSET <= QOG_DATA_OFFSET \
	|| QOG_SAVE_OFFSET > QOG_FLASH_SIZE || (QOG_DATA_OFFSET & 0xFFFFu) || (QOG_SAVE_OFFSET & 0xFFFFu)
#error "qog_flashmap.h: the display regions overlap or are not 64 KB aligned"
#endif
#if QOG_MAIN_DATA_OFFSET == 0u || QOG_MAIN_FAT_OFFSET <= QOG_MAIN_DATA_OFFSET \
	|| QOG_MAIN_FAT_OFFSET != 0x00800000u || (QOG_MAIN_DATA_OFFSET & 0xFFFFu)
#error "qog_flashmap.h: the main regions overlap, are not 64 KB aligned, or the FAT volume moved"
#endif

#endif
