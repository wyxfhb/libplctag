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

#include "cip_protocol.h"

/* ============================================================================
 * CIP Request Parsing - Stub
 * ============================================================================ */

util_err_t cip_parse_request(buf_t *input, cip_request_t *request) {
    if(!input || !request) {
        return UTIL_EINVAL;
    }

    /* TODO: Parse service code and path size from buffer */
    return UTIL_ENOTSUPPORTED;
}

/* ============================================================================
 * CIP Response Building
 * ============================================================================ */

util_err_t cip_build_response(buf_t *output, uint8_t service, uint8_t status) {
    if(!output) {
        return UTIL_EINVAL;
    }

    bool ok = true;
    /* Reply service is original service + 0x80 */
    ok &= buf_write_u8(output, "service", service + 0x80);
    /* Reserved byte */
    ok &= buf_write_u8(output, "reserved", 0x00);
    /* Status code */
    ok &= buf_write_u8(output, "status", status);

    if(!ok) {
        return buf_get_error(output);
    }

    return UTIL_OK;
}

/* ============================================================================
 * CIP Message Router - Stub
 * ============================================================================
 *
 * TODO: Port from plc_server/protocol/cip_message_router.c
 * - Parse CIP request (service code, path size, path, request data)
 * - Parse CIP path (class, instance, attributes)
 * - Route through CIP registry to appropriate class handler
 * - Build response with status and any response data
 */

util_err_t cip_message_router_dispatch(buf_t *input, buf_t *output, context_registry_t *context_reg) {
    if(!input || !output || !context_reg) {
        return UTIL_EINVAL;
    }

    /* Stub: Just return error for now */
    buf_set_error(output, UTIL_ENOTSUPPORTED, "cip_message_router_dispatch");
    return UTIL_ENOTSUPPORTED;
}
