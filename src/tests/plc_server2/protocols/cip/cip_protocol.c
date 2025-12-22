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

#include <string.h>
#include "cip_protocol.h"
#include "cip_path.h"
#include "cip_registry.h"
#include "../../plcs/ab/ab_context.h"
#include "../../plcs/ab/ab_plc_logix.h"
#include "../../../utils/log.h"

/* ============================================================================
 * CIP Request Parsing
 * ============================================================================ */

util_err_t cip_parse_request(buf_t *input, cip_request_t *request) {
    if(!input || !request) { return UTIL_EINVAL; }

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
 * CIP Response Building
 * ============================================================================ */

util_err_t cip_build_response(buf_t *output, uint8_t service, uint8_t status) {
    if(!output) { return UTIL_EINVAL; }

    bool ok = true;
    /* Reply service is original service + 0x80 */
    ok &= buf_write_u8(output, "service", service | 0x80);
    /* Reserved byte */
    ok &= buf_write_u8(output, "reserved", 0x00);
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
 * CIP Message Router Dispatcher
 * ============================================================================ */

util_err_t cip_dispatch(buf_t *input, buf_t *output, ab_plc_context_t *plc, client_context_t *client) {
    if(!input || !output || !plc || !client) { return UTIL_EINVAL; }

    pdlog(LOG_MODULE_CIP_ROUTER, LOG_LEVEL_DETAIL, "CIP dispatch:");
    pdlog_bytes(LOG_MODULE_CIP_ROUTER, LOG_LEVEL_DETAIL, input);

    if(!plc->cip_registry) {
        pdlog(LOG_MODULE_CIP_ROUTER, LOG_LEVEL_WARN, "CIP: missing CIP registry");
        cip_build_response(output, 0x00, CIP_STATUS_INVALID_PARAM);
        return UTIL_EINVAL;
    }

    /* Parse CIP request header (service + path size) */
    cip_request_t request = {0};
    util_err_t err = cip_parse_request(input, &request);
    if(err != UTIL_OK) {
        pdlog(LOG_MODULE_CIP_ROUTER, LOG_LEVEL_WARN, "failed to parse CIP request");
        cip_build_response(output, 0x00, CIP_STATUS_INVALID_PARAM);
        return err;
    }

    /* Parse CIP path */
    cip_path_t path = {0};
    err = cip_parse_path(input, request.path_size, &path);
    if(err != UTIL_OK) {
        pdlog(LOG_MODULE_CIP_ROUTER, LOG_LEVEL_WARN, "failed to parse CIP path: %s", util_err_str(err));
        cip_build_response(output, request.service, CIP_STATUS_PATH_DEST_UNKNOWN);
        return err;
    }

    /* Extract class ID from first segment, or use default for symbolic */
    uint8_t class_id = 0x00;

    if(path.segment_count > 0) {
        if(path.segments[0].type == CIP_SEGMENT_LOGICAL_CLASS_8BIT) {
            class_id = (uint8_t)path.segments[0].logical.id;
        } else if(path.segments[0].type == CIP_SEGMENT_SYMBOLIC) {
            /* Symbolic segment - use default symbolic handler class */
            /* The registry will route this using the configured symbolic handler */
            class_id = 0xFF; /* Special marker for symbolic routing */
        }
    }

    pdlog(LOG_MODULE_CIP_ROUTER, LOG_LEVEL_DETAIL, "CIP dispatch: service=0x%02X class=0x%02X", request.service, class_id);

    /* Reserve space for CIP response header */
    buf_t response_header_buf = {0};
    if(!buf_reserve_write(output, 4, &response_header_buf)) {
        pdlog(LOG_MODULE_CIP_ROUTER, LOG_LEVEL_ERROR, "CIP: failed to reserve response header space");
        return buf_get_error(output);
    }

    /* Dispatch through CIP registry */
    err = cip_class_registry_dispatch_service(plc->cip_registry, &path, request.service, input, output, plc, client);
    if(err != UTIL_OK) { pdlog(LOG_MODULE_CIP_ROUTER, LOG_LEVEL_WARN, "CIP dispatch failed: %s", util_err_str(err)); }

    /* Build response header based on result */
    uint8_t status = (err == UTIL_OK) ? CIP_STATUS_OK : CIP_STATUS_INVALID_PARAM;
    cip_build_response(&response_header_buf, request.service, status);

    pdlog(LOG_MODULE_CIP_ROUTER, LOG_LEVEL_DETAIL, "CIP dispatch complete: %s", util_err_str(err));
    pdlog_bytes(LOG_MODULE_CIP_ROUTER, LOG_LEVEL_DETAIL, output);

    return err;
}
