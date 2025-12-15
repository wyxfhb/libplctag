#pragma once

#include <stdint.h>
#include <stddef.h>

/* ============================================================================
 * Tag Type Codes (CIP Standard)
 * ============================================================================ */

#define CIP_TYPE_BOOL   0xC1  /* BOOL (logical) */
#define CIP_TYPE_SINT   0xC2  /* SINT (8-bit signed) */
#define CIP_TYPE_INT    0xC3  /* INT (16-bit signed) */
#define CIP_TYPE_DINT   0xC4  /* DINT (32-bit signed) */
#define CIP_TYPE_REAL   0xCA  /* REAL (32-bit floating point) */

/* ============================================================================
 * Tag Definition
 * ============================================================================ */

/**
 * Tag Definition Structure
 *
 * Represents a single tag that can be read/written via EtherNet/IP.
 * Supports multi-dimensional arrays via dimension array.
 */
typedef struct tag_def_s {
    uint32_t instance_id;           /* Unique instance ID (assigned at creation time, never 0) */
    char name[256];                 /* Tag name (null-terminated) */
    uint16_t tag_type;              /* CIP type code (CIP_TYPE_*) */
    size_t elem_size;               /* Size of one element in bytes */
    size_t elem_count;              /* Total elements (product of dimensions) */
    uint8_t *data;                  /* Tag data (allocated with malloc) */
    size_t dim_count;               /* Number of dimensions (1 = scalar, 2+ = array) */
    size_t dimensions[8];           /* Array dimensions (up to 8D) */
    struct tag_def_s *next;         /* Linked list next pointer */
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
tag_def_t* tag_create(const char *name, uint16_t type, size_t elem_size,
                      size_t elem_count);

/**
 * @brief Destroy a tag definition
 *
 * Frees the tag structure and its data buffer.
 *
 * @param tag Tag to destroy (may be NULL - no-op)
 */
void tag_destroy(tag_def_t *tag);

/**
 * @brief Find tag by name in linked list
 *
 * Searches a linked list of tags for one matching the given name.
 *
 * @param head Head of linked list (may be NULL)
 * @param name Tag name to search for
 * @return Matching tag_def_t, or NULL if not found
 */
tag_def_t* tag_find_by_name(tag_def_t *head, const char *name);

/**
 * @brief Create a DINT tag with initial value
 *
 * Convenience function to create a 32-bit signed integer tag.
 *
 * @param name Tag name
 * @param initial_value Initial value (32-bit signed)
 * @return Newly allocated tag, or NULL on allocation failure
 */
tag_def_t* tag_create_dint(const char *name, int32_t initial_value);

/**
 * @brief Create a REAL tag with initial value
 *
 * Convenience function to create a 32-bit floating point tag.
 *
 * @param name Tag name
 * @param initial_value Initial value (32-bit float)
 * @return Newly allocated tag, or NULL on allocation failure
 */
tag_def_t* tag_create_real(const char *name, float initial_value);

/**
 * @brief Create a DINT array tag
 *
 * Convenience function to create an array of 32-bit signed integers.
 *
 * @param name Tag name
 * @param count Number of elements
 * @return Newly allocated tag, or NULL on allocation failure
 */
tag_def_t* tag_create_dint_array(const char *name, size_t count);

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
tag_def_t* tag_create_dint_array_multi(const char *name, size_t dim_count,
                                        const size_t *dimensions);

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
tag_def_t* tag_create_real_array_multi(const char *name, size_t dim_count,
                                        const size_t *dimensions);
