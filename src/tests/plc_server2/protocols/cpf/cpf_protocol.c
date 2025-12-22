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
#include "cpf_protocol.h"
#include "../cip/cip_protocol.h"
#include "../../plcs/ab/ab_plc_logix.h"
#include "../../../utils/log.h"

/* ============================================================================
 * CPF Dispatch Router
 * ============================================================================ */

util_err_t cpf_dispatch(buf_t *input, buf_t *output, ab_plc_context_t *plc, client_context_t *client) {
    if(!input || !output || !plc || !client) { return UTIL_EINVAL; }

    bool is_connected = false;
    uint16_t sequence_number = 0;
    uint32_t connection_id = 0;
    size_t initial_read_size = buf_read_size(input);
    size_t cpf_header_size = 0;
    bool ok = true;

    pdlog(LOG_MODULE_CPF_PROTOCOL, LOG_LEVEL_DETAIL, "parsing CPF packet:");
    pdlog_bytes(LOG_MODULE_CPF_PROTOCOL, LOG_LEVEL_DETAIL, input);

    /* Read in the CPF header data */

    uint32_t interface_handle;
    uint16_t router_timeout;
    uint16_t item_count;

    ok = true;
    ok &= buf_read_u32_le(input, "interface_handle", &interface_handle);
    ok &= buf_read_u16_le(input, "router_timeout", &router_timeout);
    ok &= buf_read_u16_le(input, "item_count", &item_count);

    if(!ok) {
        pdlog(LOG_MODULE_CPF_PROTOCOL, LOG_LEVEL_WARN, "failed to read CPF header");
        return buf_get_error(input);
    }

    pdlog(LOG_MODULE_CPF_PROTOCOL, LOG_LEVEL_DETAIL, "CPF header: interface_handle=0x%08X, router_timeout=%u, item_count=%u",
          interface_handle, router_timeout, item_count);

    /* sanity check */
    if(item_count != 2) {
        pdlog(LOG_MODULE_CPF_PROTOCOL, LOG_LEVEL_WARN, "Item count=2 supported, got %u", item_count);
        return UTIL_EINVAL;
    }

    /* Parse each item */
    for(uint16_t i = 0; i < item_count; i++) {
        uint16_t item_type = 0;
        uint16_t item_length = 0;
        size_t data_header_size = 0;

        ok = buf_read_u16_le(input, "item_type", &item_type);
        ok &= buf_read_u16_le(input, "item_length", &item_length);

        if(!ok) {
            pdlog(LOG_MODULE_CPF_PROTOCOL, LOG_LEVEL_WARN, "failed to read item %u header", i);
            return buf_get_error(input);
        }

        /* Verify we have all the data */
        if(buf_read_size(input) < item_length) {
            pdlog(LOG_MODULE_CPF_PROTOCOL, LOG_LEVEL_WARN, "item %u length %u exceeds buffer", i, item_length);
            return UTIL_EBOUNDS;
        }

        /* process the items */
        switch(item_type) {
            case CPF_ITEM_NULL_ADDRESS:
                /* there is no other data than the type and length. */
                break;

            case CPF_ITEM_UNCONNECTED_DATA:
                /* no other data than the type and length. */
                data_header_size = CPF_ITEM_UCONN_DATA_SIZE;
                break;

            case CPF_ITEM_CONNECTED_ADDRESS:
                /* check item length */
                if(item_length != 4) {
                    pdlog(LOG_MODULE_CPF_PROTOCOL, LOG_LEVEL_WARN, "connected address item length %u != 4", item_length);
                    return UTIL_EINVAL;
                }

                if(!buf_read_u32_le(input, "connection_id", &connection_id)) {
                    pdlog(LOG_MODULE_CPF_PROTOCOL, LOG_LEVEL_WARN, "failed to read connection ID from connected address item");
                    return buf_get_error(input);
                }

                is_connected = true;
                break;

            case CPF_ITEM_CONNECTED_DATA:
                /* minimum length is 2 (sequence number) */
                if(item_length < 2) {
                    pdlog(LOG_MODULE_CPF_PROTOCOL, LOG_LEVEL_WARN, "connected data item length %u < 2", item_length);
                    return UTIL_EINVAL;
                }

                /* get the connection sequence number */
                if(!buf_read_u16_le(input, "sequence_number", &sequence_number)) {
                    pdlog(LOG_MODULE_CPF_PROTOCOL, LOG_LEVEL_WARN, "failed to read sequence number from connected data item");
                    return buf_get_error(input);
                }

                data_header_size = CPF_ITEM_CONN_DATA_SIZE;

                is_connected = true;
                break;

            default:
                pdlog(LOG_MODULE_CPF_PROTOCOL, LOG_LEVEL_WARN, "unsupported item %u type 0x%04X", i, item_type);
                return UTIL_ENOTSUPPORTED;
        }

        pdlog(LOG_MODULE_CPF_PROTOCOL, LOG_LEVEL_DETAIL, "item %u type=0x%04X length=%u", i, item_type, item_length);
    }

    buf_t cpf_header_buf = {0};

    /* reserve as much space as was read from the input buffer */
    if(!buf_reserve_write(output, initial_read_size - buf_read_size(input), &cpf_header_buf)) {
        pdlog(LOG_MODULE_CPF_PROTOCOL, LOG_LEVEL_WARN, "failed to reserve space for CPF header: %s",
              util_err_str(buf_get_error(output)));
        return buf_get_error(output);
    }


    size_t write_size = buf_write_size(output);

    util_err_t err = cip_dispatch(input, output, plc, client);
    if(err != UTIL_OK) {
        pdlog(LOG_MODULE_CPF_PROTOCOL, LOG_LEVEL_DETAIL, "cip_dispatch() returned error %s", util_err_str(err));
        /* Continue - CIP response contains error code */
    }

    /* calculate the CIP response size */
    size_t cip_response_size = write_size - buf_write_size(output);

    pdlog(LOG_MODULE_CPF_PROTOCOL, LOG_LEVEL_DETAIL, "CIP response size %zu bytes", cip_response_size);

    /* start writing out the CPF header */
    ok = true;
    ok &= buf_write_u32_le(&cpf_header_buf, "interface_handle", interface_handle);
    ok &= buf_write_u16_le(&cpf_header_buf, "router_timeout", router_timeout);

    ok &= buf_write_u16_le(&cpf_header_buf, "item_count", 2);

    /* now write the items */
    if(is_connected) {
        pdlog(LOG_MODULE_CPF_PROTOCOL, LOG_LEVEL_DETAIL, "Writing connected address item.");

        /* address item */
        ok &= buf_write_u16_le(&cpf_header_buf, "address_type", CPF_ITEM_CONNECTED_ADDRESS);
        ok &= buf_write_u16_le(&cpf_header_buf, "address_length", 4);
        ok &= buf_write_u32_le(&cpf_header_buf, "connection_id_echo", connection_id);

        /* data item */
        pdlog(LOG_MODULE_CPF_PROTOCOL, LOG_LEVEL_DETAIL, "Writing connected data item.");

        ok &= buf_write_u16_le(&cpf_header_buf, "data_type", CPF_ITEM_CONNECTED_DATA);
        ok &= buf_write_u16_le(&cpf_header_buf, "data_length", (uint16_t)(2 + cip_response_size)); /* +2 for sequence number */
        ok &= buf_write_u16_le(&cpf_header_buf, "sequence_number", sequence_number);
    } else {
        pdlog(LOG_MODULE_CPF_PROTOCOL, LOG_LEVEL_DETAIL, "Writing unconnected address item.");

        /* null address item */
        ok &= buf_write_u16_le(&cpf_header_buf, "address_type", CPF_ITEM_NULL_ADDRESS);
        ok &= buf_write_u16_le(&cpf_header_buf, "address_length", 0);

        pdlog(LOG_MODULE_CPF_PROTOCOL, LOG_LEVEL_DETAIL, "Writing unconnected data item.");

        /* unconnected data item */
        ok &= buf_write_u16_le(&cpf_header_buf, "data_type", CPF_ITEM_UNCONNECTED_DATA);
        ok &= buf_write_u16_le(&cpf_header_buf, "data_length", (uint16_t)cip_response_size);
    }

    if(!ok) {
        pdlog(LOG_MODULE_CPF_PROTOCOL, LOG_LEVEL_ERROR, "failed to build response: %s",
              util_err_str(buf_get_error(&cpf_header_buf)));
        return buf_get_error(&cpf_header_buf);
    }

    pdlog(LOG_MODULE_CPF_PROTOCOL, LOG_LEVEL_DETAIL, "Output with CPF header and CIP response:");
    pdlog_bytes(LOG_MODULE_CPF_PROTOCOL, LOG_LEVEL_DETAIL, output);

    return UTIL_OK;
}
