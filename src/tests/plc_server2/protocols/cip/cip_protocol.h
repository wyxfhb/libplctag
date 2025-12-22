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
#include "../../../utils/buf.h"
#include "../../../utils/err.h"
#include "../../context/context_registry.h"
#include "cip_defs.h"
#include "cip_registry.h"
#include "cip_path.h"

/* ============================================================================
 * CIP Request Structure
 * ============================================================================ */

typedef struct {
    uint8_t service;    /* Service code (0x4C, 0x4D, etc.) */
    uint8_t path_size;  /* Path size in words */
    /* Path data follows in buffer */
} cip_request_t;

/* ============================================================================
 * CIP Message Router Functions
 * ============================================================================ */

/**
 * @brief Parse CIP request header from buffer
 *
 * Reads service code and path size.
 * Advances buffer read position accordingly.
 *
 * @param input Buffer positioned at start of CIP message
 * @param request OUT: Parsed request structure
 * @return UTIL_OK on success, error code on failure
 */
util_err_t cip_parse_request(buf_t *input, cip_request_t *request);

/**
 * @brief Main CIP Message Router dispatcher
 *
 * Parses CIP request, routes to target object class via CIP registry,
 * builds response with service reply + status.
 *
 * @param input Buffer positioned at start of CIP message
 * @param output Buffer to write CIP response to
 * @param context_reg Context registry (contains CIP registry, client, PLC)
 * @return UTIL_OK on success, error code on failure
 */
util_err_t cip_message_router_dispatch(buf_t *input, buf_t *output, context_registry_t *context_reg);

/**
 * @brief Build CIP response header
 *
 * Writes reply service code and status to response buffer.
 *
 * @param output Buffer to write response to
 * @param service Original service code
 * @param status CIP status code
 * @return UTIL_OK on success, error code on failure
 */
util_err_t cip_build_response(buf_t *output, uint8_t service, uint8_t status);

#ifdef __cplusplus
}
#endif
