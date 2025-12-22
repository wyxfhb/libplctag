#pragma once

#include <stdint.h>
#include <stddef.h>

/* ============================================================================
 * Tag Type Codes (CIP Standard)
 * ============================================================================ */

#define CIP_TYPE_BOOL 0xC1 /* BOOL (logical) */
#define CIP_TYPE_SINT 0xC2 /* SINT (8-bit signed) */
#define CIP_TYPE_INT 0xC3  /* INT (16-bit signed) */
#define CIP_TYPE_DINT 0xC4 /* DINT (32-bit signed) */
#define CIP_TYPE_REAL 0xCA /* REAL (32-bit floating point) */

/* ============================================================================
 * Tag Definition
 * ============================================================================ */

/**
 * Tag Definition Structure
 *
 * Represents a single tag that can be read/written via EtherNet/IP.
 * Supports multi-dimensional arrays via dimension array.
 * Uses array-based storage with 1-based instance IDs (instance_id - 1 = array index).
 */
typedef struct tag_def_s {
    uint32_t instance_id;  /* Unique instance ID within tag class (assigned at creation time, starting from 1) */
    char name[256];        /* Tag name (null-terminated) */
    uint16_t tag_type;     /* CIP type code (CIP_TYPE_*) or UDT ID if udt_id != 0 */
    uint16_t udt_id;       /* UDT ID if tag is UDT-based (0 = built-in type) */
    size_t elem_size;      /* Size of one element in bytes */
    size_t elem_count;     /* Total elements (product of dimensions) */
    uint8_t *data;         /* Tag data (allocated with malloc) */
    size_t dim_count;      /* Number of dimensions (1 = scalar, 2+ = array) */
    size_t dimensions[8];  /* Array dimensions (up to 8D) */
    uint8_t data_file_num; /* PCCC data file number (3,7,8,18,19 for PCCC; 0 = not PCCC) */
} tag_def_t;

/* ============================================================================
 * Tag Storage Functions
 * ============================================================================ */

/**
 * @brief Create a new tag definition
 *
 * Allocates a tag_def_t structure and initializes it.
 * Data buffer is allocated but not initialized.
 *
 * @param name Tag name (will be copied)
 * @param type CIP type code
 * @param elem_size Size of one element in bytes
 * @param elem_count Number of elements
 * @return Newly allocated tag_def_t, or NULL on allocation failure
 */
tag_def_t *tag_create(const char *name, uint16_t type, size_t elem_size, size_t elem_count);

/**
 * @brief Destroy a tag definition
 *
 * Frees the tag structure and its data buffer.
 *
 * @param tag Tag to destroy (may be NULL - no-op)
 */
void tag_destroy(tag_def_t *tag);

/**
 * @brief Find tag by name in array
 *
 * Searches an array of tags for one matching the given name.
 * O(n) time complexity.
 *
 * @param tags Array of tag pointers (may be NULL)
 * @param count Number of tags in array
 * @param name Tag name to search for
 * @param name_len Length of tag name to match
 * @return Matching tag_def_t, or NULL if not found
 */
tag_def_t *tag_find_by_name(tag_def_t **tags, size_t count, const char *name, size_t name_len);

/**
 * @brief Get tag by instance ID from array
 *
 * Direct array access: tags[instance_id - 1]
 * O(1) time complexity.
 *
 * @param tags Array of tag pointers (may be NULL)
 * @param count Number of tags in array
 * @param instance_id Instance ID (1-based)
 * @return tag_def_t if found, NULL if instance_id out of bounds
 */
tag_def_t *tag_get_by_id(tag_def_t **tags, size_t count, uint32_t instance_id);

/**
 * @brief Find tag by PCCC data file number
 *
 * Searches an array of tags for one matching the given PCCC data file number.
 * O(n) time complexity.
 *
 * @param tags Array of tag pointers (may be NULL)
 * @param count Number of tags in array
 * @param file_num PCCC data file number to search for
 * @return Matching tag_def_t, or NULL if not found
 */
tag_def_t *tag_find_by_file_num(tag_def_t **tags, size_t count, uint8_t file_num);

/**
 * @brief Initialize tag array with initial capacity
 *
 * Allocates the tag array storage with initial capacity.
 * This must be called before any tag_array_add() calls.
 *
 * @param tags Pointer to tag array pointer (will be allocated)
 * @param capacity Pointer to capacity variable (will be set)
 * @return 0 on success, non-zero on allocation failure
 */
int tag_array_init(tag_def_t ***tags, size_t *capacity);

/**
 * @brief Add tag to array with automatic resizing
 *
 * Adds a tag to the end of the array, resizing (doubling capacity) if needed.
 *
 * @param tags Pointer to tag array pointer (may be reallocated)
 * @param count Pointer to count variable (will be incremented)
 * @param capacity Pointer to capacity variable (may be increased)
 * @param tag Tag to add (must not be NULL)
 * @return 0 on success, non-zero on allocation failure
 */
int tag_array_add(tag_def_t ***tags, size_t *count, size_t *capacity, tag_def_t *tag);

/**
 * @brief Destroy tag array and all tags
 *
 * Frees all tags in the array and then frees the array itself.
 *
 * @param tags Array of tag pointers (may be NULL)
 * @param count Number of tags in array
 */
void tag_array_destroy(tag_def_t **tags, size_t count);

/**
 * @brief Create a DINT tag with initial value
 *
 * Convenience function to create a 32-bit signed integer tag.
 *
 * @param name Tag name
 * @param initial_value Initial value (32-bit signed)
 * @return Newly allocated tag, or NULL on allocation failure
 */
tag_def_t *tag_create_dint(const char *name, int32_t initial_value);

/**
 * @brief Create a REAL tag with initial value
 *
 * Convenience function to create a 32-bit floating point tag.
 *
 * @param name Tag name
 * @param initial_value Initial value (32-bit float)
 * @return Newly allocated tag, or NULL on allocation failure
 */
tag_def_t *tag_create_real(const char *name, float initial_value);

/**
 * @brief Create a DINT array tag
 *
 * Convenience function to create an array of 32-bit signed integers.
 *
 * @param name Tag name
 * @param count Number of elements
 * @return Newly allocated tag, or NULL on allocation failure
 */
tag_def_t *tag_create_dint_array(const char *name, size_t count);

/**
 * @brief Create a multi-dimensional DINT array tag
 *
 * Convenience function to create a multi-dimensional array of 32-bit signed integers.
 *
 * @param name Tag name
 * @param dim_count Number of dimensions (must be > 0)
 * @param dimensions Array of dimension sizes (length = dim_count)
 * @return Newly allocated tag, or NULL on allocation failure
 */
tag_def_t *tag_create_dint_array_multi(const char *name, size_t dim_count, const size_t *dimensions);

/**
 * @brief Create a multi-dimensional REAL array tag
 *
 * Convenience function to create a multi-dimensional array of 32-bit floats.
 *
 * @param name Tag name
 * @param dim_count Number of dimensions (must be > 0)
 * @param dimensions Array of dimension sizes (length = dim_count)
 * @return Newly allocated tag, or NULL on allocation failure
 */
tag_def_t *tag_create_real_array_multi(const char *name, size_t dim_count, const size_t *dimensions);
