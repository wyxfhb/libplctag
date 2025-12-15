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
#include "cpf_protocol.h"
#include "../../utils/log.h"
#include "plc_context.h"

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
 * Header Parsing
 * ============================================================================ */

util_err_t eip_parse_header(buf_t *input, eip_header_t *header) {
    /* Verify minimum size */
    if(buf_read_size(input) < EIP_HEADER_SIZE) { return UTIL_EAGAIN; }

    bool ok = true;

    ok &= buf_read_u16_le(input, "command", &header->command);
    ok &= buf_read_u16_le(input, "length", &header->length);
    ok &= buf_read_u32_le(input, "session_handle", &header->session_handle);
    ok &= buf_read_u32_le(input, "status", &header->status);
    ok &= buf_read_u64_le(input, "sender_context", &header->sender_context);
    ok &= buf_read_u32_le(input, "options", &header->options);

    if(!ok) {
        pdlog(LOG_MODULE_EIP_PROTOCOL, LOG_LEVEL_ERROR, "Failed to parse EIP header: %s", buf_error_string(buf_get_error(input)));
        return buf_get_error(input);
    }

    /* Validate length doesn't exceed remaining buffer */
    if(header->length > buf_read_size(input)) {
        pdlog(LOG_MODULE_EIP_PROTOCOL, LOG_LEVEL_WARN, "EIP header specifies %u bytes but only %zu available", header->length,
              buf_read_size(input));
        return UTIL_EAGAIN;
    }

    return UTIL_OK;
}

/* ============================================================================
 * Response Header Building
 * ============================================================================ */

util_err_t eip_build_response_header(buf_t *output, const eip_header_t *req, uint32_t status, uint16_t data_length) {
    bool ok = true;

    /* Echo command */
    ok &= buf_write_u16_le(output, "command", req->command);

    /* Write data length */
    ok &= buf_write_u16_le(output, "length", data_length);

    /* Echo session handle */
    ok &= buf_write_u32_le(output, "session_handle", req->session_handle);

    /* Write status */
    ok &= buf_write_u32_le(output, "status", status);

    /* Echo sender context */
    ok &= buf_write_u64_le(output, "sender_context", req->sender_context);

    /* Echo options */
    ok &= buf_write_u32_le(output, "options", req->options);

    if(!ok) {
        pdlog(LOG_MODULE_EIP_PROTOCOL, LOG_LEVEL_ERROR, "Failed to build EIP header: %s",
              buf_error_string(buf_get_error(output)));
        return buf_get_error(output);
    }

    return UTIL_OK;
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
static util_err_t handle_register_session(buf_t *input, buf_t *output, eip_session_t *session, const eip_header_t *req_header,
                                          plc_context_t *plc) {
    (void)plc;

    /* Parse request data */
    uint16_t protocol_ver = 1;
    uint16_t options = 0;

    if(!buf_read_u16_le(input, "protocol_version", &protocol_ver)) {
        pdlog(LOG_MODULE_EIP_PROTOCOL, LOG_LEVEL_WARN, "RegisterSession: failed to read protocol version");
        return UTIL_EINVAL;
    }

    if(!buf_read_u16_le(input, "options", &options)) {
        pdlog(LOG_MODULE_EIP_PROTOCOL, LOG_LEVEL_WARN, "RegisterSession: failed to read options");
        return UTIL_EINVAL;
    }

    /* Generate session handle (ensure non-zero) */
    static uint32_t next_session = 1;
    session->session_handle = next_session++;
    if(session->session_handle == 0) { session->session_handle = next_session++; }
    session->registered = true;

    pdlog(LOG_MODULE_EIP_PROTOCOL, LOG_LEVEL_DETAIL, "RegisterSession: assigned handle 0x%08X", session->session_handle);

    /* Build response header with session handle set */
    eip_header_t resp_header = *req_header;
    resp_header.session_handle = session->session_handle;
    eip_build_response_header(output, &resp_header, 0, 4);

    /* Write response data */
    bool ok = true;
    ok &= buf_write_u16_le(output, "protocol_version", protocol_ver);
    ok &= buf_write_u16_le(output, "options", 0);

    if(!ok) {
        pdlog(LOG_MODULE_EIP_PROTOCOL, LOG_LEVEL_ERROR, "RegisterSession: failed to write response");
        return buf_get_error(output);
    }

    return UTIL_OK;
}

/**
 * Handle UnregisterSession command (0x0066)
 *
 * Request: No additional data beyond header
 * Response: Empty (status in header only)
 */
static util_err_t handle_unregister_session(buf_t *input, buf_t *output, eip_session_t *session, const eip_header_t *req_header,
                                            plc_context_t *plc) {
    (void)input;
    (void)plc;

    pdlog(LOG_MODULE_EIP_PROTOCOL, LOG_LEVEL_DETAIL, "UnregisterSession: closing session 0x%08X", session->session_handle);

    session->registered = false;

    /* Build response header with no data */
    eip_build_response_header(output, req_header, 0, 0);

    return UTIL_OK;
}

/**
 * Handle ListServices command (0x0004)
 *
 * Request: No additional data
 * Response: Service list (stub for now)
 */
static util_err_t handle_list_services(buf_t *input, buf_t *output, const eip_header_t *req_header, plc_context_t *plc) {
    (void)input;
    (void)plc;

    pdlog(LOG_MODULE_EIP_PROTOCOL, LOG_LEVEL_DETAIL, "ListServices");

    /* For now, respond with empty service list */
    eip_build_response_header(output, req_header, 0, 0);

    return UTIL_OK;
}

/**
 * Handle SendRRData command (0x006F - Unconnected)
 * Handle SendUnitData command (0x0070 - Connected)
 *
 * Request:
 *   [24+0-3]  uint32_le  Interface handle (0 for Ethernet/IP)
 *   [24+4-5]  uint16_le  Timeout (ms)
 *   [24+6+]   CPF packet (item count + items)
 *
 * Response:
 *   [24+0-3]  uint32_le  Interface handle
 *   [24+4-5]  uint16_le  Timeout
 *   [24+6+]   CPF packet with CIP response
 */
static util_err_t handle_send_data(buf_t *input, buf_t *output, const eip_header_t *req_header, plc_context_t *plc) {
    util_err_t err = UTIL_OK;

    /* Read interface handle and timeout */
    uint32_t interface_handle = 0;
    uint16_t timeout = 0;

    if(!buf_read_u32_le(input, "interface_handle", &interface_handle)) {
        pdlog(LOG_MODULE_EIP_PROTOCOL, LOG_LEVEL_WARN, "SendData: failed to read interface handle");
        return UTIL_EINVAL;
    }

    if(!buf_read_u16_le(input, "timeout", &timeout)) {
        pdlog(LOG_MODULE_EIP_PROTOCOL, LOG_LEVEL_WARN, "SendData: failed to read timeout");
        return UTIL_EINVAL;
    }

    const char *msg_type = (req_header->command == EIP_CMD_SEND_RR_DATA) ? "unconnected (RRData)" : "connected (UnitData)";
    pdlog(LOG_MODULE_EIP_PROTOCOL, LOG_LEVEL_DETAIL, "SendData: %s messaging, interface=0x%08X timeout=%u", msg_type, interface_handle, timeout);

    /* Parse CPF packet */
    cpf_packet_t cpf;
    err = cpf_parse_packet(input, &cpf);
    if(err != UTIL_OK) {
        pdlog(LOG_MODULE_EIP_PROTOCOL, LOG_LEVEL_WARN, "SendData: CPF parse failed: %s", util_err_str(err));
        eip_build_response_header(output, req_header, 0x0008, 6); /* Service not supported */
        buf_write_u32_le(output, "interface_handle", interface_handle);
        buf_write_u16_le(output, "timeout", timeout);
        return err;
    }

    /* Create temporary buffer for CPF response */
    uint8_t cpf_response_data[4096];
    buf_t cpf_response = buf_init(cpf_response_data, sizeof(cpf_response_data));

    /* Dispatch through CPF to CIP */
    err = cpf_dispatch(&cpf, input, &cpf_response, plc);

    if(err != UTIL_OK) {
        pdlog(LOG_MODULE_EIP_PROTOCOL, LOG_LEVEL_DETAIL, "SendData: CPF dispatch returned %s", util_err_str(err));
        /* Continue - response was built */
    }

    /* Build EIP response header */
    uint16_t data_length = (uint16_t)((size_t)6 + buf_write_pos(&cpf_response));
    eip_build_response_header(output, req_header, 0, data_length);

    /* Write response data */
    bool ok = true;
    ok &= buf_write_u32_le(output, "interface_handle", interface_handle);
    ok &= buf_write_u16_le(output, "timeout", timeout);
    ok &= buf_write_bytes(output, "cpf_data", cpf_response_data, buf_write_pos(&cpf_response));

    if(!ok) {
        pdlog(LOG_MODULE_EIP_PROTOCOL, LOG_LEVEL_ERROR, "SendData: failed to build response");
        return buf_get_error(output);
    }

    return UTIL_OK;
}

/* ============================================================================
 * Main Dispatcher
 * ============================================================================ */

util_err_t eip_dispatch(buf_t *input, buf_t *output, eip_session_t *session, plc_context_t *plc) {
    util_err_t err = UTIL_OK;

    /* Parse EIP header */
    eip_header_t req_header;
    err = eip_parse_header(input, &req_header);
    if(err != UTIL_OK) {
        pdlog(LOG_MODULE_EIP_PROTOCOL, LOG_LEVEL_WARN, "EIP dispatch: failed to parse header");
        return err;
    }

    pdlog(LOG_MODULE_EIP_PROTOCOL, LOG_LEVEL_DETAIL, "EIP dispatch: command=0x%04X, length=%u, session=0x%08X",
          req_header.command, req_header.length, req_header.session_handle);

    /* Dispatch by command */
    switch(req_header.command) {
        case EIP_CMD_REGISTER_SESSION:
            pdlog(LOG_MODULE_EIP_PROTOCOL, LOG_LEVEL_DETAIL, "EIP dispatch: Register Session");
            err = handle_register_session(input, output, session, &req_header, plc);
            break;

        case EIP_CMD_UNREGISTER_SESSION:
            pdlog(LOG_MODULE_EIP_PROTOCOL, LOG_LEVEL_DETAIL, "EIP dispatch: Unregister Session");
            err = handle_unregister_session(input, output, session, &req_header, plc);
            break;

        case EIP_CMD_LIST_SERVICES:
            pdlog(LOG_MODULE_EIP_PROTOCOL, LOG_LEVEL_DETAIL, "EIP dispatch: List Services");
            err = handle_list_services(input, output, &req_header, plc);
            break;

        case EIP_CMD_SEND_RR_DATA:
        case EIP_CMD_SEND_UNIT_DATA:
            pdlog(LOG_MODULE_EIP_PROTOCOL, LOG_LEVEL_DETAIL, "EIP dispatch: Send Data");
            err = handle_send_data(input, output, &req_header, plc);
            break;

        default:
            pdlog(LOG_MODULE_EIP_PROTOCOL, LOG_LEVEL_WARN, "Unsupported EIP command: 0x%04X", req_header.command);
            eip_build_response_header(output, &req_header, 0x0001, 0);
            err = UTIL_ENOTSUPPORTED;
            break;
    }

    return err;
}
