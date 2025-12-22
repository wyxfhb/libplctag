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

#include "../utils/err.h"
#include "context_defs.h"

/* Opaque context registry structure */
typedef struct context_registry_s context_registry_t;

/**
 * @brief Create a new context registry.
 *
 * @return context_registry_t* New registry on success, NULL on failure.
 */
context_registry_t *context_registry_create(void);

/**
 * @brief Destroy a context registry.
 *
 * @param registry Registry to destroy. No-op if NULL.
 */
void context_registry_destroy(context_registry_t *registry);

/**
 * @brief Set a context in the registry.
 *
 * @param registry Registry to modify.
 * @param context_id ID of the context to set.
 * @param context Pointer to the context data. Can be NULL.
 * @return UTIL_OK on success, UTIL_EBOUNDS if ID is invalid, UTIL_EINVAL if registry is NULL.
 */
util_err_t context_registry_set(context_registry_t *registry,
                                context_id_t context_id,
                                void *context);

/**
 * @brief Get a context from the registry.
 *
 * @param registry Registry to query.
 * @param context_id ID of the context to retrieve.
 * @return void* Pointer to the context, or NULL if not found or registry is NULL.
 */
void *context_registry_get(context_registry_t *registry,
                           context_id_t context_id);

/**
 * @brief Remove a context from the registry.
 *
 * @param registry Registry to modify.
 * @param context_id ID of the context to remove.
 * @return UTIL_OK on success, UTIL_ENOTFOUND if ID not set, UTIL_EINVAL if registry is NULL.
 */
util_err_t context_registry_remove(context_registry_t *registry,
                                   context_id_t context_id);

#ifdef __cplusplus
}
#endif
