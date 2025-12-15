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

#include <stdlib.h>
#include <string.h>
#include "tag_storage.h"

/* ============================================================================
 * Tag Instance ID Counter
 * ============================================================================ */

static uint32_t next_instance_id = 1; /* Instance IDs start at 1, never 0 */

/* ============================================================================
 * Tag Creation and Destruction
 * ============================================================================ */

tag_def_t* tag_create(const char *name, uint16_t type, size_t elem_size,
                      size_t elem_count) {
    tag_def_t *tag = (tag_def_t *)malloc(sizeof(tag_def_t));
    if (!tag) {
        return NULL;
    }

    memset(tag, 0, sizeof(*tag));

    /* Assign instance ID - this is permanent and unique */
    tag->instance_id = next_instance_id++;

    /* Copy name */
    strncpy(tag->name, name, sizeof(tag->name) - 1);
    tag->name[sizeof(tag->name) - 1] = '\0';

    /* Set type and size info */
    tag->tag_type = type;
    tag->elem_size = elem_size;
    tag->elem_count = elem_count;
    tag->dim_count = 1;  /* Default to 1D (scalar or linear array) */

    /* Allocate data buffer */
    size_t total_bytes = elem_size * elem_count;
    tag->data = (uint8_t *)calloc(elem_count, elem_size);
    if (!tag->data && total_bytes > 0) {
        free(tag);
        return NULL;
    }

    return tag;
}

void tag_destroy(tag_def_t *tag) {
    if (!tag) {
        return;
    }

    if (tag->data) {
        free(tag->data);
        tag->data = NULL;
    }

    free(tag);
}

/* ============================================================================
 * Tag Lookup
 * ============================================================================ */

tag_def_t* tag_find_by_name(tag_def_t *head, const char *name) {
    for (tag_def_t *tag = head; tag != NULL; tag = tag->next) {
        if (strcmp(tag->name, name) == 0) {
            return tag;
        }
    }
    return NULL;
}

/* ============================================================================
 * Convenience Constructors
 * ============================================================================ */

tag_def_t* tag_create_dint(const char *name, int32_t initial_value) {
    tag_def_t *tag = tag_create(name, CIP_TYPE_DINT, sizeof(int32_t), 1);
    if (tag && tag->data) {
        *(int32_t *)tag->data = initial_value;
    }
    return tag;
}

tag_def_t* tag_create_real(const char *name, float initial_value) {
    tag_def_t *tag = tag_create(name, CIP_TYPE_REAL, sizeof(float), 1);
    if (tag && tag->data) {
        *(float *)tag->data = initial_value;
    }
    return tag;
}

tag_def_t* tag_create_dint_array(const char *name, size_t count) {
    return tag_create(name, CIP_TYPE_DINT, sizeof(int32_t), count);
}

tag_def_t* tag_create_dint_array_multi(const char *name, size_t dim_count,
                                        const size_t *dimensions) {
    if (!dimensions || dim_count == 0 || dim_count > 8) {
        return NULL;
    }

    /* Calculate total element count by multiplying dimensions */
    size_t total_elems = 1;
    for (size_t i = 0; i < dim_count; i++) {
        if (dimensions[i] == 0) {
            return NULL;  /* Invalid dimension */
        }
        total_elems *= dimensions[i];
    }

    /* Create tag with calculated element count */
    tag_def_t *tag = tag_create(name, CIP_TYPE_DINT, sizeof(int32_t), total_elems);
    if (!tag) {
        return NULL;
    }

    /* Set dimension info */
    tag->dim_count = dim_count;
    memcpy(tag->dimensions, dimensions, dim_count * sizeof(size_t));

    return tag;
}

tag_def_t* tag_create_real_array_multi(const char *name, size_t dim_count,
                                        const size_t *dimensions) {
    if (!dimensions || dim_count == 0 || dim_count > 8) {
        return NULL;
    }

    /* Calculate total element count by multiplying dimensions */
    size_t total_elems = 1;
    for (size_t i = 0; i < dim_count; i++) {
        if (dimensions[i] == 0) {
            return NULL;  /* Invalid dimension */
        }
        total_elems *= dimensions[i];
    }

    /* Create tag with calculated element count */
    tag_def_t *tag = tag_create(name, CIP_TYPE_REAL, sizeof(float), total_elems);
    if (!tag) {
        return NULL;
    }

    /* Set dimension info */
    tag->dim_count = dim_count;
    memcpy(tag->dimensions, dimensions, dim_count * sizeof(size_t));

    return tag;
}
