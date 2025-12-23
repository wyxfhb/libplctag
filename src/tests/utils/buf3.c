#include "buf3.h"
#include <string.h>

slice_t slice_init(uint8_t *data, size_t size) {
    slice_t s;
    s.data = data;
    s.size = (data) ? size : 0;
    s.cursor = 0;
    s.error = (data) ? UTIL_OK : UTIL_ENULL;
    return s;
}

slice_t slice_split_front(slice_t *source, size_t len) {
    slice_t new_slice = {0};

    if(source->error != UTIL_OK) {
        new_slice.error = source->error;
        return new_slice;
    }

    if(len > source->size) {
        source->error = UTIL_EBOUNDS;
        new_slice.error = UTIL_EBOUNDS;
        return new_slice;
    }

    /* Create new slice from the front */
    new_slice.data = source->data;
    new_slice.size = len;
    new_slice.cursor = 0;
    new_slice.error = UTIL_OK;

    /* Advance the source (consume the front) */
    source->data += len;
    source->size -= len;
    /* Reset cursor of source because its data pointer moved */
    source->cursor = 0;

    return new_slice;
}

slice_t slice_split_back(slice_t *source, size_t len) {
    slice_t new_slice = {0};

    if(source->error != UTIL_OK) {
        new_slice.error = source->error;
        return new_slice;
    }

    if(len > source->size) {
        source->error = UTIL_EBOUNDS;
        new_slice.error = UTIL_EBOUNDS;
        return new_slice;
    }

    /* Create new slice from the back */
    new_slice.data = source->data + (source->size - len);
    new_slice.size = len;
    new_slice.cursor = 0;
    new_slice.error = UTIL_OK;

    /* Shrink the source (consume the back) */
    source->size -= len;
    /* Cursor might now be out of bounds if it was at the end, clamp it?
       Or just leave it, next write will fail if out of bounds. */
    if(source->cursor > source->size) { source->cursor = source->size; }

    return new_slice;
}

slice_t slice_reserve(slice_t *source, size_t len) {
    slice_t new_slice = {0};

    if(source->error != UTIL_OK) {
        new_slice.error = source->error;
        return new_slice;
    }

    if(source->cursor + len > source->size) {
        source->error = UTIL_EBOUNDS;
        new_slice.error = UTIL_EBOUNDS;
        return new_slice;
    }

    new_slice.data = source->data + source->cursor;
    new_slice.size = len;
    new_slice.cursor = 0;
    new_slice.error = UTIL_OK;

    source->cursor += len;
    return new_slice;
}

bool slice_ok(const slice_t *s) { return s && s->data && s->error == UTIL_OK; }

size_t slice_remaining(const slice_t *s) {
    if(!slice_ok(s) || s->cursor >= s->size) { return 0; }
    return s->size - s->cursor;
}

util_err_t slice_get_error(const slice_t *s) { return s ? s->error : UTIL_ENULL; }

void slice_reset_cursor(slice_t *s) {
    if(s) { s->cursor = 0; }
}

/* --- Read Implementations --- */

#define CHECK_READ(s, len)                            \
    if(!slice_ok(s) || s->cursor + (len) > s->size) { \
        if(s) s->error = UTIL_EBOUNDS;                \
        return false;                                 \
    }

bool slice_read_u8(slice_t *s, uint8_t *out) {
    CHECK_READ(s, 1);
    *out = s->data[s->cursor++];
    return true;
}

bool slice_read_u16_le(slice_t *s, uint16_t *out) {
    CHECK_READ(s, 2);
    uint8_t *p = s->data + s->cursor;
    *out = (uint16_t)p[0] | ((uint16_t)p[1] << 8);
    s->cursor += 2;
    return true;
}

bool slice_read_u16_be(slice_t *s, uint16_t *out) {
    CHECK_READ(s, 2);
    uint8_t *p = s->data + s->cursor;
    *out = ((uint16_t)p[0] << 8) | (uint16_t)p[1];
    s->cursor += 2;
    return true;
}

bool slice_read_u32_le(slice_t *s, uint32_t *out) {
    CHECK_READ(s, 4);
    uint8_t *p = s->data + s->cursor;
    *out = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    s->cursor += 4;
    return true;
}

bool slice_read_u32_be(slice_t *s, uint32_t *out) {
    CHECK_READ(s, 4);
    uint8_t *p = s->data + s->cursor;
    *out = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
    s->cursor += 4;
    return true;
}

bool slice_read_u64_le(slice_t *s, uint64_t *out) {
    CHECK_READ(s, 8);
    /* Simplified for brevity, assumes little-endian host or handles shifts */
    uint8_t *p = s->data + s->cursor;
    *out = 0;
    for(int i = 0; i < 8; i++) { *out |= ((uint64_t)p[i] << (i * 8)); }
    s->cursor += 8;
    return true;
}

bool slice_read_u64_be(slice_t *s, uint64_t *out) {
    CHECK_READ(s, 8);
    uint8_t *p = s->data + s->cursor;
    *out = 0;
    for(int i = 0; i < 8; i++) { *out = (*out << 8) | p[i]; }
    s->cursor += 8;
    return true;
}

bool slice_read_bytes(slice_t *s, uint8_t *out, size_t len) {
    CHECK_READ(s, len);
    memcpy(out, s->data + s->cursor, len);
    s->cursor += len;
    return true;
}

/* --- Write Implementations --- */

#define CHECK_WRITE(s, len)                           \
    if(!slice_ok(s) || s->cursor + (len) > s->size) { \
        if(s) s->error = UTIL_EBOUNDS;                \
        return false;                                 \
    }

bool slice_write_u8(slice_t *s, uint8_t val) {
    CHECK_WRITE(s, 1);
    s->data[s->cursor++] = val;
    return true;
}

bool slice_write_u16_le(slice_t *s, uint16_t val) {
    CHECK_WRITE(s, 2);
    uint8_t *p = s->data + s->cursor;
    p[0] = (uint8_t)(val & 0xFF);
    p[1] = (uint8_t)((val >> 8) & 0xFF);
    s->cursor += 2;
    return true;
}

bool slice_write_u16_be(slice_t *s, uint16_t val) {
    CHECK_WRITE(s, 2);
    uint8_t *p = s->data + s->cursor;
    p[0] = (uint8_t)((val >> 8) & 0xFF);
    p[1] = (uint8_t)(val & 0xFF);
    s->cursor += 2;
    return true;
}

bool slice_write_u32_le(slice_t *s, uint32_t val) {
    CHECK_WRITE(s, 4);
    uint8_t *p = s->data + s->cursor;
    p[0] = (uint8_t)(val & 0xFF);
    p[1] = (uint8_t)((val >> 8) & 0xFF);
    p[2] = (uint8_t)((val >> 16) & 0xFF);
    p[3] = (uint8_t)((val >> 24) & 0xFF);
    s->cursor += 4;
    return true;
}

bool slice_write_u32_be(slice_t *s, uint32_t val) {
    CHECK_WRITE(s, 4);
    uint8_t *p = s->data + s->cursor;
    p[0] = (uint8_t)((val >> 24) & 0xFF);
    p[1] = (uint8_t)((val >> 16) & 0xFF);
    p[2] = (uint8_t)((val >> 8) & 0xFF);
    p[3] = (uint8_t)(val & 0xFF);
    s->cursor += 4;
    return true;
}

bool slice_write_u64_le(slice_t *s, uint64_t val) {
    CHECK_WRITE(s, 8);
    uint8_t *p = s->data + s->cursor;
    for(int i = 0; i < 8; i++) { p[i] = (uint8_t)((val >> (i * 8)) & 0xFF); }
    s->cursor += 8;
    return true;
}

bool slice_write_u64_be(slice_t *s, uint64_t val) {
    CHECK_WRITE(s, 8);
    uint8_t *p = s->data + s->cursor;
    for(int i = 0; i < 8; i++) { p[7 - i] = (uint8_t)((val >> (i * 8)) & 0xFF); }
    s->cursor += 8;
    return true;
}

bool slice_write_bytes(slice_t *s, const uint8_t *data, size_t len) {
    CHECK_WRITE(s, len);
    if(len > 0 && data) {
        memcpy(s->data + s->cursor, data, len);
        s->cursor += len;
    }
    return true;
}