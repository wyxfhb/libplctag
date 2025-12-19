#pragma once

#include <stdint.h>
#include <stddef.h>

/* Forward declaration */
typedef struct udt_member_s udt_member_t;

/**
 * UDT Member Definition
 *
 * Represents a single field within a User-Defined Type.
 */
typedef struct udt_member_s {
    char name[256];           /* Member name */
    uint16_t symbol_type;     /* Type code (DINT=0xC4, REAL=0xCA, BOOL=0xC1, UDT ID, etc.) */
    size_t byte_offset;       /* Byte offset within UDT */
    size_t bit_offset;        /* Bit offset (0-7) for BOOL types, 0 for others */
    size_t element_length;    /* Size of single element (1, 2, 4 bytes) */
    uint32_t dimensions[3];   /* Array dimensions; [0,0,0] for scalar, [count,0,0] for 1D array */
} udt_member_t;

/**
 * UDT Definition
 *
 * Represents a complete User-Defined Type with its members and metadata.
 */
typedef struct udt_def_s {
    uint16_t udt_id;              /* Unique type ID (auto-assigned, 1-based) */
    char name[256];               /* UDT type name */
    size_t member_count;          /* Number of members */
    udt_member_t *members;        /* Dynamic array of members */
    size_t total_size;            /* Total bytes for one UDT instance */
    struct udt_def_s *next;       /* Linked list pointer */
} udt_def_t;

/**
 * Create a new UDT definition with auto-assigned ID
 *
 * @param name     UDT type name (max 255 chars)
 * @param total_size Total size in bytes for this UDT
 * @return         Newly allocated udt_def_t, or NULL on error
 */
udt_def_t *udt_create(const char *name, size_t total_size);

/**
 * Add a member to a UDT
 *
 * @param udt              UDT definition to modify
 * @param name             Member name
 * @param symbol_type      Type code (built-in type or UDT ID)
 * @param byte_offset      Byte offset within UDT
 * @param bit_offset       Bit offset (0-7) for BOOL, 0 for others
 * @param element_length   Size of single element
 * @param dimensions       Array dimensions (NULL for scalar)
 * @return                 0 on success, non-zero on error
 */
int udt_add_member(udt_def_t *udt, const char *name, uint16_t symbol_type,
                   size_t byte_offset, size_t bit_offset, size_t element_length,
                   const uint32_t dimensions[3]);

/**
 * Find UDT by ID
 *
 * @param head   Head of UDT linked list
 * @param udt_id UDT ID to search for
 * @return       Pointer to udt_def_t if found, NULL otherwise
 */
udt_def_t *udt_find_by_id(udt_def_t *head, uint16_t udt_id);

/**
 * Find UDT by name
 *
 * @param head Head of UDT linked list
 * @param name UDT name to search for (case-sensitive)
 * @return     Pointer to udt_def_t if found, NULL otherwise
 */
udt_def_t *udt_find_by_name(udt_def_t *head, const char *name);

/**
 * Destroy entire UDT registry
 *
 * Frees all linked UDT definitions and their members.
 *
 * @param head Head of UDT linked list
 */
void udt_destroy_all(udt_def_t *head);

/**
 * Get the next auto-assigned UDT ID
 *
 * @return Next sequential UDT ID (starting from 1)
 */
uint16_t udt_get_next_id(void);

/**
 * Reset UDT ID counter (for testing)
 */
void udt_reset_id_counter(void);
