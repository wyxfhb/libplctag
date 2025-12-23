#pragma once

/***************************************************************************
 *   Copyright (C) 2025 by Kyle Hayes                                      *
 *   Author Kyle Hayes  kyle.hayes@gmail.com                               *
 *                                                                         *
 * This software is available under either the Mozilla Public License      *
 * version 2.0 or the GNU LGPL version 2 (or later) license, whichever     *
 * you choose.                                                             *
 *                                                                         *
 * MPL 2.0:                                                                *
 *                                                                         *
 *   This Source Code Form is subject to the terms of the Mozilla Public   *
 *   License, v. 2.0. If a copy of the MPL was not distributed with this   *
 *   file, You can obtain one at http://mozilla.org/MPL/2.0/.              *
 *                                                                         *
 *                                                                         *
 * LGPL 2:                                                                 *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU Library General Public License as       *
 *   published by the Free Software Foundation; either version 2 of the    *
 *   License, or (at your option) any later version.                       *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this program; if not, write to the                 *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/


#include <stdint.h>
#include <stddef.h>

/* ============================================================================
 * Class 0x6C (Variable Type Object) Entry Types
 * ============================================================================ */

/**
 * Entry type discriminator for unified UDT storage.
 *
 * Unified storage contains both UDT definitions and their field definitions.
 * They are stored in a single flat array with sequential instance IDs:
 *   - UDT entry with instance_id = N
 *   - Followed immediately by its field entries: N+1, N+2, ...
 *   - Next UDT entry: N + field_count + 1
 */
typedef enum {
    UDT_ENTRY_TYPE_DEF, /* Entry is a UDT definition */
    UDT_ENTRY_FIELD     /* Entry is a UDT field definition */
} udt_entry_type_t;

/**
 * Unified UDT Entry
 *
 * Represents either a UDT definition or a field definition.
 * Stored in a single flat array with both UDT and field entries.
 *
 * Instance IDs are 1-based and sequential:
 *   - Access: entries[instance_id - 1]
 *   - For UDT at index i with field_count fields:
 *     - Fields stored at indices i+1 through i+field_count
 */
typedef struct udt_entry_s {
    udt_entry_type_t entry_type; /* Distinguishes UDT definitions from field definitions */
    uint16_t instance_id;        /* 1-based instance ID, unique per entry */
    char name[256];              /* UDT name or field name (null-terminated) */

    union {
        /* When entry_type == UDT_ENTRY_TYPE_DEF */
        struct {
            uint16_t field_count; /* Number of fields following this UDT entry */
            size_t total_size;    /* Total bytes for one UDT instance */
            uint16_t crc_code;    /* CRC code (0 if not computed) */
        } udt;

        /* When entry_type == UDT_ENTRY_FIELD */
        struct {
            uint16_t symbol_type;  /* CIP type code (0xC3=INT, 0xC4=DINT, 0xCA=REAL, etc.) or UDT ID */
            size_t byte_offset;    /* Byte offset within parent UDT */
            size_t bit_offset;     /* Bit offset (0-7) for BOOL types, 0 for others */
            size_t element_length; /* Size of one element in bytes */
        } field;
    } data;

    uint32_t dimensions[3]; /* UDT: [0]=field_count, [1-2]=0; Field: actual array dimensions */
} udt_entry_t;

/* ============================================================================
 * Class 0x6C Array Management Functions
 * ============================================================================ */

/**
 * @brief Initialize UDT entry array with initial capacity
 *
 * Allocates the UDT entry array storage with initial capacity.
 * This must be called before any udt_*_add() calls.
 *
 * @param entries Pointer to UDT entry array pointer (will be allocated)
 * @param capacity Pointer to capacity variable (will be set)
 * @return 0 on success, non-zero on allocation failure
 */
int udt_array_init(udt_entry_t **entries, size_t *capacity);

/**
 * @brief Add UDT type definition entry to array
 *
 * Adds a UDT entry to the array, resizing (doubling capacity) if needed.
 * Returns the instance ID assigned to the UDT.
 *
 * @param entries Pointer to UDT entry array pointer (may be reallocated)
 * @param count Pointer to count variable (will be incremented)
 * @param capacity Pointer to capacity variable (may be increased)
 * @param name UDT name
 * @param total_size Total bytes for one UDT instance
 * @param field_count Number of fields in this UDT
 * @return Assigned instance_id on success, 0 on allocation failure
 */
uint16_t udt_add_type_def(udt_entry_t **entries, size_t *count, size_t *capacity, const char *name, size_t total_size,
                          uint16_t field_count);

/**
 * @brief Add field definition entry to array
 *
 * Adds a field entry to the array, resizing (doubling capacity) if needed.
 * Returns the instance ID assigned to the field.
 *
 * @param entries Pointer to UDT entry array pointer (may be reallocated)
 * @param count Pointer to count variable (will be incremented)
 * @param capacity Pointer to capacity variable (may be increased)
 * @param name Field name
 * @param symbol_type CIP type code or UDT ID
 * @param byte_offset Byte offset within parent UDT
 * @param bit_offset Bit offset for BOOL fields (0 for others)
 * @param element_length Size of one element
 * @param dimensions Array dimensions (NULL for scalar)
 * @return Assigned instance_id on success, 0 on allocation failure
 */
uint16_t udt_add_field(udt_entry_t **entries, size_t *count, size_t *capacity, const char *name, uint16_t symbol_type,
                       size_t byte_offset, size_t bit_offset, size_t element_length, const uint32_t dimensions[3]);

/**
 * @brief Get UDT entry by instance ID from array
 *
 * Direct array access: entries[instance_id - 1]
 * O(1) time complexity.
 *
 * @param entries Array of UDT entries (may be NULL)
 * @param count Number of entries in array
 * @param instance_id Instance ID (1-based)
 * @return Pointer to udt_entry_t if found, NULL if instance_id out of bounds
 */
udt_entry_t *udt_get_by_id(udt_entry_t *entries, size_t count, uint16_t instance_id);

/**
 * @brief Find UDT type definition by name in array
 *
 * Searches the array for a UDT entry matching the given name.
 * O(n) time complexity but skips field entries.
 *
 * @param entries Array of UDT entries (may be NULL)
 * @param count Number of entries in array
 * @param name UDT name to search for (case-sensitive)
 * @return Pointer to udt_entry_t if found, NULL if not found
 */
udt_entry_t *udt_find_by_name(udt_entry_t *entries, size_t count, const char *name);

/**
 * @brief Destroy UDT entry array
 *
 * Frees the UDT entry array.
 *
 * @param entries Array of UDT entries (may be NULL)
 * @param count Number of entries in array (unused but provided for consistency)
 */
void udt_array_destroy(udt_entry_t *entries, size_t count);
