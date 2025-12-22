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
#include "../../../utils/err.h"
#include "../../../utils/buf.h"
#include "../../context/context_registry.h"
#include "cip_defs.h"

/* ============================================================================
 * CIP Type Definitions
 * ============================================================================ */

typedef uint8_t cip_service_code_t;
typedef uint8_t cip_class_id_t;
typedef uint32_t cip_instance_id_t;

/* Opaque registry structure */
typedef struct cip_class_registry_s cip_class_registry_t;

/* CIP path structure (defined elsewhere) */
typedef struct cip_path_s cip_path_t;

/* ============================================================================
 * CIP Service Handler
 * ============================================================================
 *
 * Service handlers are called to process CIP service requests.
 * They receive:
 * - service_code: The requested CIP service code
 * - path: Parsed CIP path (class/instance/attribute)
 * - input: Request data (positioned after standard CIP headers)
 * - output: Response buffer (positioned at start of payload)
 * - context_reg: Context registry for accessing PLC, client, and other contexts
 *
 * Handlers should:
 * - Return UTIL_OK on success
 * - Set output buffer error on failure
 * - Not modify the context registry or other handlers
 */

typedef util_err_t (*cip_service_handler_t)(
    cip_service_code_t service_code,
    cip_path_t *path,
    buf_t *input,
    buf_t *output,
    context_registry_t *context_reg
);

/* ============================================================================
 * CIP Class Registry API
 * ============================================================================ */

/**
 * @brief Create a new CIP class registry.
 *
 * @return cip_class_registry_t* New registry on success, NULL on failure.
 */
cip_class_registry_t *cip_class_registry_create(void);

/**
 * @brief Destroy a CIP class registry.
 *
 * @param registry Registry to destroy. No-op if NULL.
 */
void cip_class_registry_destroy(cip_class_registry_t *registry);

/**
 * @brief Set the default class for symbolic segment routing.
 *
 * When a CIP path contains a symbolic segment (tag name), this class
 * is used to route the request if no explicit class is specified.
 *
 * @param registry Registry to modify.
 * @param class_id Class ID to use for symbolic routing (typically 0x6B for Symbol Object).
 * @return UTIL_OK on success, UTIL_EINVAL if registry is NULL.
 */
util_err_t cip_class_registry_set_symbolic_handler_class(
    cip_class_registry_t *registry,
    cip_class_id_t class_id
);

/**
 * @brief Get the symbolic handler class.
 *
 * @param registry Registry to query.
 * @return The class ID used for symbolic routing, or 0xFF if not set.
 */
cip_class_id_t cip_class_registry_get_symbolic_handler_class(
    cip_class_registry_t *registry
);

/**
 * @brief Add a service handler to a class.
 *
 * Registers a handler function for a specific service code within a CIP class.
 * If a handler already exists for this service code, it is replaced.
 *
 * @param registry Registry to modify.
 * @param class_id Class ID to register handler for (0-255).
 * @param service_code Service code to handle (0-255).
 * @param handler Handler function pointer.
 * @return UTIL_OK on success, UTIL_EINVAL if registry or handler is NULL.
 */
util_err_t cip_class_registry_add_service(
    cip_class_registry_t *registry,
    cip_class_id_t class_id,
    cip_service_code_t service_code,
    cip_service_handler_t handler
);

/**
 * @brief Dispatch a service request to the appropriate handler.
 *
 * Routes the request to the class identified in the path, then to the
 * appropriate service handler within that class.
 *
 * For symbolic paths (no explicit class), uses the symbolic handler class.
 * For unhandled service codes, returns an error with CIP_STATUS_INVALID_SERVICE.
 *
 * @param registry Registry to dispatch through.
 * @param path Parsed CIP path identifying target class and instance.
 * @param service_code Service code to dispatch.
 * @param input Request data buffer.
 * @param output Response data buffer.
 * @param context_reg Context registry passed to handler.
 * @return UTIL_OK if handler succeeded, error code otherwise.
 */
util_err_t cip_class_registry_dispatch_service(
    cip_class_registry_t *registry,
    cip_path_t *path,
    cip_service_code_t service_code,
    buf_t *input,
    buf_t *output,
    context_registry_t *context_reg
);

#ifdef __cplusplus
}
#endif
