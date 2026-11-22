/* Read-in-place access to the game data region in flash.
 *
 * The region holds one QOGD image built on the PC by tools/mkdata.py from
 * the user's own files. Lumps are returned as XIP pointers: no copy, no RAM.
 *
 * Layout (all little-endian, matches tools/mkdata.py):
 *
 *   header, 64 bytes
 *     0  u32 magic        'QOGD' (0x44474F51)
 *     4  u32 version      QOGD_VERSION
 *     8  u32 header_size  64
 *    12  u32 entry_count
 *    16  u32 dir_offset   from region start
 *    20  u32 total_size   bytes, header included
 *    24  u32 body_crc32   zlib CRC32 of bytes [header_size, total_size)
 *    28  u32 align        lump alignment used by the packer
 *    32  char label[28]   free text, NUL padded
 *    60  u32 header_crc32 zlib CRC32 of bytes [0, 60)
 *
 *   directory at dir_offset: entry_count x 32 bytes
 *     0  char name[24]    NUL padded, case-sensitive
 *    24  u32  offset      from region start, multiple of `align`
 *    28  u32  size
 */
#ifndef QOG_DATA_H
#define QOG_DATA_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define QOGD_MAGIC        0x44474F51u   /* "QOGD" little-endian */
#define QOGD_VERSION      1u
#define QOGD_HEADER_SIZE  64u
#define QOGD_NAME_LEN     24u

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t header_size;
    uint32_t entry_count;
    uint32_t dir_offset;
    uint32_t total_size;
    uint32_t body_crc32;
    uint32_t align;
    char     label[28];
    uint32_t header_crc32;
} qogd_header_t;

typedef struct {
    char     name[QOGD_NAME_LEN];
    uint32_t offset;
    uint32_t size;
} qogd_entry_t;

_Static_assert(sizeof(qogd_header_t) == QOGD_HEADER_SIZE, "QOGD header is 64 bytes");
_Static_assert(sizeof(qogd_entry_t) == 32, "QOGD entry is 32 bytes");

typedef enum {
    QOG_DATA_OK = 0,
    QOG_DATA_EMPTY,        /* erased flash: nothing written yet */
    QOG_DATA_BAD_MAGIC,
    QOG_DATA_BAD_VERSION,
    QOG_DATA_BAD_HEADER,   /* header CRC or bounds */
    QOG_DATA_BAD_BODY      /* body CRC (only from qog_data_verify) */
} qog_data_status_t;

/* Pure: validate a header against the space it lives in. Host-testable. */
qog_data_status_t qog_data_check_header(const qogd_header_t *h, uint32_t region_size);

/* Check the header in flash. Cheap; call at boot. */
qog_data_status_t qog_data_open(void);

/* CRC the whole body. Reads every byte of the image through XIP, so it costs
 * real time (it reports how long via *elapsed_us if non-NULL). */
qog_data_status_t qog_data_verify(uint32_t *elapsed_us);

const qogd_header_t *qog_data_header(void);

/* Find a lump by exact name. Returns its XIP address and sets *size, or
 * NULL if the region is not open or the name is absent. */
const void *qog_data_find(const char *name, uint32_t *size);

/* Entry by index, for listing. NULL past the end. */
const qogd_entry_t *qog_data_entry(uint32_t index);

const char *qog_data_status_text(qog_data_status_t s);

#endif
