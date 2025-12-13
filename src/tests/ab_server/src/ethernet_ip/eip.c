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

#include "eip.h"
#include "cpf.h"
#include "../../../utils/err.h"
#include "../../../utils/buf.h"
#include "../utils.h"
#include "../../../utils/log.h"
#include <stdlib.h>

#define EIP_REGISTER_SESSION ((uint16_t)0x0065)
#define EIP_REGISTER_SESSION_SIZE (4) /* 4 bytes, 2 16-bit words */

#define EIP_UNREGISTER_SESSION ((uint16_t)0x0066)
#define EIP_UNCONNECTED_SEND ((uint16_t)0x006F)
#define EIP_CONNECTED_SEND ((uint16_t)0x0070)

/* supported EIP version */
#define EIP_VERSION ((uint16_t)1)


typedef struct {
    uint16_t command;
    uint16_t length;
    uint32_t session_handle;
    uint32_t status;
    uint64_t sender_context;
    uint32_t options;
} eip_header_s;


static util_err_t register_session(buf_t *input, buf_t *output, plc_s *plc, eip_header_s *header);
static util_err_t unregister_session(buf_t *input, buf_t *output, plc_s *plc, eip_header_s *header);


util_err_t eip_dispatch_request(buf_t *input, buf_t *output, plc_s *plc) {
    eip_header_s header;
    util_err_t handler_err;
    size_t total_input_size = buf_read_size(input);

    pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "eip_dispatch_request(): input size = %zu", total_input_size);

    /* Read EIP header */
    bool ok = true;
    ok &= buf_read_u16_le(input, "command", &header.command);
    ok &= buf_read_u16_le(input, "length", &header.length);
    ok &= buf_read_u32_le(input, "session_handle", &header.session_handle);
    ok &= buf_read_u32_le(input, "status", &header.status);
    ok &= buf_read_u64_le(input, "sender_context", &header.sender_context);
    uint32_t reserved;
    ok &= buf_read_u32_le(input, "reserved", &reserved);  /* skip 4 bytes */
    ok &= buf_read_u32_le(input, "options", &header.options);

    if (!ok) {
        pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Failed to parse EIP header: %s", util_err_str(buf_get_error(input)));
        return buf_get_error(input);
    }

    /* Sanity check: total packet size should be header + payload length */
    if(total_input_size != (size_t)(EIP_HEADER_SIZE + header.length)) {
        pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Illegal EIP packet. Length should be %d but is %zu!", EIP_HEADER_SIZE + header.length, total_input_size);
        return UTIL_EINVAL;
    }

    /* Store sender context for later */
    plc->sender_context = header.sender_context;

    /* Reset output buffer and prepare for response */
    buf_reset(output);

    /* Dispatch based on command */
    switch(header.command) {
        case EIP_REGISTER_SESSION:
            handler_err = register_session(input, output, plc, &header);
            break;

        case EIP_UNREGISTER_SESSION:
            handler_err = unregister_session(input, output, plc, &header);
            break;

        case EIP_UNCONNECTED_SEND:
            handler_err = handle_cpf_unconnected(input, output, plc);
            break;

        case EIP_CONNECTED_SEND:
            handler_err = handle_cpf_connected(input, output, plc);
            break;

        default:
            handler_err = UTIL_ENOTSUPPORTED;
            break;
    }

    /* Build response header */
    buf_reset(output);
    ok = true;
    ok &= buf_write_u16_le(output, "response_command", header.command);
    ok &= buf_write_u16_le(output, "response_length", (uint16_t)(buf_write_pos(output) - 4));  /* Will update later */
    ok &= buf_write_u32_le(output, "response_session", plc->session_handle);

    if (handler_err == UTIL_OK) {
        ok &= buf_write_u32_le(output, "response_status", 0);  /* Success */
    } else if (handler_err == UTIL_ECLOSED) {
        /* Special case: connection closed is not a true error */
        return UTIL_ECLOSED;
    } else {
        ok &= buf_write_u32_le(output, "response_status", (uint32_t)handler_err);
    }

    ok &= buf_write_u64_le(output, "response_context", plc->sender_context);
    ok &= buf_write_u32_le(output, "response_reserved", 0);
    ok &= buf_write_u32_le(output, "response_options", header.options);

    if (!ok) {
        pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Failed to write EIP response header: %s", util_err_str(buf_get_error(output)));
        return buf_get_error(output);
    }

    /* Update the length field in header with actual payload size */
    size_t response_payload_size = buf_write_pos(output) - EIP_HEADER_SIZE;
    uint8_t *response_data = (uint8_t *)buf_read_ptr(output) - buf_read_size(output);
    response_data[2] = (uint8_t)(response_payload_size & 0xFF);
    response_data[3] = (uint8_t)((response_payload_size >> 8) & 0xFF);

    return handler_err == UTIL_OK ? UTIL_OK : UTIL_OK;  /* Return OK - handlers write error status in EIP header */
}


static util_err_t register_session(buf_t *input, buf_t *output, plc_s *plc, eip_header_s *header) {
    struct {
        uint16_t eip_version;
        uint16_t option_flags;
    } register_request;

    bool ok = true;
    ok &= buf_read_u16_le(input, "eip_version", &register_request.eip_version);
    ok &= buf_read_u16_le(input, "option_flags", &register_request.option_flags);

    if (!ok) {
        pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Failed to parse register session request: %s", util_err_str(buf_get_error(input)));
        return buf_get_error(input);
    }

    /* sanity checks.  The command and packet length are checked by now. */

    /* session_handle must be zero. */
    if(header->session_handle != (uint32_t)0) {
        pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Request failed sanity check: request session handle is %u but should be zero.", header->session_handle);
        return UTIL_EINVAL;
    }

    /* session status must be zero. */
    if(header->status != (uint32_t)0) {
        pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Request failed sanity check: request status is %u but should be zero.", header->status);
        return UTIL_EINVAL;
    }

    /* session sender plc must be zero. */
    if(header->sender_context != (uint64_t)0) {
        pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Request failed sanity check: request sender context should be zero.");
        return UTIL_EINVAL;
    }

    /* session options must be zero. */
    if(header->options != (uint32_t)0) {
        pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Request failed sanity check: request options is %u but should be zero.", header->options);
        return UTIL_EINVAL;
    }

    /* EIP version must be 1. */
    if(register_request.eip_version != EIP_VERSION) {
        pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Request failed sanity check: request EIP version is %u but should be %u.", register_request.eip_version, EIP_VERSION);
        return UTIL_EINVAL;
    }

    /* Session request option flags must be zero. */
    if(register_request.option_flags != (uint16_t)0) {
        pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Request failed sanity check: request option flags field is %u but should be zero.", register_request.option_flags);
        return UTIL_EINVAL;
    }

    /* all good, generate a session handle. */
    plc->session_handle = header->session_handle = (uint32_t)(random_u64(UINT32_MAX) + 1);

    /* build the response. */
    ok = true;
    ok &= buf_write_u16_le(output, "response_version", register_request.eip_version);
    ok &= buf_write_u16_le(output, "response_flags", register_request.option_flags);

    if (!ok) {
        pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Failed to write register session response: %s", util_err_str(buf_get_error(output)));
        return buf_get_error(output);
    }

    return UTIL_OK;
}


static util_err_t unregister_session(buf_t *input, buf_t *output, plc_s *plc, eip_header_s *header) {
    (void)input;
    (void)output;

    if(header->session_handle == plc->session_handle) {
        return UTIL_ECLOSED;
    } else {
        return UTIL_EINVAL;
    }
}
