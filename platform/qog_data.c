#include "qog_data.h"
#include <string.h>
#include "qog_flashmap.h"
#include "common/crc.h"

#ifndef HOST_TEST
#include "pico/time.h"
#endif

qog_data_status_t qog_data_check_header(const qogd_header_t *h, uint32_t region_size) {
    if (h->magic == 0xFFFFFFFFu) return QOG_DATA_EMPTY;
    if (h->magic != QOGD_MAGIC) return QOG_DATA_BAD_MAGIC;
    if (h->version != QOGD_VERSION) return QOG_DATA_BAD_VERSION;
    if (fwog_crc32(h, offsetof(qogd_header_t, header_crc32)) != h->header_crc32)
        return QOG_DATA_BAD_HEADER;
    if (h->header_size != QOGD_HEADER_SIZE) return QOG_DATA_BAD_HEADER;
    if (h->total_size < QOGD_HEADER_SIZE || h->total_size > region_size)
        return QOG_DATA_BAD_HEADER;
    /* Directory must sit wholly inside the image. 64-bit to dodge overflow
       on a hostile entry_count. */
    const uint64_t dir_end = (uint64_t)h->dir_offset +
                             (uint64_t)h->entry_count * sizeof(qogd_entry_t);
    if (h->dir_offset < QOGD_HEADER_SIZE || dir_end > h->total_size)
        return QOG_DATA_BAD_HEADER;
    if (h->align == 0u || (h->align & (h->align - 1u)) != 0u)
        return QOG_DATA_BAD_HEADER;
    return QOG_DATA_OK;
}

#ifndef HOST_TEST

static const uint8_t *const s_base = (const uint8_t *)QOG_DATA_XIP_ADDR;
static bool s_open;

static const qogd_header_t *hdr(void) { return (const qogd_header_t *)s_base; }

qog_data_status_t qog_data_open(void) {
    const qog_data_status_t st = qog_data_check_header(hdr(), QOG_DATA_MAX_SIZE);
    s_open = (st == QOG_DATA_OK);
    return st;
}

qog_data_status_t qog_data_verify(uint32_t *elapsed_us) {
    const qog_data_status_t st = qog_data_open();
    if (st != QOG_DATA_OK) return st;
    const uint64_t t0 = time_us_64();
    const qogd_header_t *h = hdr();
    const uint32_t crc = fwog_crc32(s_base + h->header_size, h->total_size - h->header_size);
    if (elapsed_us) *elapsed_us = (uint32_t)(time_us_64() - t0);
    if (crc != h->body_crc32) {
        s_open = false;
        return QOG_DATA_BAD_BODY;
    }
    return QOG_DATA_OK;
}

const qogd_header_t *qog_data_header(void) { return s_open ? hdr() : NULL; }

const qogd_entry_t *qog_data_entry(uint32_t index) {
    if (!s_open || index >= hdr()->entry_count) return NULL;
    return (const qogd_entry_t *)(s_base + hdr()->dir_offset) + index;
}

const void *qog_data_find(const char *name, uint32_t *size) {
    if (!s_open || !name) return NULL;
    const size_t n = strlen(name);
    if (n == 0u || n > QOGD_NAME_LEN) return NULL;
    const qogd_entry_t *e = (const qogd_entry_t *)(s_base + hdr()->dir_offset);
    for (uint32_t i = 0; i < hdr()->entry_count; i++, e++) {
        if (memcmp(e->name, name, n) == 0 && (n == QOGD_NAME_LEN || e->name[n] == '\0')) {
            if ((uint64_t)e->offset + e->size > hdr()->total_size) return NULL;
            if (size) *size = e->size;
            return s_base + e->offset;
        }
    }
    return NULL;
}

#endif /* HOST_TEST */

const char *qog_data_status_text(qog_data_status_t s) {
    switch (s) {
    case QOG_DATA_OK:          return "ok";
    case QOG_DATA_EMPTY:       return "empty (no data written)";
    case QOG_DATA_BAD_MAGIC:   return "bad magic";
    case QOG_DATA_BAD_VERSION: return "wrong version";
    case QOG_DATA_BAD_HEADER:  return "bad header";
    case QOG_DATA_BAD_BODY:    return "body CRC mismatch";
    }
    return "?";
}
