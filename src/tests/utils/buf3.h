#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "err.h"

/**
 * @brief Simplified Slice/View API.
 *
 * Concept:
 * A slice represents a window into memory.
 * You can "split" a slice to create sub-slices.
 * Splitting consumes the parent slice, making it act like a linear allocator.
 */
typedef struct slice_s {
    uint8_t *data;    /* Start of this slice's memory window */
    size_t size;      /* Size of this slice's memory window */
    size_t cursor;    /* Current read/write position relative to data */
    util_err_t error; /* Sticky error state */
} slice_t;

/* Initialization */
slice_t slice_init(uint8_t *data, size_t size);

/* Allocators / Splitters */

/**
 * @brief Carve a slice from the FRONT of the source.
 *
 * The 'source' slice is advanced by 'len' bytes, shrinking it.
 * Returns a new slice representing the consumed front portion.
 * Used for allocating headers or sequential fields.
 */
slice_t slice_split_front(slice_t *source, size_t len);

/**
 * @brief Carve a slice from the BACK of the source.
 *
 * The 'source' slice size is reduced by 'len' bytes.
 * Returns a new slice representing the consumed back portion.
 * Used for allocating footers or CRCs.
 */
slice_t slice_split_back(slice_t *source, size_t len);

/**
 * @brief Reserve space at the current cursor position.
 *
 * Returns a slice representing the reserved window [cursor, cursor+len).
 * Advances the source cursor by 'len'.
 * Used for writing headers that will be filled later, while keeping the stream contiguous.
 */
slice_t slice_reserve(slice_t *source, size_t len);

/* State */
bool slice_ok(const slice_t *s);
size_t slice_remaining(const slice_t *s);
util_err_t slice_get_error(const slice_t *s);
void slice_reset_cursor(slice_t *s);

/* Read Operations (Advances cursor) */
bool slice_read_u8(slice_t *s, uint8_t *out);
bool slice_read_u16_le(slice_t *s, uint16_t *out);
bool slice_read_u16_be(slice_t *s, uint16_t *out);
bool slice_read_u32_le(slice_t *s, uint32_t *out);
bool slice_read_u32_be(slice_t *s, uint32_t *out);
bool slice_read_u64_le(slice_t *s, uint64_t *out);
bool slice_read_u64_be(slice_t *s, uint64_t *out);
bool slice_read_bytes(slice_t *s, uint8_t *out, size_t len);

/* Write Operations (Advances cursor) */
bool slice_write_u8(slice_t *s, uint8_t val);
bool slice_write_u16_le(slice_t *s, uint16_t val);
bool slice_write_u16_be(slice_t *s, uint16_t val);
bool slice_write_u32_le(slice_t *s, uint32_t val);
bool slice_write_u32_be(slice_t *s, uint32_t val);
bool slice_write_u64_le(slice_t *s, uint64_t val);
bool slice_write_u64_be(slice_t *s, uint64_t val);
bool slice_write_bytes(slice_t *s, const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif