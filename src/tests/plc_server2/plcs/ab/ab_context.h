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

#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include "../generic/tag_storage.h"
#include "../generic/udt_storage.h"
#include "../../protocols/cip/cip_registry.h"

/* ============================================================================
 * PLC Type Enumeration
 * ============================================================================ */

typedef enum {
    PLC_TYPE_CONTROLLOGIX,   /* ControlLogix family */
    PLC_TYPE_MICRO800,       /* Micro800 family (not yet implemented) */
} plc_type_t;

/* ============================================================================
 * AB PLC Context Structure
 * ============================================================================
 *
 * Represents a single AB PLC instance (ControlLogix, Micro800, etc.)
 * Stores PLC-specific configuration, tags, UDTs, and the CIP registry.
 */

typedef struct ab_plc_context_s {
    plc_type_t plc_type;                /* PLC type (ControlLogix, Micro800, etc.) */
    cip_class_registry_t *cip_registry;  /* CIP class/service registry */

    /* Tag and UDT Storage */
    tag_def_t **tags;                   /* Array of tag pointers */
    size_t tag_count;                   /* Number of tags */
    size_t tag_capacity;                /* Capacity for tag array */

    udt_entry_t *udts;                  /* Array of UDT entries */
    size_t udt_count;                   /* Number of UDTs */
    size_t udt_capacity;                /* Capacity for UDT array */

    /* Path Configuration */
    uint8_t path[32];                   /* CIP path to PLC */
    size_t path_len;                    /* Length of path in bytes */

    /* Session Management */
    uint32_t next_session_id;           /* Next session ID to allocate */
} ab_plc_context_t;

/* ============================================================================
 * AB PLC Setup Functions
 * ============================================================================ */

/**
 * @brief Create and initialize an AB PLC context
 *
 * @param plc_type Type of PLC (ControlLogix, Micro800, etc.)
 * @return Newly allocated AB PLC context, or NULL on failure
 */
ab_plc_context_t *ab_plc_context_create(plc_type_t plc_type);

/**
 * @brief Destroy an AB PLC context and all its resources
 *
 * Frees all tags, UDTs, CIP registry, and the context itself.
 *
 * @param plc Context to destroy (may be NULL - no-op)
 */
void ab_plc_context_destroy(ab_plc_context_t *plc);

#ifdef __cplusplus
}
#endif
