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

#include "cpf.h"
#include "cip.h"
#include "eip.h"
#include "../../../utils/log.h"
#include "../../../utils/buf.h"
#include <stdint.h>

#define CPF_ITEM_NAI ((uint16_t)0x0000) /* NULL Address Item */
#define CPF_ITEM_CAI ((uint16_t)0x00A1) /* connected address item */
#define CPF_ITEM_CDI ((uint16_t)0x00B1) /* connected data item */
#define CPF_ITEM_UDI ((uint16_t)0x00B2) /* Unconnected data item */


typedef struct {
    uint32_t interface_handle;
    uint16_t router_timeout;
    uint16_t item_count; /* should be 2 for now. */
    uint16_t item_addr_type;
    uint16_t item_addr_length;
    uint16_t item_data_type;
    uint16_t item_data_length;
} cpf_uc_header_s;

#define CPF_UCONN_HEADER_SIZE (16)

typedef struct {
    uint32_t interface_handle;
    uint16_t router_timeout;
    uint16_t item_count; /* should be 2 for now. */
    uint16_t item_addr_type;
    uint16_t item_addr_length;
    uint32_t conn_id;
    uint16_t item_data_type;
    uint16_t item_data_length;
    uint16_t conn_seq;
} cpf_co_header_s;

#define CPF_CONN_HEADER_SIZE (22)


util_err_t handle_cpf_unconnected(buf_t *input, buf_t *output, plc_s *plc) {
    cpf_uc_header_s header;

    pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "handle_cpf_unconnected(): got packet");

    /* we must have some sort of payload. */
    if(buf_read_size(input) <= CPF_UCONN_HEADER_SIZE) {
        pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Unusable size of unconnected CPF packet!");
        return UTIL_EINVAL;
    }

    /* unpack the request. */
    bool ok = true;
    ok &= buf_read_u32_le(input, "interface_handle", &header.interface_handle);
    ok &= buf_read_u16_le(input, "router_timeout", &header.router_timeout);
    ok &= buf_read_u16_le(input, "item_count", &header.item_count);
    ok &= buf_read_u16_le(input, "item_addr_type", &header.item_addr_type);
    ok &= buf_read_u16_le(input, "item_addr_length", &header.item_addr_length);
    ok &= buf_read_u16_le(input, "item_data_type", &header.item_data_type);
    ok &= buf_read_u16_le(input, "item_data_length", &header.item_data_length);

    if (!ok) {
        pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Failed to parse CPF header: %s", util_err_str(buf_get_error(input)));
        return buf_get_error(input);
    }

    /* sanity check the number of items. */
    if(header.item_count != (uint16_t)2) {
        pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Unsupported unconnected CPF packet, expected two items but found %u!", header.item_count);
        return UTIL_EINVAL;
    }

    /* sanity check the data. */
    if(header.item_addr_type != CPF_ITEM_NAI) {
        pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Expected null address item but found %x!", header.item_addr_type);
        return UTIL_EINVAL;
    }

    if(header.item_addr_length != 0) {
        pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Expected zero address item length but found %d bytes!", header.item_addr_length);
        return UTIL_EINVAL;
    }

    if(header.item_data_type != CPF_ITEM_UDI) {
        pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Expected unconnected data item but found %x!", header.item_data_type);
        return UTIL_EINVAL;
    }

    if(header.item_data_length != buf_read_size(input)) {
        pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "CPF unconnected payload length mismatch!");
        return UTIL_EINVAL;
    }

    /* Write CPF response header to output buffer, leaving room for CIP response */
    buf_reset(output);
    ok = true;
    ok &= buf_write_u32_le(output, "interface_handle", header.interface_handle);
    ok &= buf_write_u16_le(output, "router_timeout", header.router_timeout);
    ok &= buf_write_u16_le(output, "item_count", 2);  /* two items */
    ok &= buf_write_u16_le(output, "item_addr_type", CPF_ITEM_NAI);
    ok &= buf_write_u16_le(output, "item_addr_length", 0);
    ok &= buf_write_u16_le(output, "item_data_type", CPF_ITEM_UDI);
    ok &= buf_write_u16_le(output, "item_data_length_placeholder", 0);  /* Will update later */

    if (!ok) {
        pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Failed to write CPF response header: %s", util_err_str(buf_get_error(output)));
        return buf_get_error(output);
    }

    /* Save position of item_data_length field for later update */
    size_t data_length_offset = buf_write_pos(output) - 2;

    /* dispatch to CIP - it will write the response starting at current position */
    util_err_t cip_err = cip_dispatch_request(input, output, plc);

    if(cip_err != UTIL_OK) {
        pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "CIP dispatch failed: %s", util_err_str(cip_err));
        return cip_err;
    }

    /* Update the item_data_length field with actual response size */
    size_t response_len = buf_write_pos(output) - CPF_UCONN_HEADER_SIZE;
    uint8_t *output_data = buf_write_ptr(output) - buf_write_pos(output);
    output_data[data_length_offset] = (uint8_t)(response_len & 0xFF);
    output_data[data_length_offset + 1] = (uint8_t)((response_len >> 8) & 0xFF);

    return UTIL_OK;
}


util_err_t handle_cpf_connected(buf_t *input, buf_t *output, plc_s *plc) {
    cpf_co_header_s header;

    /* we must have some sort of payload. */
    if(buf_read_size(input) <= CPF_CONN_HEADER_SIZE) {
        pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Unusable size of connected CPF packet!");
        return UTIL_ERR_INVALID_DATA;
    }

    /* unpack the request. */
    bool ok = true;
    ok &= buf_read_u32_le(input, "interface_handle", &header.interface_handle);
    ok &= buf_read_u16_le(input, "router_timeout", &header.router_timeout);
    ok &= buf_read_u16_le(input, "item_count", &header.item_count);
    ok &= buf_read_u16_le(input, "item_addr_type", &header.item_addr_type);
    ok &= buf_read_u16_le(input, "item_addr_length", &header.item_addr_length);
    ok &= buf_read_u32_le(input, "conn_id", &header.conn_id);
    ok &= buf_read_u16_le(input, "item_data_type", &header.item_data_type);
    ok &= buf_read_u16_le(input, "item_data_length", &header.item_data_length);
    ok &= buf_read_u16_le(input, "conn_seq", &header.conn_seq);

    if (!ok) {
        pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Failed to parse CPF connected header: %s", util_err_str(buf_get_error(input)));
        return buf_get_error(input);
    }

    /* sanity check the number of items. */
    if(header.item_count != (uint16_t)2) {
        pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Unsupported connected CPF packet, expected two items but found %u!", header.item_count);
        return UTIL_ERR_INVALID_DATA;
    }

    /* sanity check the data. */
    if(header.item_addr_type != CPF_ITEM_CAI) {
        pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Expected connected address item but found %x!", header.item_addr_type);
        return UTIL_ERR_INVALID_DATA;
    }

    if(header.item_addr_length != 4) {
        pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Expected address item length of 4 but found %d bytes!", header.item_addr_length);
        return UTIL_ERR_INVALID_DATA;
    }

    if(header.conn_id != plc->server_connection_id) {
        pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Expected connection ID %x but found connection ID %x!", plc->server_connection_id, header.conn_id);
        return UTIL_ERR_INVALID_DATA;
    }

    if(header.item_data_type != CPF_ITEM_CDI) {
        pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Expected connected data item but found %x!", header.item_data_type);
        return UTIL_ERR_INVALID_DATA;
    }

    if(header.item_data_length != buf_read_size(input)) {
        pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "CPF connected payload length mismatch!");
        return UTIL_ERR_INVALID_DATA;
    }

    /* do we care about the sequence ID? Should check. */
    plc->client_connection_seq = header.conn_seq;

    /* Write CPF response header to output buffer, leaving room for CIP response */
    buf_reset(output);
    ok = true;
    ok &= buf_write_u32_le(output, "interface_handle", header.interface_handle);
    ok &= buf_write_u16_le(output, "router_timeout", header.router_timeout);
    ok &= buf_write_u16_le(output, "item_count", 2);  /* two items */
    ok &= buf_write_u16_le(output, "item_addr_type", CPF_ITEM_CAI);
    ok &= buf_write_u16_le(output, "item_addr_length", 4);  /* connection ID is 4 bytes */
    ok &= buf_write_u32_le(output, "client_connection_id", plc->client_connection_id);
    ok &= buf_write_u16_le(output, "item_data_type", CPF_ITEM_CDI);
    ok &= buf_write_u16_le(output, "item_data_length_placeholder", 0);  /* Will update later */
    ok &= buf_write_u16_le(output, "conn_seq", header.conn_seq);

    if (!ok) {
        pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Failed to write CPF connected response header: %s", util_err_str(buf_get_error(output)));
        return buf_get_error(output);
    }

    /* Save position of item_data_length field for later update */
    size_t data_length_offset = buf_write_pos(output) - 4;  /* 2 bytes for length + 2 bytes for seq */

    /* dispatch to CIP - it will write the response starting at current position */
    util_err_t cip_err = cip_dispatch_request(input, output, plc);

    if(cip_err != UTIL_OK) {
        pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "CIP dispatch failed: %s", util_err_str(cip_err));
        return cip_err;
    }

    /* Update the item_data_length field with actual response size (plus 2 bytes for sequence number) */
    size_t response_len = buf_write_pos(output) - CPF_CONN_HEADER_SIZE + 2;
    uint8_t *output_data = (uint8_t *)buf_read_ptr(output) - buf_read_size(output);
    output_data[data_length_offset] = (uint8_t)(response_len & 0xFF);
    output_data[data_length_offset + 1] = (uint8_t)((response_len >> 8) & 0xFF);

    return UTIL_OK;
}
