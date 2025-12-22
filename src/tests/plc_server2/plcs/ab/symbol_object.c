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

#include "symbol_object.h"

/* ============================================================================
 * Service Handlers - Stubs
 * ============================================================================
 *
 * TODO: Port from plc_server/protocol/objects/logix_symbol_object.c
 *
 * Implement:
 * - handle_read_tag (service 0x4C)
 * - handle_write_tag (service 0x4D)
 * - handle_read_tag_frag (service 0x52)
 * - handle_write_tag_frag (service 0x53)
 *
 * These handlers receive:
 * - service_code: The CIP service code
 * - path: Parsed CIP path (with symbolic tag name)
 * - input: Request data (positioned after standard headers)
 * - output: Response data buffer
 * - context_reg: Context registry to access PLC and client contexts
 */

/* Stub handler - returns not supported */
static util_err_t stub_handler(
    uint8_t service_code,
    cip_path_t *path,
    buf_t *input,
    buf_t *output,
    context_registry_t *context_reg) {
    (void)service_code;
    (void)path;
    (void)input;
    (void)context_reg;

    buf_set_error(output, UTIL_ENOTSUPPORTED, "symbol_object_handler");
    return UTIL_ENOTSUPPORTED;
}

/* ============================================================================
 * Symbol Object Registration
 * ============================================================================ */

util_err_t symbol_object_register(cip_class_registry_t *registry) {
    if(!registry) {
        return UTIL_EINVAL;
    }

    util_err_t err = UTIL_OK;

    /* Register read tag handler (0x4C) */
    err = cip_class_registry_add_service(registry, 0x6B, 0x4C, stub_handler);
    if(err != UTIL_OK) { return err; }

    /* Register write tag handler (0x4D) */
    err = cip_class_registry_add_service(registry, 0x6B, 0x4D, stub_handler);
    if(err != UTIL_OK) { return err; }

    /* Register read tag fragmented handler (0x52) */
    err = cip_class_registry_add_service(registry, 0x6B, 0x52, stub_handler);
    if(err != UTIL_OK) { return err; }

    /* Register write tag fragmented handler (0x53) */
    err = cip_class_registry_add_service(registry, 0x6B, 0x53, stub_handler);
    if(err != UTIL_OK) { return err; }

    return UTIL_OK;
}
