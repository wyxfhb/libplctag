/* /home/kyle/Projects/libplctag/src/tests/utils/buf4.h */
#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "err.h"

/*
 * ============================================================================
 * 1. The Slice (View)
 * ============================================================================
 * A slice is a lightweight, immutable description of a memory region.
 * It contains NO state (cursors, errors). It is just a pointer and a length.
 * It is intended to be passed by value.
 */
typedef struct slice_s {
    uint8_t *data;
    size_t len;
} slice_t;

/* Constructors */
static inline slice_t slice_make(uint8_t *data, size_t len) {
    slice_t s = {data, len};
    return s;
}

static inline slice_t slice_empty(void) {
    slice_t s = {NULL, 0};
    return s;
}


static inline slice_t slice_set_err(util_err_t error) {
    slice_t result = {NULL, (size_t)(uintptr_t)error};
    return result;
}

static inline util_err_t slice_get_err(slice_t s) {
    if(!s.data) { return (util_err_t)(uintptr_t)s.len; }
    return UTIL_OK;
}

/*
 * Slicing Operations (Functional)
 * These return NEW slices. They do not modify the input slice.
 */


/**
 * @brief Get the first 'n' bytes.
 * Useful for isolating headers.
 */
slice_t slice_head(slice_t s, size_t n);

/**
 * @brief Get the remaining bytes after 'n'.
 * Useful for getting the payload after a header.
 */
slice_t slice_tail(slice_t s, size_t n);


/* Write operations -  Functions change the written slice to include the data
 * just written and remove space from the available slice
 */
bool slice_encode_u8(slice_t *encoded, slice_t *available, uint8_t val);
bool slice_encode_u16_le(slice_t *encoded, slice_t *available, uint16_t val);
bool slice_encode_u16_be(slice_t *encoded, slice_t *available, uint16_t val);
bool slice_encode_u32_le(slice_t *encoded, slice_t *available, uint32_t val);
bool slice_encode_u32_be(slice_t *encoded, slice_t *available, uint32_t val);
bool slice_encode_u64_le(slice_t *encoded, slice_t *available, uint64_t val);
bool slice_encode_u64_be(slice_t *encoded, slice_t *available, uint64_t val);
bool slice_encode_bytes(slice_t *encoded, slice_t *available, const uint8_t *data, size_t len);

/* Read operations - these advance the cursor inside the reader */
bool slice_decode_u8(slice_t *decoded, slice_t *available, uint8_t *out);
bool slice_decode_u16_le(slice_t *decoded, slice_t *available, uint16_t *out);
bool slice_decode_u16_be(slice_t *decoded, slice_t *available, uint16_t *out);
bool slice_decode_u32_le(slice_t *decoded, slice_t *available, uint32_t *out);
bool slice_decode_u32_be(slice_t *decoded, slice_t *available, uint32_t *out);
bool slice_decode_u64_le(slice_t *decoded, slice_t *available, uint64_t *out);
bool slice_decode_u64_be(slice_t *decoded, slice_t *available, uint64_t *out);
bool slice_decode_bytes(slice_t *decoded, slice_t *available, uint8_t *out, size_t len);

#ifdef __cplusplus
}
#endif
