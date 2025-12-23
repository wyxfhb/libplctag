#include "buf5.h"
#include <string.h>

/* Helper to check if a slice is valid */
static inline bool is_valid(slice_t s) { return s.data != NULL; }

slice_t slice_head(slice_t s, size_t n) {
    if(!is_valid(s)) {
        return s;  // Propagate error
    }

    if(n > s.len) { return slice_set_err(UTIL_EBOUNDS); }
    return slice_make(s.data, n);
}

slice_t slice_tail(slice_t s, size_t n) {
    if(!is_valid(s)) {
        return s;  // Propagate error
    }

    if(n > s.len) { return slice_set_err(UTIL_EBOUNDS); }
    return slice_make(s.data + n, s.len - n);
}

/* --- Encoding (Write) --- */

#define CHECK_ENCODE(enc, avail, size)             \
    if(!avail || !enc) return false;               \
    if(!is_valid(*avail)) {                        \
        *enc = *avail;                             \
        return false;                              \
    }                                              \
    if(size > avail->len) {                        \
        slice_t err = slice_set_err(UTIL_EBOUNDS); \
        *avail = err;                              \
        *enc = err;                                \
        return false;                              \
    }

bool slice_encode_u8(slice_t *encoded, slice_t *available, uint8_t val) {
    CHECK_ENCODE(encoded, available, 1);

    available->data[0] = val;

    *encoded = slice_make(available->data, 1);
    *available = slice_make(available->data + 1, available->len - 1);
    return true;
}

bool slice_encode_u16_le(slice_t *encoded, slice_t *available, uint16_t val) {
    CHECK_ENCODE(encoded, available, 2);
    uint8_t *p = available->data;
    p[0] = (uint8_t)(val & 0xFF);
    p[1] = (uint8_t)((val >> 8) & 0xFF);

    *encoded = slice_make(p, 2);
    *available = slice_make(p + 2, available->len - 2);
    return true;
}

bool slice_encode_u16_be(slice_t *encoded, slice_t *available, uint16_t val) {
    CHECK_ENCODE(encoded, available, 2);
    uint8_t *p = available->data;
    p[0] = (uint8_t)((val >> 8) & 0xFF);
    p[1] = (uint8_t)(val & 0xFF);

    *encoded = slice_make(p, 2);
    *available = slice_make(p + 2, available->len - 2);
    return true;
}

bool slice_encode_u32_le(slice_t *encoded, slice_t *available, uint32_t val) {
    CHECK_ENCODE(encoded, available, 4);
    uint8_t *p = available->data;
    p[0] = (uint8_t)(val & 0xFF);
    p[1] = (uint8_t)((val >> 8) & 0xFF);
    p[2] = (uint8_t)((val >> 16) & 0xFF);
    p[3] = (uint8_t)((val >> 24) & 0xFF);

    *encoded = slice_make(p, 4);
    *available = slice_make(p + 4, available->len - 4);
    return true;
}

bool slice_encode_u32_be(slice_t *encoded, slice_t *available, uint32_t val) {
    CHECK_ENCODE(encoded, available, 4);
    uint8_t *p = available->data;
    p[0] = (uint8_t)((val >> 24) & 0xFF);
    p[1] = (uint8_t)((val >> 16) & 0xFF);
    p[2] = (uint8_t)((val >> 8) & 0xFF);
    p[3] = (uint8_t)(val & 0xFF);

    *encoded = slice_make(p, 4);
    *available = slice_make(p + 4, available->len - 4);
    return true;
}

bool slice_encode_u64_le(slice_t *encoded, slice_t *available, uint64_t val) {
    CHECK_ENCODE(encoded, available, 8);
    uint8_t *p = available->data;
    for(int i = 0; i < 8; i++) { p[i] = (uint8_t)((val >> (i * 8)) & 0xFF); }

    *encoded = slice_make(p, 8);
    *available = slice_make(p + 8, available->len - 8);
    return true;
}

bool slice_encode_u64_be(slice_t *encoded, slice_t *available, uint64_t val) {
    CHECK_ENCODE(encoded, available, 8);
    uint8_t *p = available->data;
    for(int i = 0; i < 8; i++) { p[7 - i] = (uint8_t)((val >> (i * 8)) & 0xFF); }

    *encoded = slice_make(p, 8);
    *available = slice_make(p + 8, available->len - 8);
    return true;
}

bool slice_encode_bytes(slice_t *encoded, slice_t *available, const uint8_t *data, size_t len) {
    CHECK_ENCODE(encoded, available, len);
    if(len > 0 && data) { memcpy(available->data, data, len); }
    *encoded = slice_make(available->data, len);
    *available = slice_make(available->data + len, available->len - len);
    return true;
}

/* --- Decoding (Read) --- */

#define CHECK_DECODE(dec, avail, size)             \
    if(!avail || !dec) return false;               \
    if(!is_valid(*avail)) {                        \
        *dec = *avail;                             \
        return false;                              \
    }                                              \
    if(size > avail->len) {                        \
        slice_t err = slice_set_err(UTIL_EBOUNDS); \
        *avail = err;                              \
        *dec = err;                                \
        return false;                              \
    }

bool slice_decode_u8(slice_t *decoded, slice_t *available, uint8_t *out) {
    CHECK_DECODE(decoded, available, 1);
    if(out) { *out = available->data[0]; }

    *decoded = slice_make(available->data, 1);
    *available = slice_make(available->data + 1, available->len - 1);
    return true;
}

/* (Other decode functions follow similar pattern...) */