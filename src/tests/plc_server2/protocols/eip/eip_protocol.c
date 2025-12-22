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

#include <inttypes.h>
#include <string.h>
#include "eip_protocol.h"
#include "../cpf/cpf_protocol.h"
#include "../../plcs/ab/ab_context.h"
#include "../../plcs/ab/ab_plc_logix.h"
#include "../../../utils/err.h"
#include "../../../utils/buf.h"
#include "../../../utils/log.h"

/* ============================================================================
 * Frame Check
 * ============================================================================ */

util_err_t eip_frame_check(buf_t *buf, void *context) {
    (void)context;

    pdlog(LOG_MODULE_EIP_PROTOCOL, LOG_LEVEL_DETAIL, "Checking EIP frame");
    pdlog_bytes(LOG_MODULE_EIP_PROTOCOL, LOG_LEVEL_DETAIL, buf);

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
 * Command Handlers
 * ============================================================================ */

/**
 * Handle RegisterSession command (0x0065)
 *
 * Request:
 *   [24+0-1]  uint16_le  Protocol version (usually 1)
 *   [24+2-3]  uint16_le  Options flags (usually 0)
 *
 * Response:
 *   Same structure + session handle in EIP header
 */
static util_err_t handle_register_session(buf_t *input, buf_t *output, uint32_t *session_handle, ab_plc_context_t *plc, client_context_t *client) {
    if(!plc || !client) {
        pdlog(LOG_MODULE_EIP_PROTOCOL, LOG_LEVEL_WARN, "RegisterSession: missing context");
        return UTIL_EINVAL;
    }

    /* Parse request data */
    uint16_t protocol_ver = 0;
    uint16_t options = 0;
    bool ok = true;

    pdlog(LOG_MODULE_EIP_PROTOCOL, LOG_LEVEL_DETAIL, "RegisterSession request:");
    pdlog_bytes(LOG_MODULE_EIP_PROTOCOL, LOG_LEVEL_DETAIL, input);

    ok &= buf_read_u16_le(input, "protocol_version", &protocol_ver);
    ok &= buf_read_u16_le(input, "options", &options);

    if(!ok) {
        pdlog(LOG_MODULE_EIP_PROTOCOL, LOG_LEVEL_WARN, "failed to read request data");
        return UTIL_EINVAL;
    }

    /* check that the protocol version is supported */
    if(protocol_ver != 1) {
        pdlog(LOG_MODULE_EIP_PROTOCOL, LOG_LEVEL_WARN, "unsupported protocol version %u", protocol_ver);
        return UTIL_EINVAL;
    }

    if(options != 0) {
        pdlog(LOG_MODULE_EIP_PROTOCOL, LOG_LEVEL_WARN, "unsupported options flags 0x%04X", options);
        return UTIL_EINVAL;
    }

    /* Generate session handle (ensure non-zero) */
    static uint32_t next_session = 1;
    *session_handle = next_session++;
    client->eip_session_handle = *session_handle;
    if(*session_handle == 0) { *session_handle = next_session++; }

    pdlog(LOG_MODULE_EIP_PROTOCOL, LOG_LEVEL_DETAIL, "assigned session handle %" PRIx32, *session_handle);

    /* build the return payload */
    ok = buf_write_u16_le(output, "protocol_version", protocol_ver);
    ok &= buf_write_u16_le(output, "options", options);

    if(!ok) {
        pdlog(LOG_MODULE_EIP_PROTOCOL, LOG_LEVEL_WARN, "failed to build response");
        return buf_get_error(output);
    }

    /* Build response header with session handle set */
    return UTIL_OK;
}

/**
 * Handle UnregisterSession command (0x0066)
 *
 * Request: No additional data beyond header
 * Response: Empty (status in header only)
 */
static util_err_t handle_unregister_session(buf_t *input, buf_t *output, uint32_t *session_handle, ab_plc_context_t *plc, client_context_t *client) {
    (void)input;
    (void)output;
    (void)plc;

    if(!client) {
        pdlog(LOG_MODULE_EIP_PROTOCOL, LOG_LEVEL_WARN, "UnregisterSession: missing client context");
        return UTIL_EINVAL;
    }

    /* sanity check to see if the session being closed matches the client's session */
    if(*session_handle == 0) {
        pdlog(LOG_MODULE_EIP_PROTOCOL, LOG_LEVEL_WARN, "UnregisterSession: no session to close");
        return UTIL_EINVAL;
    }

    if(client->eip_session_handle != *session_handle) {
        pdlog(LOG_MODULE_EIP_PROTOCOL, LOG_LEVEL_WARN,
              "UnregisterSession: session handle mismatch (client=0x%08X, request=0x%08X)", client->eip_session_handle,
              *session_handle);
        return UTIL_EINVAL;
    }

    pdlog(LOG_MODULE_EIP_PROTOCOL, LOG_LEVEL_DETAIL, "closing session 0x%08X", *session_handle);

    *session_handle = 0;

    return UTIL_OK;
}

/* ============================================================================
 * Main Dispatcher
 * ============================================================================ */

util_err_t eip_dispatch(buf_t *input, buf_t *output, ab_plc_context_t *plc, client_context_t *client) {
    util_err_t err = UTIL_OK;

    if(!input || !output || !plc || !client) {
        return UTIL_EINVAL;
    }

    pdlog(LOG_MODULE_EIP_PROTOCOL, LOG_LEVEL_DETAIL, "EIP dispatch:");
    pdlog_bytes(LOG_MODULE_EIP_PROTOCOL, LOG_LEVEL_DETAIL, input);

    /* read the EIP encapsulation header */

    bool ok = true;
    uint16_t command;
    uint16_t length;
    uint32_t session_handle;
    uint32_t status;
    uint64_t sender_context;
    uint32_t options;

    ok &= buf_read_u16_le(input, "command", &command);
    ok &= buf_read_u16_le(input, "length", &length);
    ok &= buf_read_u32_le(input, "session_handle", &session_handle);
    ok &= buf_read_u32_le(input, "status", &status);
    ok &= buf_read_u64_le(input, "sender_context", &sender_context);
    ok &= buf_read_u32_le(input, "options", &options);

    if(!ok) {
        pdlog(LOG_MODULE_EIP_PROTOCOL, LOG_LEVEL_WARN, "Failed to read EIP header fields: %s",
              util_err_str(buf_get_error(input)));
        return buf_get_error(input);
    }

    /* set up the response buffer */
    buf_t eip_response_buf = {0};
    if(!buf_reserve_write(output, EIP_HEADER_SIZE, &eip_response_buf)) {
        pdlog(LOG_MODULE_EIP_PROTOCOL, LOG_LEVEL_WARN, "Failed to reserve EIP header space: %s",
              util_err_str(buf_get_error(output)));
        return buf_get_error(output);
    }

    pdlog(LOG_MODULE_EIP_PROTOCOL, LOG_LEVEL_DETAIL, "command=0x%04X, length=%u, session=0x%08X", command, length,
          session_handle);

    /* Dispatch by command */
    switch(command) {
        case EIP_CMD_REGISTER_SESSION:
            pdlog(LOG_MODULE_EIP_PROTOCOL, LOG_LEVEL_DETAIL, "Register Session");
            err = handle_register_session(input, output, &session_handle, plc, client);
            break;

        case EIP_CMD_UNREGISTER_SESSION:
            pdlog(LOG_MODULE_EIP_PROTOCOL, LOG_LEVEL_DETAIL, "Unregister Session");
            err = handle_unregister_session(input, output, &session_handle, plc, client);
            break;

        case EIP_CMD_SEND_RR_DATA:
        case EIP_CMD_SEND_UNIT_DATA:
            pdlog(LOG_MODULE_EIP_PROTOCOL, LOG_LEVEL_DETAIL, "Dispatching CPF packet");
            err = cpf_dispatch(input, output, plc, client);
            break;

        default:
            pdlog(LOG_MODULE_EIP_PROTOCOL, LOG_LEVEL_WARN, "Unsupported EIP command: 0x%04X", command);
            err = UTIL_ENOTSUPPORTED;
            break;
    }

    /* fill in the EIP response header with the correct length and command */
    ok = buf_write_u16_le(&eip_response_buf, "command", command);
    ok &= buf_write_u16_le(&eip_response_buf, "length", (uint16_t)(buf_read_size(output) - EIP_HEADER_SIZE));
    ok &= buf_write_u32_le(&eip_response_buf, "session_handle", session_handle);
    ok &= buf_write_u32_le(&eip_response_buf, "status", (err == UTIL_OK) ? 0 : 0x0001);
    ok &= buf_write_u64_le(&eip_response_buf, "sender_context", sender_context);
    ok &= buf_write_u32_le(&eip_response_buf, "options", options);

    if(!ok) {
        pdlog(LOG_MODULE_EIP_PROTOCOL, LOG_LEVEL_WARN, "Failed to build EIP response header: %s",
              util_err_str(buf_get_error(&eip_response_buf)));
        return buf_get_error(&eip_response_buf);
    }

    return err;
}
