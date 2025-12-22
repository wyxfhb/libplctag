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

#include "udt_storage.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ============================================================================
 * Class 0x6C Array Management
 * ============================================================================ */

int udt_array_init(udt_entry_t **entries, size_t *capacity) {
    if (!entries || !capacity) {
        return -1;
    }

    /* Initial capacity: 128 entries */
    *capacity = 128;
    *entries = (udt_entry_t *)calloc(*capacity, sizeof(udt_entry_t));
    if (!*entries) {
        *capacity = 0;
        return -1;
    }

    return 0;
}

uint16_t udt_add_type_def(udt_entry_t **entries, size_t *count, size_t *capacity,
                          const char *name, size_t total_size, uint16_t field_count) {
    if (!entries || !count || !capacity || !name) {
        return 0;
    }

    /* Check if we need to resize */
    if (*count >= *capacity) {
        size_t new_capacity = *capacity * 2;
        if (new_capacity == 0) {
            new_capacity = 128;
        }

        udt_entry_t *new_array = (udt_entry_t *)realloc(*entries, new_capacity * sizeof(udt_entry_t));
        if (!new_array) {
            return 0;
        }

        *entries = new_array;
        *capacity = new_capacity;
    }

    /* Add UDT entry */
    udt_entry_t *entry = &(*entries)[*count];
    memset(entry, 0, sizeof(*entry));

    entry->entry_type = UDT_ENTRY_TYPE_DEF;
    entry->instance_id = (uint16_t)(*count + 1);  /* 1-based instance ID */
    strncpy(entry->name, name, sizeof(entry->name) - 1);
    entry->name[sizeof(entry->name) - 1] = '\0';

    entry->data.udt.total_size = total_size;
    entry->data.udt.field_count = field_count;
    entry->data.udt.crc_code = 0;

    entry->dimensions[0] = field_count;
    entry->dimensions[1] = 0;
    entry->dimensions[2] = 0;

    uint16_t udt_instance_id = entry->instance_id;
    (*count)++;

    return udt_instance_id;
}

uint16_t udt_add_field(udt_entry_t **entries, size_t *count, size_t *capacity,
                       const char *name, uint16_t symbol_type, size_t byte_offset,
                       size_t bit_offset, size_t element_length, const uint32_t dimensions[3]) {
    if (!entries || !count || !capacity || !name) {
        return 0;
    }

    /* Check if we need to resize */
    if (*count >= *capacity) {
        size_t new_capacity = *capacity * 2;
        if (new_capacity == 0) {
            new_capacity = 128;
        }

        udt_entry_t *new_array = (udt_entry_t *)realloc(*entries, new_capacity * sizeof(udt_entry_t));
        if (!new_array) {
            return 0;
        }

        *entries = new_array;
        *capacity = new_capacity;
    }

    /* Add field entry */
    udt_entry_t *entry = &(*entries)[*count];
    memset(entry, 0, sizeof(*entry));

    entry->entry_type = UDT_ENTRY_FIELD;
    entry->instance_id = (uint16_t)(*count + 1);  /* 1-based instance ID */
    strncpy(entry->name, name, sizeof(entry->name) - 1);
    entry->name[sizeof(entry->name) - 1] = '\0';

    entry->data.field.symbol_type = symbol_type;
    entry->data.field.byte_offset = byte_offset;
    entry->data.field.bit_offset = bit_offset;
    entry->data.field.element_length = element_length;

    if (dimensions) {
        entry->dimensions[0] = dimensions[0];
        entry->dimensions[1] = dimensions[1];
        entry->dimensions[2] = dimensions[2];
    } else {
        entry->dimensions[0] = 0;
        entry->dimensions[1] = 0;
        entry->dimensions[2] = 0;
    }

    uint16_t field_instance_id = entry->instance_id;
    (*count)++;

    return field_instance_id;
}

udt_entry_t* udt_get_by_id(udt_entry_t *entries, size_t count, uint16_t instance_id) {
    if (!entries || instance_id == 0 || instance_id > count) {
        return NULL;
    }

    /* Instance IDs are 1-based, array is 0-based */
    return &entries[instance_id - 1];
}

udt_entry_t* udt_find_by_name(udt_entry_t *entries, size_t count, const char *name) {
    if (!entries || !name) {
        return NULL;
    }

    for (size_t i = 0; i < count; i++) {
        if (entries[i].entry_type == UDT_ENTRY_TYPE_DEF && strcmp(entries[i].name, name) == 0) {
            return &entries[i];
        }
    }
    return NULL;
}

void udt_array_destroy(udt_entry_t *entries, size_t count) {
    if (!entries) {
        return;
    }

    /* Free the array */
    free(entries);
}
