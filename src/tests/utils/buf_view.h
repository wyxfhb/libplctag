#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "err.h"

/**
 * @brief A safe, slice-based buffer view abstraction.
 *
 * Unlike buf_t, this structure represents a specific "window" into memory.
 * It has a single cursor relative to the start of its window.
 *
 * Key concept: Slicing.
 * Instead of resetting cursors, you create new views (slices) from existing ones.
 *
 * Example (Writing Nested Headers):
 *   buf_view_t packet = buf_view_init(buffer, 1024);
 *
 *   // 1. Reserve space for Outer Header
 *   buf_view_t outer_hdr = buf_view_slice(&packet, OUTER_HDR_SIZE);
 *
 *   // 2. Reserve space for Inner Header (packet cursor has already moved past outer)
 *   buf_view_t inner_hdr = buf_view_slice(&packet, INNER_HDR_SIZE);
 *
 *   // 3. Write Payload (packet cursor is now past both headers)
 *   buf_view_write_bytes(&packet, payload_data, payload_len);
 *
 *   // 4. Fill headers (using the isolated views created earlier)
 *   buf_view_write_u16_le(&inner_hdr, payload_len);
 *   buf_view_write_u32_le(&outer_hdr, total_len);
 */
typedef struct buf_view_s {
    uint8_t *data;    /* Pointer to the start of this view's window */
    size_t capacity;  /* Size of this view's window */
    size_t cursor;    /* Current position relative to data */
    util_err_t error; /* Sticky error state */
} buf_view_t;

/* Initialization */
buf_view_t buf_view_init(uint8_t *data, size_t capacity);

/* Slicing / Reservation */

/**
 * @brief Create a sub-view of the next 'len' bytes and advance the parent's cursor.
 *
 * Used for:
 * - Reading: "Peeling off" a header to parse, leaving the payload in the parent view.
 * - Writing: Reserving space for a header to be filled later, advancing parent to payload area.
 *
 * @param v Parent view.
 * @param len Number of bytes to slice/reserve.
 * @return buf_view_t New view representing the slice. On error, has error flag set.
 */
buf_view_t buf_view_slice(buf_view_t *v, size_t len);

/**
 * @brief Create a sub-view of all remaining bytes and advance parent cursor to end.
 */
buf_view_t buf_view_slice_remaining(buf_view_t *v);

/* State Checks */
bool buf_view_ok(const buf_view_t *v);
size_t buf_view_remaining(const buf_view_t *v);
size_t buf_view_pos(const buf_view_t *v);
util_err_t buf_view_get_error(const buf_view_t *v);

/* Cursor Management */
bool buf_view_seek(buf_view_t *v, size_t pos);
bool buf_view_advance(buf_view_t *v, size_t n);

/* Read Operations (Advances cursor) */
bool buf_view_read_u8(buf_view_t *v, uint8_t *out);
bool buf_view_read_u16_le(buf_view_t *v, uint16_t *out);
bool buf_view_read_u16_be(buf_view_t *v, uint16_t *out);
bool buf_view_read_u32_le(buf_view_t *v, uint32_t *out);
bool buf_view_read_u32_be(buf_view_t *v, uint32_t *out);
bool buf_view_read_u64_le(buf_view_t *v, uint64_t *out);
bool buf_view_read_u64_be(buf_view_t *v, uint64_t *out);
bool buf_view_read_bytes(buf_view_t *v, uint8_t *out, size_t len);

/* Write Operations (Advances cursor) */
bool buf_view_write_u8(buf_view_t *v, uint8_t val);
bool buf_view_write_u16_le(buf_view_t *v, uint16_t val);
bool buf_view_write_u16_be(buf_view_t *v, uint16_t val);
bool buf_view_write_u32_le(buf_view_t *v, uint32_t val);
bool buf_view_write_u32_be(buf_view_t *v, uint32_t val);
bool buf_view_write_u64_le(buf_view_t *v, uint64_t val);
bool buf_view_write_u64_be(buf_view_t *v, uint64_t val);
bool buf_view_write_bytes(buf_view_t *v, const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif