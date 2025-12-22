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

tag_def_t *tag_create(const char *name, uint16_t type, size_t elem_size, size_t elem_count) {
    tag_def_t *tag = (tag_def_t *)malloc(sizeof(tag_def_t));
    if(!tag) { return NULL; }

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
    tag->dim_count = 1; /* Default to 1D (scalar or linear array) */

    /* Allocate data buffer */
    size_t total_bytes = elem_size * elem_count;
    tag->data = (uint8_t *)calloc(elem_count, elem_size);
    if(!tag->data && total_bytes > 0) {
        free(tag);
        return NULL;
    }

    return tag;
}

void tag_destroy(tag_def_t *tag) {
    if(!tag) { return; }

    if(tag->data) {
        free(tag->data);
        tag->data = NULL;
    }

    free(tag);
}

/* ============================================================================
 * Tag Lookup
 * ============================================================================ */

tag_def_t *tag_find_by_name(tag_def_t **tags, size_t count, const char *name, size_t name_len) {
    if(!tags || !name) { return NULL; }

    for(size_t i = 0; i < count; i++) {
        if(tags[i] && strncmp(tags[i]->name, name, name_len) == 0) { return tags[i]; }
    }
    return NULL;
}

/* ============================================================================
 * Convenience Constructors
 * ============================================================================ */

tag_def_t *tag_create_dint(const char *name, int32_t initial_value) {
    tag_def_t *tag = tag_create(name, CIP_TYPE_DINT, sizeof(int32_t), 1);
    if(tag && tag->data) { *(int32_t *)tag->data = initial_value; }
    return tag;
}

tag_def_t *tag_create_real(const char *name, float initial_value) {
    tag_def_t *tag = tag_create(name, CIP_TYPE_REAL, sizeof(float), 1);
    if(tag && tag->data) { *(float *)tag->data = initial_value; }
    return tag;
}

tag_def_t *tag_create_dint_array(const char *name, size_t count) {
    return tag_create(name, CIP_TYPE_DINT, sizeof(int32_t), count);
}

tag_def_t *tag_create_dint_array_multi(const char *name, size_t dim_count, const size_t *dimensions) {
    if(!dimensions || dim_count == 0 || dim_count > 8) { return NULL; }

    /* Calculate total element count by multiplying dimensions */
    size_t total_elems = 1;
    for(size_t i = 0; i < dim_count; i++) {
        if(dimensions[i] == 0) { return NULL; /* Invalid dimension */ }
        total_elems *= dimensions[i];
    }

    /* Create tag with calculated element count */
    tag_def_t *tag = tag_create(name, CIP_TYPE_DINT, sizeof(int32_t), total_elems);
    if(!tag) { return NULL; }

    /* Set dimension info */
    tag->dim_count = dim_count;
    memcpy(tag->dimensions, dimensions, dim_count * sizeof(size_t));

    return tag;
}

tag_def_t *tag_create_real_array_multi(const char *name, size_t dim_count, const size_t *dimensions) {
    if(!dimensions || dim_count == 0 || dim_count > 8) { return NULL; }

    /* Calculate total element count by multiplying dimensions */
    size_t total_elems = 1;
    for(size_t i = 0; i < dim_count; i++) {
        if(dimensions[i] == 0) { return NULL; /* Invalid dimension */ }
        total_elems *= dimensions[i];
    }

    /* Create tag with calculated element count */
    tag_def_t *tag = tag_create(name, CIP_TYPE_REAL, sizeof(float), total_elems);
    if(!tag) { return NULL; }

    /* Set dimension info */
    tag->dim_count = dim_count;
    memcpy(tag->dimensions, dimensions, dim_count * sizeof(size_t));

    return tag;
}

/* ============================================================================
 * Tag Array Management
 * ============================================================================ */

int tag_array_init(tag_def_t ***tags, size_t *capacity) {
    if(!tags || !capacity) { return -1; }

    /* Initial capacity: 128 tags */
    *capacity = 128;
    *tags = (tag_def_t **)calloc(*capacity, sizeof(tag_def_t *));
    if(!*tags) {
        *capacity = 0;
        return -1;
    }

    return 0;
}

int tag_array_add(tag_def_t ***tags, size_t *count, size_t *capacity, tag_def_t *tag) {
    if(!tags || !count || !capacity || !tag) { return -1; }

    /* Check if we need to resize */
    if(*count >= *capacity) {
        /* Double the capacity */
        size_t new_capacity = *capacity * 2;
        if(new_capacity == 0) { new_capacity = 128; }

        tag_def_t **new_array = (tag_def_t **)realloc(*tags, new_capacity * sizeof(tag_def_t *));
        if(!new_array) { return -1; }

        *tags = new_array;
        *capacity = new_capacity;
    }

    /* Add tag to array */
    (*tags)[*count] = tag;
    (*count)++;

    return 0;
}

tag_def_t *tag_get_by_id(tag_def_t **tags, size_t count, uint32_t instance_id) {
    if(!tags || instance_id == 0 || instance_id > count) { return NULL; }

    /* Instance IDs are 1-based, array is 0-based */
    return tags[instance_id - 1];
}

tag_def_t *tag_find_by_file_num(tag_def_t **tags, size_t count, uint8_t file_num) {
    if(!tags || file_num == 0) { return NULL; }

    for(size_t i = 0; i < count; i++) {
        if(tags[i] && tags[i]->data_file_num == file_num) { return tags[i]; }
    }
    return NULL;
}

void tag_array_destroy(tag_def_t **tags, size_t count) {
    if(!tags) { return; }

    /* Destroy all tags */
    for(size_t i = 0; i < count; i++) {
        if(tags[i]) { tag_destroy(tags[i]); }
    }

    /* Free the array itself */
    free(tags);
}
