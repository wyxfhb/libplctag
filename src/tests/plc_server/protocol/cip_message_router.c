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
#include "../../utils/log.h"
#include "../../utils/buf.h"
#include "plc_context.h"

/* ============================================================================
 * Request Parsing
 * ============================================================================ */

util_err_t cip_parse_request(buf_t *input, cip_request_t *request) {
    bool ok = true;

    ok &= buf_read_u8(input, "service", &request->service);
    ok &= buf_read_u8(input, "path_size", &request->path_size);

    if(!ok) {
        pdlog(LOG_MODULE_CIP_ROUTER, LOG_LEVEL_WARN, "CIP: failed to parse request header");
        return buf_get_error(input);
    }

    pdlog(LOG_MODULE_CIP_ROUTER, LOG_LEVEL_DETAIL, "CIP: service=0x%02X, path_size=%u words", request->service,
          request->path_size);

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

    if(!ok) {
        pdlog(LOG_MODULE_CIP_ROUTER, LOG_LEVEL_ERROR, "CIP: failed to build response header");
        return buf_get_error(output);
    }

    return UTIL_OK;
}

/* ============================================================================
 * Main Dispatcher
 * ============================================================================ */

util_err_t cip_message_router_dispatch(buf_t *input, buf_t *output, plc_context_t *plc) {
    /* Parse CIP request header */
    cip_request_t request;
    util_err_t err = cip_parse_request(input, &request);
    if(err != UTIL_OK) {
        pdlog(LOG_MODULE_CIP_ROUTER, LOG_LEVEL_WARN, "CIP dispatch: failed to parse request");
        cip_build_response(output, 0x00, CIP_STATUS_INVALID_PARAM);
        return err;
    }

    /* Parse CIP path */
    cip_path_t path;
    err = cip_parse_path(input, request.path_size, &path);
    if(err != UTIL_OK) {
        pdlog(LOG_MODULE_CIP_ROUTER, LOG_LEVEL_WARN, "CIP dispatch: failed to parse path: %s", util_err_str(err));
        cip_build_response(output, request.service, CIP_STATUS_PATH_DEST_UNKNOWN);
        return err;
    }

    /* check the first segment in the path */
    if(path.segment_count == 0) {
        pdlog(LOG_MODULE_CIP_ROUTER, LOG_LEVEL_WARN, "CIP dispatch: empty path");
        cip_build_response(output, request.service, CIP_STATUS_PATH_DEST_UNKNOWN);
        return UTIL_EINVAL;
    }

    /* Check if first segment is a logical class (0x20, 0x21, 0x22) */
    if(path.segments[0].type == CIP_SEGMENT_LOGICAL_CLASS_8BIT ||
       path.segments[0].type == CIP_SEGMENT_LOGICAL_CLASS_16BIT ||
       path.segments[0].type == CIP_SEGMENT_LOGICAL_CLASS_32BIT) {

        uint32_t class_id = path.segments[0].logical.id;
        uint32_t instance_id = 0;

        /* Check if second segment is a logical instance (0x24, 0x25, 0x26) */
        if(path.segment_count > 1 &&
           (path.segments[1].type == CIP_SEGMENT_LOGICAL_INSTANCE_8BIT ||
            path.segments[1].type == CIP_SEGMENT_LOGICAL_INSTANCE_16BIT ||
            path.segments[1].type == CIP_SEGMENT_LOGICAL_INSTANCE_32BIT)) {
            instance_id = path.segments[1].logical.id;
        }

        pdlog(LOG_MODULE_CIP_ROUTER, LOG_LEVEL_DETAIL, "CIP dispatch: routing to class 0x%02X instance 0x%08X",
              class_id, instance_id);

        return cip_registry_dispatch(plc->registry, (uint16_t)class_id, instance_id, request.service, &path, input, output, plc);
    }

    if(path.segments[0].type == CIP_SEGMENT_SYMBOLIC) {
        pdlog(LOG_MODULE_CIP_ROUTER, LOG_LEVEL_DETAIL, "CIP dispatch: routing to Symbol Object (0x6B) for tag '%.*s'",
              (int)path.segments[0].symbolic.length, path.segments[0].symbolic.name);

        /* Micro800 routes all tag accesses through Symbol Object Class 0x6B */
        return cip_registry_dispatch(plc->registry, 0x6B, 0, request.service, &path, input, output, plc);
    }

    /* If we get here, we couldn't route the request */
    pdlog(LOG_MODULE_CIP_ROUTER, LOG_LEVEL_WARN, "CIP dispatch: couldn't determine target object (segment type 0x%02X)",
          path.segments[0].type);
    cip_build_response(output, request.service, CIP_STATUS_PATH_DEST_UNKNOWN);

    return UTIL_ENOTFOUND;
}
