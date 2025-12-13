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

#include "cip_message_router.h"
#include "cip_path.h"
#include "cip_object_registry.h"
#include "log.h"
#include "plc_context.h"

/* ============================================================================
 * Request Parsing
 * ============================================================================ */

util_err_t cip_parse_request(buf_t *input, cip_request_t *request) {
    bool ok = true;

    ok &= buf_read_u8(input, "service", &request->service);
    ok &= buf_read_u8(input, "path_size", &request->path_size);

    if (!ok) {
        pdlog(LOG_MODULE_CIP_ROUTER, LOG_LEVEL_WARN,
              "CIP: failed to parse request header");
        return buf_get_error(input);
    }

    pdlog(LOG_MODULE_CIP_ROUTER, LOG_LEVEL_DETAIL,
          "CIP: service=0x%02X, path_size=%u words",
          request->service, request->path_size);

    return UTIL_OK;
}

/* ============================================================================
 * Response Building
 * ============================================================================ */

util_err_t cip_build_response(buf_t *output, uint8_t service, uint8_t status) {
    bool ok = true;

    /* Reply service = request service | 0x80 */
    ok &= buf_write_u8(output, "reply_service", service | 0x80);

    /* Reserved byte */
    ok &= buf_write_u8(output, "reserved", 0);

    /* Status code */
    ok &= buf_write_u8(output, "status", status);

    /* Extended status size (0 for now) */
    ok &= buf_write_u8(output, "ext_status_size", 0);

    if (!ok) {
        pdlog(LOG_MODULE_CIP_ROUTER, LOG_LEVEL_ERROR,
              "CIP: failed to build response header");
        return buf_get_error(output);
    }

    return UTIL_OK;
}

/* ============================================================================
 * Main Dispatcher
 * ============================================================================ */

util_err_t cip_message_router_dispatch(buf_t *input, buf_t *output,
                                       plc_context_t *plc) {
    /* Parse CIP request header */
    cip_request_t request;
    util_err_t err = cip_parse_request(input, &request);
    if (err != UTIL_OK) {
        pdlog(LOG_MODULE_CIP_ROUTER, LOG_LEVEL_WARN,
              "CIP dispatch: failed to parse request");
        cip_build_response(output, 0x00, CIP_STATUS_INVALID_PARAM);
        return err;
    }

    /* Parse CIP path */
    cip_path_t path;
    err = cip_parse_path(input, request.path_size, &path);
    if (err != UTIL_OK) {
        pdlog(LOG_MODULE_CIP_ROUTER, LOG_LEVEL_WARN,
              "CIP dispatch: failed to parse path: %s", util_err_str(err));
        cip_build_response(output, request.service, CIP_STATUS_PATH_DEST_UNKNOWN);
        return err;
    }

    /* For Micro800: Symbolic segment without class ID means Symbol Object (0x6B)
     * Dispatch to Symbol Object, instance 0 */
    if (path.symbol_name) {
        pdlog(LOG_MODULE_CIP_ROUTER, LOG_LEVEL_DETAIL,
              "CIP dispatch: routing to Symbol Object (0x6B) for tag '%.*s'",
              (int)path.symbol_length, path.symbol_name);

        /* Micro800 routes all tag accesses through Symbol Object Class 0x6B */
        return cip_registry_dispatch(plc->registry, 0x6B, 0,
                                    request.service, &path,
                                    input, output, plc);
    }

    /* If we get here, we couldn't route the request */
    pdlog(LOG_MODULE_CIP_ROUTER, LOG_LEVEL_WARN,
          "CIP dispatch: couldn't determine target object");
    cip_build_response(output, request.service, CIP_STATUS_PATH_DEST_UNKNOWN);

    return UTIL_ENOTFOUND;
}
