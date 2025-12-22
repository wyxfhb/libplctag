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
#include "../../utils/log.h"


/* ============================================================================
 * CPF (Common Packet Format) Constants
 * ============================================================================ */

/* CPF Item Type IDs */
#define CPF_ITEM_NULL_ADDRESS 0x0000      /* Null Address */
#define CPF_ITEM_CONNECTED_ADDRESS 0x00A1 /* Connected Address Item */
#define CPF_ITEM_CONNECTED_DATA 0x00B1    /* Connected Data Item */
#define CPF_ITEM_UNCONNECTED_DATA 0x00B2  /* Unconnected Data Item (most common) */


/**
 * CPF Item
 *
 * Represents one item in a CPF packet.
 * Data is stored as pointer + length, not copied.
 */
typedef struct {
    uint16_t type_id; /* Item type (CPF_ITEM_*) */
    uint16_t length;  /* Length of data */
    buf_t data;       /* buffer view to item data (in input buffer) */
} cpf_item_t;

/**
 * CPF Packet
 *
 * Represents a parsed CPF packet with its items.
 */
typedef struct {
    uint16_t item_count; /* Number of items in packet */
    cpf_item_t items[8]; /* Item array (max 8 items) */
} cpf_packet_t;


/* ============================================================================
 * CPF Unconnected Message Handler
 * ============================================================================ */

// static util_err_t cpf_dispatch_unconnected(cpf_packet_t *packet, buf_t *input, buf_t *output, plc_context_t *plc) {
//     /* Find unconnected data item (0x00B2) */
//     cpf_item_t *data_item = NULL;
//     for(uint16_t i = 0; i < packet->item_count; i++) {
//         if(packet->items[i].type_id == CPF_ITEM_UNCONNECTED_DATA) {
//             data_item = &packet->items[i];
//             break;
//         }
//     }

//     if(!data_item) {
//         pdlog(LOG_MODULE_CPF_PROTOCOL, LOG_LEVEL_WARN, "CPF dispatch: no unconnected data item found");
//         return UTIL_ENOTFOUND;
//     }

//     pdlog(LOG_MODULE_CPF_PROTOCOL, LOG_LEVEL_DETAIL, "CPF dispatch: unconnected message, routing CIP message (%u bytes)",
//           data_item->length);


//     /* Dispatch to CIP message router */
//     util_err_t err = cip_message_router_dispatch(input, output, plc);

//     if(err != UTIL_OK) {
//         pdlog(LOG_MODULE_CPF_PROTOCOL, LOG_LEVEL_DETAIL, "CPF dispatch: CIP router returned %s", util_err_str(err));
//         /* Continue - CIP response contains error code */
//     }

//     /* Build CPF response */
//     /* Item count: 2 (Null Address + Unconnected Data) */
//     bool ok = true;
//     ok &= buf_write_u16_le(output, "item_count", 2);

//     /* Item 1: Null Address (0x0000, length 0) */
//     ok &= buf_write_u16_le(output, "address_type", CPF_ITEM_NULL_ADDRESS);
//     ok &= buf_write_u16_le(output, "address_length", 0);

//     /* Item 2: Unconnected Data (0x00B2) with CIP response */
//     ok &= buf_write_u16_le(output, "data_type", CPF_ITEM_UNCONNECTED_DATA);
//     ok &= buf_write_u16_le(output, "data_length", (uint16_t)buf_write_pos(&cip_response));

//     /* Write CIP response data */
//     ok &= buf_write_bytes(output, "cip_data", cip_response_data, buf_write_pos(&cip_response));

//     if(!ok) {
//         pdlog(LOG_MODULE_CPF_PROTOCOL, LOG_LEVEL_ERROR, "CPF dispatch: failed to build response: %s",
//               util_err_str(buf_get_error(output)));
//         return buf_get_error(output);
//     }

//     return UTIL_OK;
// }

// /* ============================================================================
//  * CPF Connected Message Handler
//  * ============================================================================ */

// static util_err_t cpf_dispatch_connected(cpf_packet_t *packet, buf_t *input, buf_t *output, plc_context_t *plc) {
//     /* Find Connected Address Item (0x00A1) */
//     cpf_item_t *addr_item = NULL;
//     cpf_item_t *data_item = NULL;

//     for(uint16_t i = 0; i < packet->item_count; i++) {
//         if(packet->items[i].type_id == CPF_ITEM_CONNECTED_ADDRESS) {
//             addr_item = &packet->items[i];
//         } else if(packet->items[i].type_id == CPF_ITEM_CONNECTED_DATA) {
//             data_item = &packet->items[i];
//         }
//     }

//     if(!addr_item || !data_item) {
//         pdlog(LOG_MODULE_CPF_PROTOCOL, LOG_LEVEL_WARN, "CPF dispatch: missing connected address or data item");
//         return UTIL_ENOTFOUND;
//     }

//     /* Address item should contain 4-byte connection ID */
//     if(addr_item->length != 4) {
//         pdlog(LOG_MODULE_CPF_PROTOCOL, LOG_LEVEL_WARN, "CPF dispatch: connected address item length %u != 4",
//         addr_item->length); return UTIL_EINVAL;
//     }

//     /* Extract connection ID from address item */
//     uint32_t conn_id = 0;
//     buf_t addr_buf = buf_init(addr_item->data, addr_item->length);
//     addr_buf.write = addr_item->length;
//     if(!buf_read_u32_le(&addr_buf, "connection_id", &conn_id)) {
//         pdlog(LOG_MODULE_CPF_PROTOCOL, LOG_LEVEL_WARN, "CPF dispatch: failed to parse connection ID");
//         return UTIL_EINVAL;
//     }

//     /* Validate connection ID */
//     if(conn_id != plc->server_connection_id) {
//         pdlog(LOG_MODULE_CPF_PROTOCOL, LOG_LEVEL_WARN, "CPF dispatch: connection ID mismatch (got 0x%08X, expected 0x%08X)",
//               conn_id, plc->server_connection_id);
//         return UTIL_ENOTFOUND;
//     }

//     pdlog(LOG_MODULE_CPF_PROTOCOL, LOG_LEVEL_DETAIL, "CPF dispatch: connected message, connection_id=0x%08X", conn_id);

//     /* Data item should be at least 2 bytes (sequence number) + CIP message */
//     if(data_item->length < 2) {
//         pdlog(LOG_MODULE_CPF_PROTOCOL, LOG_LEVEL_WARN, "CPF dispatch: connected data item length %u < 2", data_item->length);
//         return UTIL_EINVAL;
//     }

//     /* Extract sequence number from first 2 bytes of data item */
//     uint16_t seq_num = 0;
//     buf_t data_buf = buf_init(data_item->data, data_item->length);
//     data_buf.write = data_item->length;
//     if(!buf_read_u16_le(&data_buf, "sequence_number", &seq_num)) {
//         pdlog(LOG_MODULE_CPF_PROTOCOL, LOG_LEVEL_WARN, "CPF dispatch: failed to parse sequence number");
//         return UTIL_EINVAL;
//     }

//     pdlog(LOG_MODULE_CPF_PROTOCOL, LOG_LEVEL_DETAIL, "CPF dispatch: sequence=%u, routing CIP message", seq_num);

//     /* Reserve space in output for CPF response header */
//     buf_t cpf_header = {0};
//     if(!buf_reserve_write(output, 14, &cpf_header)) {
//         pdlog(LOG_MODULE_CPF_PROTOCOL, LOG_LEVEL_ERROR, "CPF dispatch: failed to reserve space for CPF header: %s",
//               util_err_str(buf_get_error(output)));
//         return buf_get_error(output);
//     }

//     /* Dispatch to CIP message router - input is already positioned at CIP request data */
//     util_err_t err = cip_message_router_dispatch(input, output, plc);

//     if(err != UTIL_OK) {
//         pdlog(LOG_MODULE_CPF_PROTOCOL, LOG_LEVEL_DETAIL, "CPF dispatch: CIP router returned %s", util_err_str(err));
//         /* Continue - CIP response contains error code */
//     }

//     /* Fill in the reserved CPF header space */
//     bool ok = true;
//     ok &= buf_write_u16_le(&cpf_header, "item_count", 2);

//     /* Item 1: Connected Address Item (0x00A1) with connection ID echo */
//     ok &= buf_write_u16_le(&cpf_header, "address_type", CPF_ITEM_CONNECTED_ADDRESS);
//     ok &= buf_write_u16_le(&cpf_header, "address_length", 4);
//     ok &= buf_write_u32_le(&cpf_header, "connection_id_echo", plc->server_connection_id);

//     /* Item 2: Connected Data Item (0x00B1) */
//     size_t cip_response_size = buf_write_pos(output) - buf_write_pos(&cpf_header);
//     uint16_t response_length = (uint16_t)(2 + cip_response_size); /* +2 for sequence number */
//     ok &= buf_write_u16_le(&cpf_header, "data_type", CPF_ITEM_CONNECTED_DATA);
//     ok &= buf_write_u16_le(&cpf_header, "data_length", response_length);

//     /* Write sequence number echo */
//     ok &= buf_write_u16_le(&cpf_header, "sequence_echo", seq_num);

//     if(!ok) {
//         pdlog(LOG_MODULE_CPF_PROTOCOL, LOG_LEVEL_ERROR, "CPF dispatch: failed to build response: %s",
//               util_err_str(buf_get_error(&cpf_header)));
//         return buf_get_error(&cpf_header);
//     }

//     pdlog(LOG_MODULE_CPF_PROTOCOL, LOG_LEVEL_DETAIL, "CPF dispatch: connected response complete, %u bytes",
//           buf_write_pos(output));

//     return UTIL_OK;
// }

/* ============================================================================
 * CPF Dispatch Router
 * ============================================================================ */

util_err_t cpf_dispatch(buf_t *input, buf_t *output, client_context_t *client) {
    plc_context_t *plc = client->plc;
    bool is_connected = false;
    uint16_t sequence_number = 0;
    uint32_t connection_id = 0;
    size_t initial_read_size = buf_read_size(input);
    size_t cpf_header_size = 0;
    bool ok = true;

    pdlog(LOG_MODULE_CPF_PROTOCOL, LOG_LEVEL_DETAIL, "parsing CPF packet:");
    pdlog_bytes(LOG_MODULE_CPF_PROTOCOL, LOG_LEVEL_DETAIL, input);

    /*
     * Read in the CPF header data.
     */

    /* get the data ahead of the count word */
    uint32_t interface_handle;
    uint16_t router_timeout;
    uint16_t item_count;

    ok = true;
    ok &= buf_read_u32_le(input, "interface_handle", &interface_handle);
    ok &= buf_read_u16_le(input, "router_timeout", &router_timeout);
    ok &= buf_read_u16_le(input, "item_count", &item_count);

    pdlog(LOG_MODULE_CPF_PROTOCOL, LOG_LEVEL_DETAIL, "CPF header: interface_handle=0x%08X, router_timeout=%u, item_count=%u",
          interface_handle, router_timeout, item_count);

    if(!ok) {
        pdlog(LOG_MODULE_CPF_PROTOCOL, LOG_LEVEL_WARN, "failed to read CPF header");
        return buf_get_error(input);
    }

    /* sanity check */
    if(item_count != 2) {
        pdlog(LOG_MODULE_CPF_PROTOCOL, LOG_LEVEL_WARN, "Item count=2 supported, got %u", item_count);
        return UTIL_EINVAL;
    }

    /* Parse each item */
    for(uint16_t i = 0; i < item_count; i++) {
        uint16_t item_type = 0;
        uint16_t item_length = 0;

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

        switch(item_type) {
            case CPF_ITEM_NULL_ADDRESS:
                /* there is no other data than the type and length. */
                break;

            case CPF_ITEM_UNCONNECTED_DATA:
                /* no other data than the type and length. */
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
        pdlog(LOG_MODULE_CPF_PROTOCOL, LOG_LEVEL_WARN, "CPF dispatch: failed to reserve space for CPF header: %s",
              util_err_str(buf_get_error(output)));
        return buf_get_error(output);
    }

    size_t write_size = buf_write_size(output);

    util_err_t err = cip_message_router_dispatch(input, output, client);
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

    /* now write the items*/
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
