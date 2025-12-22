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
#include "context_registry.h"

/* ============================================================================
 * Context Registry Implementation
 * ============================================================================
 *
 * Simple array-indexed storage for void* contexts.
 * Context IDs are 0-255, providing O(1) lookup and O(1) insertion.
 */

#define MAX_CONTEXTS 256

typedef struct context_registry_s {
    void *contexts[MAX_CONTEXTS];
} context_registry_t;

context_registry_t *context_registry_create(void) {
    context_registry_t *registry = (context_registry_t *)malloc(sizeof(context_registry_t));
    if(!registry) { return NULL; }

    memset(registry->contexts, 0, sizeof(registry->contexts));

    return registry;
}

void context_registry_destroy(context_registry_t *registry) {
    if(!registry) { return; }
    free(registry);
}

util_err_t context_registry_set(context_registry_t *registry,
                                context_id_t context_id,
                                void *context) {
    if(!registry) { return UTIL_EINVAL; }

    registry->contexts[context_id] = context;

    return UTIL_OK;
}

void *context_registry_get(context_registry_t *registry,
                           context_id_t context_id) {
    if(!registry) { return NULL; }

    return registry->contexts[context_id];
}

util_err_t context_registry_remove(context_registry_t *registry,
                                   context_id_t context_id) {
    if(!registry) { return UTIL_EINVAL; }

    if(registry->contexts[context_id] == NULL) { return UTIL_ENOTFOUND; }

    registry->contexts[context_id] = NULL;

    return UTIL_OK;
}
