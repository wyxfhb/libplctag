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
#include "../../../utils/buf.h"
#include "../../../utils/err.h"

/* ============================================================================
 * CIP Path Segment Types
 * ============================================================================ */

/* Logical segment types */
#define CIP_SEGMENT_LOGICAL_CLASS_8BIT 0x20
#define CIP_SEGMENT_LOGICAL_CLASS_16BIT 0x21
#define CIP_SEGMENT_LOGICAL_CLASS_32BIT 0x22

#define CIP_SEGMENT_LOGICAL_INSTANCE_8BIT 0x24
#define CIP_SEGMENT_LOGICAL_INSTANCE_16BIT 0x25
#define CIP_SEGMENT_LOGICAL_INSTANCE_32BIT 0x26

#define CIP_SEGMENT_LOGICAL_ATTRIBUTE_8BIT 0x30
#define CIP_SEGMENT_LOGICAL_ATTRIBUTE_16BIT 0x31
#define CIP_SEGMENT_LOGICAL_ATTRIBUTE_32BIT 0x32

#define CIP_SEGMENT_NUMERIC_8BIT 0x28
#define CIP_SEGMENT_NUMERIC_16BIT 0x29
#define CIP_SEGMENT_NUMERIC_32BIT 0x2A

/* Symbolic and data segments */
#define CIP_SEGMENT_SYMBOLIC 0x91
#define CIP_SEGMENT_DATA 0x80

/* ============================================================================
 * CIP Path Segment
 * ============================================================================ */

/**
 * CIP Path Segment
 *
 * Represents one segment in a CIP EPATH.
 * For Phase 4, we only parse symbolic segments (tag names).
 */
typedef struct {
    uint8_t type; /* Segment type code */

    union {
        /* Logical segment (class, instance, attribute, or numeric) */
        struct {
            uint32_t id; /* Class, instance, attribute, or numeric value */
        } logical;

        /* Symbolic segment (tag name) */
        struct {
            const char *name; /* Pointer to name in buffer */
            size_t length;    /* Name length in bytes */
        } symbolic;

        /* Data segment (Omron-style) */
        struct {
            uint32_t offset_bytes;  /* Byte offset */
            uint16_t element_count; /* Number of elements */
        } data;
    };
} cip_segment_t;

/* ============================================================================
 * CIP Path
 * ============================================================================ */

/**
 * CIP Path
 *
 * Represents a complete parsed CIP path (EPATH).
 * Contains all segments parsed from the wire format.
 */
typedef struct cip_path_s {
    cip_segment_t segments[16]; /* Array of segments */
    size_t segment_count;       /* Number of segments */
} cip_path_t;

/* ============================================================================
 * CIP Path Functions
 * ============================================================================ */

/**
 * @brief Parse CIP path from buffer
 *
 * Reads path_size_words * 2 bytes and parses all segments.
 * For Phase 4, primarily handles symbolic segments (tag names).
 *
 * @param input Buffer positioned at start of path
 * @param path_size_words Path size in 16-bit words
 * @param path OUT: Parsed path structure
 * @return UTIL_OK on success, error code on failure
 */
util_err_t cip_parse_path(buf_t *input, uint8_t path_size_words, cip_path_t *path);
