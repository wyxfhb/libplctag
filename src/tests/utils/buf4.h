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

/*
 * Slicing Operations (Functional)
 * These return NEW slices. They do not modify the input slice.
 */

/**
 * @brief Create a sub-slice.
 * @return A new slice representing the window, or empty if out of bounds.
 */
slice_t slice_sub(slice_t s, size_t offset, size_t len);

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

/**
 * @brief Split a slice into two parts.
 *
 * @param s The source slice.
 * @param split_point The offset to split at.
 * @param out_head Receives the first 'split_point' bytes.
 * @param out_tail Receives the remainder.
 * @return true if split was within bounds, false otherwise (outputs set to empty/remainder).
 */
bool slice_split(slice_t s, size_t split_point, slice_t *out_head, slice_t *out_tail);


/*
 * ============================================================================
 * 2. The Writer (State)
 * ============================================================================
 * Holds the mutable state (cursor, error) for writing into a slice.
 * This wraps a slice to provide sequential write capabilities.
 */
typedef struct slice_writer_s {
    slice_t slice;    /* The memory being written to */
    size_t cursor;    /* Current write position */
    util_err_t error; /* Sticky error state */
} slice_writer_t;

void writer_init(slice_writer_t *w, slice_t s);

/* State checks */
size_t writer_pos(const slice_writer_t *w);
size_t writer_remaining(const slice_writer_t *w);
util_err_t writer_get_error(const slice_writer_t *w);

/* Write operations - these advance the cursor inside the writer */
bool writer_write_u8(slice_writer_t *w, uint8_t val);
bool writer_write_u16_le(slice_writer_t *w, uint16_t val);
bool writer_write_u16_be(slice_writer_t *w, uint16_t val);
bool writer_write_u32_le(slice_writer_t *w, uint32_t val);
bool writer_write_u32_be(slice_writer_t *w, uint32_t val);
bool writer_write_u64_le(slice_writer_t *w, uint64_t val);
bool writer_write_u64_be(slice_writer_t *w, uint64_t val);
bool writer_write_bytes(slice_writer_t *w, const uint8_t *data, size_t len);

/**
 * @brief Reserve space in the stream for later filling.
 *
 * Advances the writer's cursor by 'len' and returns a slice representing
 * the skipped region. This allows you to write a header *after* writing
 * the payload, without seeking.
 */
slice_t writer_reserve(slice_writer_t *w, size_t len);


/*
 * ============================================================================
 * 3. The Reader (State)
 * ============================================================================
 * Holds the mutable state (cursor, error) for reading from a slice.
 */
typedef struct slice_reader_s {
    slice_t slice;    /* The memory being read from */
    size_t cursor;    /* Current read position */
    util_err_t error; /* Sticky error state */
} slice_reader_t;

void reader_init(slice_reader_t *r, slice_t s);

/* State checks */
size_t reader_pos(const slice_reader_t *r);
size_t reader_remaining(const slice_reader_t *r);
util_err_t reader_get_error(const slice_reader_t *r);

/* Read operations - these advance the cursor inside the reader */
bool reader_read_u8(slice_reader_t *r, uint8_t *out);
bool reader_read_u16_le(slice_reader_t *r, uint16_t *out);
bool reader_read_u16_be(slice_reader_t *r, uint16_t *out);
bool reader_read_u32_le(slice_reader_t *r, uint32_t *out);
bool reader_read_u32_be(slice_reader_t *r, uint32_t *out);
bool reader_read_u64_le(slice_reader_t *r, uint64_t *out);
bool reader_read_u64_be(slice_reader_t *r, uint64_t *out);
bool reader_read_bytes(slice_reader_t *r, uint8_t *out, size_t len);

/**
 * @brief Read a sub-slice from the stream.
 *
 * Returns a slice of the next 'len' bytes and advances the cursor.
 * Useful for parsing nested structures without copying data.
 */
slice_t reader_read_slice(slice_reader_t *r, size_t len);

#ifdef __cplusplus
}
#endif
