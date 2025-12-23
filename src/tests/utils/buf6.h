#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

typedef struct reader_s {
    uint8_t *base;
    size_t size;
    size_t cursor;
    int err;
} reader_t;

typedef struct writer_s {
    uint8_t *base;
    size_t size;
    size_t cursor;
    int err;
} writer_t;


reader_t reader_init(uint8_t *buf, size_t size);

size_t reader_remaining(const reader_t *r);
bool reader_ok(const reader_t *r);

bool reader_set_err(reader_t *r, int err_code);
int reader_get_err(const reader_t *r);

bool reader_read_u8(reader_t *r, uint8_t *out);
bool reader_read_u16_le(reader_t *r, uint16_t *out);
bool reader_read_u16_be(reader_t *r, uint16_t *out);
bool reader_read_u32_le(reader_t *r, uint32_t *out);
bool reader_read_u32_be(reader_t *r, uint32_t *out);
bool reader_read_u64_le(reader_t *r, uint64_t *out);
bool reader_read_u64_be(reader_t *r, uint64_t *out);
bool reader_read_bytes(reader_t *r, uint8_t *out, size_t len);

/**
 * @brief Peek at a 16-bit big-endian integer at a specific offset from the start of the buffer.
 * Does not advance the cursor.
 * @param r The reader.
 * @param offset Byte offset from the beginning of the reader's buffer.
 * @param out Pointer to store the peeked value.
 * @return true on success, false if the peek would read out of bounds.
 */
bool reader_peek_u16_be(const reader_t *r, size_t offset, uint16_t *out);

writer_t writer_init(uint8_t *buf, size_t size);

size_t writer_remaining(const writer_t *w);
bool writer_ok(const writer_t *w);

bool writer_set_err(writer_t *w, int err_code);
int writer_get_err(const writer_t *w);

size_t writer_get_cursor(const writer_t *w);
bool writer_set_cursor(writer_t *w, size_t cursor);

bool writer_write_u8(writer_t *w, uint8_t val);
bool writer_write_u16_le(writer_t *w, uint16_t val);
bool writer_write_u16_be(writer_t *w, uint16_t val);
bool writer_write_u32_le(writer_t *w, uint32_t val);
bool writer_write_u32_be(writer_t *w, uint32_t val);
bool writer_write_u64_le(writer_t *w, uint64_t val);
bool writer_write_u64_be(writer_t *w, uint64_t val);
bool writer_write_bytes(writer_t *w, const uint8_t *data, size_t len);
