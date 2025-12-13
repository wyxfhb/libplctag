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
#include "log.h"

/* ============================================================================
 * CPF Parsing
 * ============================================================================ */

util_err_t cpf_parse_packet(buf_t *input, cpf_packet_t *packet) {
    memset(packet, 0, sizeof(*packet));

    /* Read item count */
    if (!buf_read_u16_le(input, "item_count", &packet->item_count)) {
        pdlog(LOG_MODULE_CPF_PROTOCOL, LOG_LEVEL_WARN,
              "CPF: failed to read item count");
        return buf_get_error(input);
    }

    if (packet->item_count > 8) {
        pdlog(LOG_MODULE_CPF_PROTOCOL, LOG_LEVEL_WARN,
              "CPF: item count %u exceeds max 8",
              packet->item_count);
        return UTIL_EINVAL;
    }

    pdlog(LOG_MODULE_CPF_PROTOCOL, LOG_LEVEL_DETAIL,
          "CPF: parsing %u items", packet->item_count);

    /* Parse each item */
    for (uint16_t i = 0; i < packet->item_count; i++) {
        cpf_item_t *item = &packet->items[i];

        /* Read item type and length */
        if (!buf_read_u16_le(input, "item_type", &item->type_id)) {
            pdlog(LOG_MODULE_CPF_PROTOCOL, LOG_LEVEL_WARN,
                  "CPF: failed to read item %u type", i);
            return buf_get_error(input);
        }

        if (!buf_read_u16_le(input, "item_length", &item->length)) {
            pdlog(LOG_MODULE_CPF_PROTOCOL, LOG_LEVEL_WARN,
                  "CPF: failed to read item %u length", i);
            return buf_get_error(input);
        }

        /* Store pointer to item data */
        item->data = buf_read_ptr(input);

        /* Verify we have the data */
        if (buf_read_size(input) < item->length) {
            pdlog(LOG_MODULE_CPF_PROTOCOL, LOG_LEVEL_WARN,
                  "CPF: item %u length %u exceeds buffer",
                  i, item->length);
            return UTIL_EBOUNDS;
        }

        /* Advance past item data */
        buf_read_advance(input, item->length);

        pdlog(LOG_MODULE_CPF_PROTOCOL, LOG_LEVEL_DETAIL,
              "CPF: item %u type=0x%04X length=%u",
              i, item->type_id, item->length);
    }

    return UTIL_OK;
}

/* ============================================================================
 * CPF Dispatch and Response Building
 * ============================================================================ */

util_err_t cpf_dispatch(const cpf_packet_t *packet, buf_t *input,
                       buf_t *output, plc_context_t *plc) {
    /* Find unconnected data item (0x00B2) */
    const cpf_item_t *data_item = NULL;
    for (uint16_t i = 0; i < packet->item_count; i++) {
        if (packet->items[i].type_id == CPF_ITEM_UNCONNECTED_DATA) {
            data_item = &packet->items[i];
            break;
        }
    }

    if (!data_item) {
        pdlog(LOG_MODULE_CPF_PROTOCOL, LOG_LEVEL_WARN,
              "CPF dispatch: no unconnected data item found");
        return UTIL_ENOTFOUND;
    }

    pdlog(LOG_MODULE_CPF_PROTOCOL, LOG_LEVEL_DETAIL,
          "CPF dispatch: routing CIP message (%u bytes)",
          data_item->length);

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

    if (err != UTIL_OK) {
        pdlog(LOG_MODULE_CPF_PROTOCOL, LOG_LEVEL_DETAIL,
              "CPF dispatch: CIP router returned %s",
              util_err_str(err));
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
    ok &= buf_write_u16_le(output, "data_length", buf_write_pos(&cip_response));

    /* Write CIP response data */
    ok &= buf_write_bytes(output, "cip_data",
                          cip_response_data, buf_write_pos(&cip_response));

    if (!ok) {
        pdlog(LOG_MODULE_CPF_PROTOCOL, LOG_LEVEL_ERROR,
              "CPF dispatch: failed to build response: %s",
              buf_error_string(buf_get_error(output)));
        return buf_get_error(output);
    }

    return UTIL_OK;
}
