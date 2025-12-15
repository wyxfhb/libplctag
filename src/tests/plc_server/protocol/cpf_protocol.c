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
#include "cip_message_router.h"
#include "../plc_context.h"
#include "../../utils/log.h"

/* ============================================================================
 * CPF Parsing
 * ============================================================================ */

util_err_t cpf_parse_packet(buf_t *input, cpf_packet_t *packet) {
    memset(packet, 0, sizeof(*packet));

    /* Read item count */
    if(!buf_read_u16_le(input, "item_count", &packet->item_count)) {
        pdlog(LOG_MODULE_CPF_PROTOCOL, LOG_LEVEL_WARN, "CPF: failed to read item count");
        return buf_get_error(input);
    }

    if(packet->item_count > 8) {
        pdlog(LOG_MODULE_CPF_PROTOCOL, LOG_LEVEL_WARN, "CPF: item count %u exceeds max 8", packet->item_count);
        return UTIL_EINVAL;
    }

    pdlog(LOG_MODULE_CPF_PROTOCOL, LOG_LEVEL_DETAIL, "CPF: parsing %u items", packet->item_count);

    /* Parse each item */
    for(uint16_t i = 0; i < packet->item_count; i++) {
        cpf_item_t *item = &packet->items[i];

        /* Read item type and length */
        if(!buf_read_u16_le(input, "item_type", &item->type_id)) {
            pdlog(LOG_MODULE_CPF_PROTOCOL, LOG_LEVEL_WARN, "CPF: failed to read item %u type", i);
            return buf_get_error(input);
        }

        if(!buf_read_u16_le(input, "item_length", &item->length)) {
            pdlog(LOG_MODULE_CPF_PROTOCOL, LOG_LEVEL_WARN, "CPF: failed to read item %u length", i);
            return buf_get_error(input);
        }

        /* Store pointer to item data */
        item->data = buf_read_ptr(input);

        /* Verify we have the data */
        if(buf_read_size(input) < item->length) {
            pdlog(LOG_MODULE_CPF_PROTOCOL, LOG_LEVEL_WARN, "CPF: item %u length %u exceeds buffer", i, item->length);
            return UTIL_EBOUNDS;
        }

        /* Advance past item data */
        buf_read_advance(input, item->length);

        pdlog(LOG_MODULE_CPF_PROTOCOL, LOG_LEVEL_DETAIL, "CPF: item %u type=0x%04X length=%u", i, item->type_id, item->length);
    }

    return UTIL_OK;
}

/* ============================================================================
 * CPF Unconnected Message Handler
 * ============================================================================ */

static util_err_t cpf_dispatch_unconnected(cpf_packet_t *packet, buf_t *input, buf_t *output, plc_context_t *plc) {
    (void)input;

    /* Find unconnected data item (0x00B2) */
    cpf_item_t *data_item = NULL;
    for(uint16_t i = 0; i < packet->item_count; i++) {
        if(packet->items[i].type_id == CPF_ITEM_UNCONNECTED_DATA) {
            data_item = &packet->items[i];
            break;
        }
    }

    if(!data_item) {
        pdlog(LOG_MODULE_CPF_PROTOCOL, LOG_LEVEL_WARN, "CPF dispatch: no unconnected data item found");
        return UTIL_ENOTFOUND;
    }

    pdlog(LOG_MODULE_CPF_PROTOCOL, LOG_LEVEL_DETAIL, "CPF dispatch: unconnected message, routing CIP message (%u bytes)", data_item->length);

    /* Create a buffer view for the CIP message */
    uint8_t cip_request_data[2048];
    memcpy(cip_request_data, data_item->data, data_item->length);
    buf_t cip_request = buf_init(cip_request_data, sizeof(cip_request_data));
    cip_request.write = data_item->length;

    /* Create output buffer for CIP response */
    uint8_t cip_response_data[2048];
    buf_t cip_response = buf_init(cip_response_data, sizeof(cip_response_data));

    /* Dispatch to CIP message router */
    util_err_t err = cip_message_router_dispatch(&cip_request, &cip_response, plc);

    if(err != UTIL_OK) {
        pdlog(LOG_MODULE_CPF_PROTOCOL, LOG_LEVEL_DETAIL, "CPF dispatch: CIP router returned %s", util_err_str(err));
        /* Continue - CIP response contains error code */
    }

    /* Build CPF response */
    /* Item count: 2 (Null Address + Unconnected Data) */
    bool ok = true;
    ok &= buf_write_u16_le(output, "item_count", 2);

    /* Item 1: Null Address (0x0000, length 0) */
    ok &= buf_write_u16_le(output, "address_type", CPF_ITEM_NULL_ADDRESS);
    ok &= buf_write_u16_le(output, "address_length", 0);

    /* Item 2: Unconnected Data (0x00B2) with CIP response */
    ok &= buf_write_u16_le(output, "data_type", CPF_ITEM_UNCONNECTED_DATA);
    ok &= buf_write_u16_le(output, "data_length", (uint16_t)buf_write_pos(&cip_response));

    /* Write CIP response data */
    ok &= buf_write_bytes(output, "cip_data", cip_response_data, buf_write_pos(&cip_response));

    if(!ok) {
        pdlog(LOG_MODULE_CPF_PROTOCOL, LOG_LEVEL_ERROR, "CPF dispatch: failed to build response: %s",
              buf_error_string(buf_get_error(output)));
        return buf_get_error(output);
    }

    return UTIL_OK;
}

/* ============================================================================
 * CPF Connected Message Handler
 * ============================================================================ */

static util_err_t cpf_dispatch_connected(cpf_packet_t *packet, buf_t *input, buf_t *output, plc_context_t *plc) {
    (void)input;

    /* Find Connected Address Item (0x00A1) */
    cpf_item_t *addr_item = NULL;
    cpf_item_t *data_item = NULL;

    for(uint16_t i = 0; i < packet->item_count; i++) {
        if(packet->items[i].type_id == CPF_ITEM_CONNECTED_ADDRESS) {
            addr_item = &packet->items[i];
        } else if(packet->items[i].type_id == CPF_ITEM_CONNECTED_DATA) {
            data_item = &packet->items[i];
        }
    }

    if(!addr_item || !data_item) {
        pdlog(LOG_MODULE_CPF_PROTOCOL, LOG_LEVEL_WARN, "CPF dispatch: missing connected address or data item");
        return UTIL_ENOTFOUND;
    }

    /* Address item should contain 4-byte connection ID */
    if(addr_item->length != 4) {
        pdlog(LOG_MODULE_CPF_PROTOCOL, LOG_LEVEL_WARN, "CPF dispatch: connected address item length %u != 4", addr_item->length);
        return UTIL_EINVAL;
    }

    /* Extract connection ID from address item */
    uint32_t conn_id = 0;
    buf_t addr_buf = buf_init(addr_item->data, addr_item->length);
    addr_buf.write = addr_item->length;
    if(!buf_read_u32_le(&addr_buf, "connection_id", &conn_id)) {
        pdlog(LOG_MODULE_CPF_PROTOCOL, LOG_LEVEL_WARN, "CPF dispatch: failed to parse connection ID");
        return UTIL_EINVAL;
    }

    /* Validate connection ID */
    if(conn_id != plc->server_connection_id) {
        pdlog(LOG_MODULE_CPF_PROTOCOL, LOG_LEVEL_WARN, "CPF dispatch: connection ID mismatch (got 0x%08X, expected 0x%08X)",
              conn_id, plc->server_connection_id);
        return UTIL_ENOTFOUND;
    }

    pdlog(LOG_MODULE_CPF_PROTOCOL, LOG_LEVEL_DETAIL, "CPF dispatch: connected message, connection_id=0x%08X", conn_id);

    /* Data item should be at least 2 bytes (sequence number) + CIP message */
    if(data_item->length < 2) {
        pdlog(LOG_MODULE_CPF_PROTOCOL, LOG_LEVEL_WARN, "CPF dispatch: connected data item length %u < 2", data_item->length);
        return UTIL_EINVAL;
    }

    /* Extract sequence number from first 2 bytes of data item */
    uint16_t seq_num = 0;
    buf_t data_buf = buf_init(data_item->data, data_item->length);
    data_buf.write = data_item->length;
    if(!buf_read_u16_le(&data_buf, "sequence_number", &seq_num)) {
        pdlog(LOG_MODULE_CPF_PROTOCOL, LOG_LEVEL_WARN, "CPF dispatch: failed to parse sequence number");
        return UTIL_EINVAL;
    }

    /* CIP message starts after the 2-byte sequence number */
    uint16_t cip_length = data_item->length - 2;

    pdlog(LOG_MODULE_CPF_PROTOCOL, LOG_LEVEL_DETAIL, "CPF dispatch: sequence=%u, routing CIP message (%u bytes)", seq_num, cip_length);

    /* Create a buffer view for the CIP message (skip sequence number) */
    uint8_t cip_request_data[2048];
    if(cip_length > 0) {
        memcpy(cip_request_data, &data_item->data[2], cip_length);
    }
    buf_t cip_request = buf_init(cip_request_data, sizeof(cip_request_data));
    cip_request.write = cip_length;

    /* Create output buffer for CIP response */
    uint8_t cip_response_data[2048];
    buf_t cip_response = buf_init(cip_response_data, sizeof(cip_response_data));

    /* Dispatch to CIP message router */
    util_err_t err = cip_message_router_dispatch(&cip_request, &cip_response, plc);

    if(err != UTIL_OK) {
        pdlog(LOG_MODULE_CPF_PROTOCOL, LOG_LEVEL_DETAIL, "CPF dispatch: CIP router returned %s", util_err_str(err));
        /* Continue - CIP response contains error code */
    }

    /* Build CPF response with connected items */
    /* Item count: 2 (Connected Address + Connected Data) */
    bool ok = true;
    ok &= buf_write_u16_le(output, "item_count", 2);

    /* Item 1: Connected Address Item (0x00A1) with connection ID echo */
    ok &= buf_write_u16_le(output, "address_type", CPF_ITEM_CONNECTED_ADDRESS);
    ok &= buf_write_u16_le(output, "address_length", 4);
    ok &= buf_write_u32_le(output, "connection_id_echo", plc->server_connection_id);

    /* Item 2: Connected Data Item (0x00B1) with sequence number + CIP response */
    uint16_t response_length = (uint16_t)buf_write_pos(&cip_response) + 2;  /* +2 for sequence number */
    ok &= buf_write_u16_le(output, "data_type", CPF_ITEM_CONNECTED_DATA);
    ok &= buf_write_u16_le(output, "data_length", response_length);

    /* Write sequence number echo */
    ok &= buf_write_u16_le(output, "sequence_echo", seq_num);

    /* Write CIP response data */
    ok &= buf_write_bytes(output, "cip_data", cip_response_data, buf_write_pos(&cip_response));

    if(!ok) {
        pdlog(LOG_MODULE_CPF_PROTOCOL, LOG_LEVEL_ERROR, "CPF dispatch: failed to build response: %s",
              buf_error_string(buf_get_error(output)));
        return buf_get_error(output);
    }

    pdlog(LOG_MODULE_CPF_PROTOCOL, LOG_LEVEL_DETAIL, "CPF dispatch: connected response complete, %u bytes", buf_write_pos(output));

    return UTIL_OK;
}

/* ============================================================================
 * CPF Dispatch Router
 * ============================================================================ */

util_err_t cpf_dispatch(cpf_packet_t *packet, buf_t *input, buf_t *output, plc_context_t *plc) {
    /* Determine if this is a connected or unconnected message */
    bool is_connected = false;

    for(uint16_t i = 0; i < packet->item_count; i++) {
        if(packet->items[i].type_id == CPF_ITEM_CONNECTED_ADDRESS ||
           packet->items[i].type_id == CPF_ITEM_CONNECTED_DATA) {
            is_connected = true;
            break;
        }
    }

    if(is_connected) {
        pdlog(LOG_MODULE_CPF_PROTOCOL, LOG_LEVEL_DETAIL, "CPF dispatch: routing to connected message handler");
        return cpf_dispatch_connected(packet, input, output, plc);
    } else {
        pdlog(LOG_MODULE_CPF_PROTOCOL, LOG_LEVEL_DETAIL, "CPF dispatch: routing to unconnected message handler");
        return cpf_dispatch_unconnected(packet, input, output, plc);
    }
}
