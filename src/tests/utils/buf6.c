#include "buf6.h"
#include <string.h>

/* --- Reader Implementation --- */

reader_t reader_init(uint8_t *buf, size_t size) {
    reader_t r;
    r.base = buf;
    r.size = (buf) ? size : 0;
    r.cursor = 0;
    r.err = (buf) ? 0 : -1; /* Use appropriate error code */
    return r;
}

size_t reader_remaining(const reader_t *r) {
    if(r->cursor >= r->size) { return 0; }
    return r->size - r->cursor;
}

bool reader_ok(const reader_t *r) { return r->err == 0; }

bool reader_set_err(reader_t *r, int err_code) {
    if(r->err == 0) { r->err = err_code; }
    return false;
}

int reader_get_err(const reader_t *r) { return r->err; }

#define CHECK_READ(r, n)            \
    if((r)->err != 0) return false; \
    if((r)->cursor + (n) > (r)->size) return reader_set_err(r, 1) /* UTIL_EBOUNDS */

bool reader_read_u8(reader_t *r, uint8_t *out) {
    CHECK_READ(r, 1);
    if(out) { *out = r->base[r->cursor]; }
    r->cursor++;
    return true;
}

bool reader_read_u16_le(reader_t *r, uint16_t *out) {
    CHECK_READ(r, 2);
    uint8_t *p = r->base + r->cursor;
    if(out) { *out = (uint16_t)p[0] | ((uint16_t)p[1] << 8); }
    r->cursor += 2;
    return true;
}

bool reader_read_u16_be(reader_t *r, uint16_t *out) {
    CHECK_READ(r, 2);
    uint8_t *p = r->base + r->cursor;
    if(out) { *out = ((uint16_t)p[0] << 8) | (uint16_t)p[1]; }
    r->cursor += 2;
    return true;
}

bool reader_read_u32_le(reader_t *r, uint32_t *out) {
    CHECK_READ(r, 4);
    uint8_t *p = r->base + r->cursor;
    if(out) { *out = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
    r->cursor += 4;
    return true;
}

bool reader_read_u32_be(reader_t *r, uint32_t *out) {
    CHECK_READ(r, 4);
    uint8_t *p = r->base + r->cursor;
    if(out) { *out = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3]; }
    r->cursor += 4;
    return true;
}

bool reader_read_u64_le(reader_t *r, uint64_t *out) {
    CHECK_READ(r, 8);
    uint8_t *p = r->base + r->cursor;
    if(out) {
        *out = 0;
        for(int i = 0; i < 8; i++) { *out |= ((uint64_t)p[i] << (i * 8)); }
    }
    r->cursor += 8;
    return true;
}

bool reader_read_u64_be(reader_t *r, uint64_t *out) {
    CHECK_READ(r, 8);
    uint8_t *p = r->base + r->cursor;
    if(out) {
        *out = 0;
        for(int i = 0; i < 8; i++) { *out = (*out << 8) | p[i]; }
    }
    r->cursor += 8;
    return true;
}

bool reader_read_bytes(reader_t *r, uint8_t *out, size_t len) {
    CHECK_READ(r, len);
    if(out) { memcpy(out, r->base + r->cursor, len); }
    r->cursor += len;
    return true;
}

/* --- Writer Implementation --- */

writer_t writer_init(uint8_t *buf, size_t size) {
    writer_t w;
    w.base = buf;
    w.size = (buf) ? size : 0;
    w.cursor = 0;
    w.err = (buf) ? 0 : -1;
    return w;
}

size_t writer_remaining(const writer_t *w) {
    if(w->cursor >= w->size) { return 0; }
    return w->size - w->cursor;
}

bool writer_ok(const writer_t *w) { return w->err == 0; }

bool writer_set_err(writer_t *w, int err_code) {
    if(w->err == 0) { w->err = err_code; }
    return false;
}

int writer_get_err(const writer_t *w) { return w->err; }

size_t writer_get_cursor(const writer_t *w) { return w->cursor; }

bool writer_set_cursor(writer_t *w, size_t cursor) {
    if(cursor > w->size) { return writer_set_err(w, 1); }
    w->cursor = cursor;
    return true;
}

#define CHECK_WRITE(w, n)           \
    if((w)->err != 0) return false; \
    if((w)->cursor + (n) > (w)->size) return writer_set_err(w, 1)

bool writer_write_u8(writer_t *w, uint8_t val) {
    CHECK_WRITE(w, 1);
    w->base[w->cursor++] = val;
    return true;
}

bool writer_write_u16_le(writer_t *w, uint16_t val) {
    CHECK_WRITE(w, 2);
    uint8_t *p = w->base + w->cursor;
    p[0] = (uint8_t)(val & 0xFF);
    p[1] = (uint8_t)((val >> 8) & 0xFF);
    w->cursor += 2;
    return true;
}

bool writer_write_u16_be(writer_t *w, uint16_t val) {
    CHECK_WRITE(w, 2);
    uint8_t *p = w->base + w->cursor;
    p[0] = (uint8_t)((val >> 8) & 0xFF);
    p[1] = (uint8_t)(val & 0xFF);
    w->cursor += 2;
    return true;
}

bool writer_write_u32_le(writer_t *w, uint32_t val) {
    CHECK_WRITE(w, 4);
    uint8_t *p = w->base + w->cursor;
    p[0] = (uint8_t)(val & 0xFF);
    p[1] = (uint8_t)((val >> 8) & 0xFF);
    p[2] = (uint8_t)((val >> 16) & 0xFF);
    p[3] = (uint8_t)((val >> 24) & 0xFF);
    w->cursor += 4;
    return true;
}

bool writer_write_u32_be(writer_t *w, uint32_t val) {
    CHECK_WRITE(w, 4);
    uint8_t *p = w->base + w->cursor;
    p[0] = (uint8_t)((val >> 24) & 0xFF);
    p[1] = (uint8_t)((val >> 16) & 0xFF);
    p[2] = (uint8_t)((val >> 8) & 0xFF);
    p[3] = (uint8_t)(val & 0xFF);
    w->cursor += 4;
    return true;
}

bool writer_write_u64_le(writer_t *w, uint64_t val) {
    CHECK_WRITE(w, 8);
    uint8_t *p = w->base + w->cursor;
    for(int i = 0; i < 8; i++) { p[i] = (uint8_t)((val >> (i * 8)) & 0xFF); }
    w->cursor += 8;
    return true;
}

bool writer_write_u64_be(writer_t *w, uint64_t val) {
    CHECK_WRITE(w, 8);
    uint8_t *p = w->base + w->cursor;
    for(int i = 0; i < 8; i++) { p[7 - i] = (uint8_t)((val >> (i * 8)) & 0xFF); }
    w->cursor += 8;
    return true;
}

bool writer_write_bytes(writer_t *w, const uint8_t *data, size_t len) {
    CHECK_WRITE(w, len);
    if(len > 0 && data) {
        memcpy(w->base + w->cursor, data, len);
        w->cursor += len;
    }
    return true;
}