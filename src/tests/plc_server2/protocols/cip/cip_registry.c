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
#include "cip_registry.h"
#include "cip_path.h"

/* ============================================================================
 * CIP Class Structure
 * ============================================================================
 *
 * Each CIP class contains an array of service handlers, indexed by service code.
 * Service code 0 is reserved and should not be used (returns invalid service).
 */

#define MAX_SERVICES 256

typedef struct cip_class_s {
    cip_service_handler_t handlers[MAX_SERVICES];
} cip_class_t;

/* ============================================================================
 * CIP Class Registry Structure
 * ============================================================================
 *
 * Contains:
 * - Array of class pointers (one per class ID 0-255)
 * - Symbolic handler class ID (used when path has no explicit class)
 */

#define MAX_CLASSES 256

typedef struct cip_class_registry_s {
    cip_class_t *classes[MAX_CLASSES];
    cip_class_id_t symbolic_handler_class;
} cip_class_registry_t;

/* ============================================================================
 * Registry Creation and Destruction
 * ============================================================================ */

cip_class_registry_t *cip_class_registry_create(void) {
    cip_class_registry_t *registry = (cip_class_registry_t *)malloc(sizeof(cip_class_registry_t));
    if(!registry) { return NULL; }

    memset(registry->classes, 0, sizeof(registry->classes));
    registry->symbolic_handler_class = 0xFF; /* Unset initially */

    return registry;
}

void cip_class_registry_destroy(cip_class_registry_t *registry) {
    if(!registry) { return; }

    /* Free all class structures */
    for(int i = 0; i < MAX_CLASSES; i++) {
        if(registry->classes[i]) {
            free(registry->classes[i]);
        }
    }

    free(registry);
}

/* ============================================================================
 * Symbolic Handler Class
 * ============================================================================ */

util_err_t cip_class_registry_set_symbolic_handler_class(
    cip_class_registry_t *registry,
    cip_class_id_t class_id) {
    if(!registry) { return UTIL_EINVAL; }

    registry->symbolic_handler_class = class_id;

    return UTIL_OK;
}

cip_class_id_t cip_class_registry_get_symbolic_handler_class(
    cip_class_registry_t *registry) {
    if(!registry) { return 0xFF; }

    return registry->symbolic_handler_class;
}

/* ============================================================================
 * Service Registration
 * ============================================================================ */

util_err_t cip_class_registry_add_service(
    cip_class_registry_t *registry,
    cip_class_id_t class_id,
    cip_service_code_t service_code,
    cip_service_handler_t handler) {
    if(!registry || !handler) { return UTIL_EINVAL; }

    /* Lazily allocate class structure if needed */
    if(!registry->classes[class_id]) {
        cip_class_t *class = (cip_class_t *)malloc(sizeof(cip_class_t));
        if(!class) { return UTIL_ERESOURCE; }

        memset(class->handlers, 0, sizeof(class->handlers));
        registry->classes[class_id] = class;
    }

    /* Register the handler */
    registry->classes[class_id]->handlers[service_code] = handler;

    return UTIL_OK;
}

/* ============================================================================
 * Service Dispatch
 * ============================================================================ */

util_err_t cip_class_registry_dispatch_service(
    cip_class_registry_t *registry,
    cip_path_t *path,
    cip_service_code_t service_code,
    buf_t *input,
    buf_t *output,
    context_registry_t *context_reg) {
    if(!registry || !path || !input || !output || !context_reg) {
        return UTIL_EINVAL;
    }

    /* Determine target class from path
     * For now, we handle the simple case of a path starting with a logical class segment.
     * Symbolic paths (tag names) use the symbolic handler class.
     * This is a placeholder - actual path routing will be more complex. */

    cip_class_id_t target_class = 0xFF;

    /* Check if path has segments */
    if(path->segment_count > 0) {
        /* Check first segment - if it's a class segment, use it */
        if(path->segments[0].type == 0x20) { /* Logical class 8-bit */
            target_class = (cip_class_id_t)path->segments[0].logical.id;
        } else if(path->segments[0].type == 0x91) { /* Symbolic segment */
            /* Use symbolic handler class for tag names */
            target_class = registry->symbolic_handler_class;
        }
    }

    /* Validate that we have a target class */
    if(target_class == 0xFF) {
        buf_set_error(output, UTIL_ENOTFOUND, "target_class");
        return UTIL_ENOTFOUND;
    }

    /* Check if class is registered */
    if(!registry->classes[target_class]) {
        buf_set_error(output, UTIL_ENOTFOUND, "class");
        return UTIL_ENOTFOUND;
    }

    /* Get the handler for this service code */
    cip_service_handler_t handler = registry->classes[target_class]->handlers[service_code];
    if(!handler) {
        buf_set_error(output, UTIL_ENOTSUPPORTED, "service_code");
        return UTIL_ENOTSUPPORTED;
    }

    /* Call the handler */
    return handler(service_code, path, input, output, context_reg);
}
