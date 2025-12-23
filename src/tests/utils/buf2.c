#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "err.h"

/**
 * @brief Buffer structure and operations.
 *
 * Buffers are used for managing byte arrays with read and write cursors.
 * They also carry error status for operations.  Most operations return
 * a boolean indicating success or failure, with details available in
 * the buffer's error fields.  This allows chaining multiple operations
 * while checking for errors at the end.
 *
 * bool ok = true;
 *
 * ok &= buf_view_splice_bytes(&buf, 0, data1, len1);
 * ok &= buf_view_write_u32_le(&buf, value);
 * ...
 *
 * if(!ok) {
 *     // handle error
 * }
 *
 * Once a buffer has an error set, further operations are no-ops
 * until the error is cleared.
 */

typedef struct buf_view_s {
    uint8_t *data;
    size_t capacity;  // total buffer capacity
    size_t start;     // start position of the buffer data
    size_t end;       // end position of the buffer data
    size_t cursor;    // read or write cursor
    util_err_t error;
    char *failed_field;
} buf_view_t;


/**
 * @brief sets the start position of the buffer data to the cursor.
 *
 * @param buf
 * @param start
 * @return true - if successful
 * @return false - if out of bounds or null pointer etc.
 */
extern bool buf_view_set_start_to_cursor(buf_view_t *buf, size_t start);

/**
 * @brief sets the end position of the buffer data to the cursor.
 *
 * @param buf
 * @param end
 * @return true
 * @return false
 */
extern bool buf_view_set_end_to_cursor(buf_view_t *buf, size_t end);


/**
 * @brief get the size of the data in the buffer (end - start).
 *
 * @param buf
 * @return size_t
 */
extern size_t buf_view_get_data_size(buf_view_t *buf);

/**
 * @brief
 *
 * @param buf
 * @return uint8_t*
 */
extern uint8_t *buf_view_get_data_ptr(buf_view_t *buf);


/**
 * @brief compresses the buffer by moving the valid data to the start of the buffer.
 *
 * @param buf
 * @return true
 * @return false
 */
extern bool buf_view_compress(buf_view_t *buf);


/**
 * @brief seeks the cursor to a position in the buffer.
 *
 * @param buf
 * @param position
 * @return true
 * @return false
 */
extern bool buf_view_seek(buf_view_t *buf, size_t position);


/**
 * @brief get current position of the cursor in the buffer.
 *
 * @param buf
 * @return size_t
 */
extern size_t buf_view_tell(buf_view_t *buf);

/* Creation/Initialization */

/**
 * @brief Initialize a new buffer.
 *
 * @param data Pointer to the buffer data.
 * @param capacity Total capacity of the buffer.
 * @return buf_view_t The initialized buffer, by value, not pointer.
 */
buf_view_t buf_view_init(uint8_t *data, size_t capacity);


/* Error handling */

/**
 * @brief Set the error status of the buffer.
 *
 * @param b Pointer to the buffer.
 * @param error Error status to set.
 * @param field_name Name of the field that caused the error.
 */
void buf_view_set_error(buf_view_t *b, util_err_t error, const char *field_name);

/**
 * @brief Clear the error status of the buffer.
 *
 * Clear both the error code and failed field name.
 *
 * @param b Pointer to the buffer.
 * @return util_err_t Previous error status.
 */
util_err_t buf_view_clear_error(buf_view_t *b);


/**
 * @brief Get the current error status of the buffer.
 *
 * @param b Pointer to the buffer.
 * @return util_err_t Current error status.
 */
util_err_t buf_view_get_error(buf_view_t *b);

/**
 * @brief Get the name of the field that caused the last error.
 *
 * @param b Pointer to the buffer.
 * @return char* Name of the failed field or NULL if no error.
 */
char *buf_view_get_failed_field(buf_view_t *b);

/**
 * @brief Check if the buffer is in a good state (no errors).
 *
 * Also checks for invalid state such as read/write cursors out of bounds.
 *
 * @param b Pointer to the buffer.
 * @return true if no errors, false otherwise.
 */
bool buf_view_ok(buf_view_t *b);


/* Status/Access */

/**
 * @brief Get the total capacity of the buffer.
 *
 * @param b Pointer to the buffer.
 * @return size_t Total capacity of the buffer or zero if b is NULL.
 */
size_t buf_view_get_capacity(buf_view_t *b);


/**
 * @brief Cut a portion of the buffer.
 *
 * This removes a portion of the buffer starting at the given offset
 * and spanning n bytes.  The remaining data is shifted down to fill
 * the gap.
 *
 * @param b Pointer to the buffer.
 * @param offset Offset from the beginning of the buffer.
 * @param n Number of bytes to cut.
 * @return true if the operation was successful, false otherwise.
 */
bool buf_view_cut(buf_view_t *b, size_t offset, size_t n);

/**
 * @brief Splice data into the buffer.
 *
 * This inserts data into the buffer at the given offset from the
 * read index, shifting existing data up to make room.
 *
 * @param dest Pointer to the buffer.
 * @param dest_offset Offset from the read index of the buffer.
 * @param src Pointer to the source buffer.
 * @param src_offset Offset from the read index of the source buffer.
 * @param n Number of bytes to splice.
 * @return true if the operation was successful, false otherwise.
 */
bool buf_view_splice_buf(buf_view_t *dest, size_t dest_offset, buf_view_t *src, size_t src_offset, size_t n);

/**
 * @brief Splice bytes into the buffer.
 *
 * This inserts data into the buffer at the given offset from the
 * read index, shifting existing data up to make room.
 *
 * @param b Pointer to the buffer.
 * @param offset Offset from the read index of the buffer.
 * @param data Pointer to the data to splice in.
 * @param n Number of bytes to splice.
 * @return true if the operation was successful, false otherwise.
 */
bool buf_view_splice_bytes(buf_view_t *b, size_t offset, uint8_t *data, size_t n);


/**
 * @brief Save a checkpoint of the buffer state.
 *
 * Creates a snapshot of the buffer's current state (read/write cursors and error status).
 * This can be used with buf_view_restore() to revert the buffer to a previous state.
 * Useful for transactional parsing where you want to attempt parsing and restore
 * the buffer if parsing fails (e.g., incomplete data).
 *
 * Example:
 *   buf_view_t checkpoint = buf_view_checkpoint(buf);
 *   if (!try_parse(buf, result)) {
 *       buf_view_restore(buf, checkpoint);  // Revert to checkpoint
 *       return INCOMPLETE;
 *   }
 *
 * @param b Pointer to the buffer.
 * @return buf_view_t A snapshot of the buffer state at the time of the call.
 */
static inline buf_view_t buf_view_checkpoint(buf_view_t *b) { return *b; }


/**
 * @brief Restore a buffer to a previously saved checkpoint state.
 *
 * Reverts the buffer to the state captured by buf_view_checkpoint().
 * This restores the read/write cursors and error status.
 *
 * @param b Pointer to the buffer to restore.
 * @param checkpoint The checkpoint state to restore to (from buf_view_checkpoint()).
 */
static inline void buf_view_restore(buf_view_t *b, buf_view_t checkpoint) { *b = checkpoint; }

/* Buffer read/write */


/**
 * @brief The following are convenience functions for reading and writing.
 *
 * They return true on success, false on failure.  On failure, the buffer's
 * error status is set to indicate the type of error that occurred and the buffer is
 * left in the state it was in before the call, apart from the error state.
 *
 * All will fail if the buffer is already in an error state.
 *
 * The field_name parameter is used to indicate which field caused the error.
 *
 * Reads and write advance the cursor appropriately on success.
 */

/* Reads */
bool buf_view_read_u8(buf_view_t *b, const char *field_name, uint8_t *out);
bool buf_view_read_u16_be(buf_view_t *b, const char *field_name, uint16_t *out);
bool buf_view_read_u16_le(buf_view_t *b, const char *field_name, uint16_t *out);
bool buf_view_read_u32_be(buf_view_t *b, const char *field_name, uint32_t *out);
bool buf_view_read_u32_le(buf_view_t *b, const char *field_name, uint32_t *out);
bool buf_view_read_u64_be(buf_view_t *b, const char *field_name, uint64_t *out);
bool buf_view_read_u64_le(buf_view_t *b, const char *field_name, uint64_t *out);
bool buf_view_read_bytes(buf_view_t *b, const char *field_name, uint8_t *out, size_t len);

/* Writes */
bool buf_view_write_u8(buf_view_t *b, const char *field_name, uint8_t v);
bool buf_view_write_u16_be(buf_view_t *b, const char *field_name, uint16_t v);
bool buf_view_write_u16_le(buf_view_t *b, const char *field_name, uint16_t v);
bool buf_view_write_u32_be(buf_view_t *b, const char *field_name, uint32_t v);
bool buf_view_write_u32_le(buf_view_t *b, const char *field_name, uint32_t v);
bool buf_view_write_u64_be(buf_view_t *b, const char *field_name, uint64_t v);
bool buf_view_write_u64_le(buf_view_t *b, const char *field_name, uint64_t v);
bool buf_view_write_bytes(buf_view_t *b, const char *field_name, const uint8_t *data, size_t len);


#ifdef __cplusplus
}
#endif
