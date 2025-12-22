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
#include "eip_protocol.h"
#include "../cpf/cpf_protocol.h"

/* ============================================================================
 * Frame Check
 * ============================================================================ */

util_err_t eip_frame_check(buf_t *buf, void *context) {
    (void)context;

    size_t available = buf_read_size(buf);

    /* Need at least EIP header (24 bytes) */
    if(available < EIP_HEADER_SIZE) { return UTIL_EAGAIN; }

    /* Peek at header to get data length (non-destructive) */
    buf_t peek = buf_checkpoint(buf);
    uint16_t cmd, length;

    if(!buf_read_u16_le(&peek, "command", &cmd)) { return buf_get_error(&peek); }
    if(!buf_read_u16_le(&peek, "length", &length)) { return buf_get_error(&peek); }

    /* Check if we have complete packet (header + data) */
    if(available >= (EIP_HEADER_SIZE + length)) { return UTIL_OK; }

    return UTIL_EAGAIN;
}

/* ============================================================================
 * Main Dispatcher - Stub
 * ============================================================================
 *
 * TODO: Port from plc_server/protocol/eip_protocol.c
 * - Parse EIP header (command, length, session handle, status, context, options)
 * - Route to command handlers (RegisterSession, UnregisterSession, SendData)
 * - Build response header
 */

util_err_t eip_dispatch(buf_t *input, buf_t *output, context_registry_t *context_reg) {
    if(!input || !output || !context_reg) {
        return UTIL_EINVAL;
    }

    /* Stub: Just return error for now */
    buf_set_error(output, UTIL_ENOTSUPPORTED, "eip_dispatch");
    return UTIL_ENOTSUPPORTED;
}
