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

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "connection_manager.h"
#include "ab_context.h"
#include "ab_plc_logix.h"
#include "../../protocols/cip/cip_defs.h"
#include "../../protocols/cip/cip_path.h"
#include "../../protocols/cip/cip_protocol.h"
#include "../../../utils/log.h"
#include "../../../utils/buf.h"

/* Note: CIP service codes defined in cip_defs.h */

/* ============================================================================
 * Service: Forward Open (0x54) and Forward Open Extended (0x5B)
 * ============================================================================ */

static util_err_t connection_manager_forward_open(
    uint8_t service_code,
    cip_path_t *path,
    buf_t *input,
    buf_t *output,
    ab_plc_context_t *plc,
    client_context_t *client) {

    (void)path;

    pdlog(LOG_MODULE_CONNECTION_MANAGER, LOG_LEVEL_DETAIL, "Forward Open%s request",
          (service_code == CIP_SRV_LARGE_FORWARD_OPEN) ? " Extended" : "");

    /* Parse Forward Open request - fields in order per EtherNet/IP spec */
    uint8_t secs_per_tick = 0;
    uint8_t timeout_ticks = 0;
    uint32_t originator_vendor = 0;
    uint32_t originator_serial = 0;
    uint16_t conn_serial = 0;
    uint16_t vendor_id = 0;
    uint32_t serial_num = 0;
    uint8_t timeout_mult = 0;
    uint32_t c2s_rpi = 0;
    uint16_t c2s_params_16 = 0;
    uint32_t c2s_params_32 = 0;
    uint32_t s2c_rpi = 0;
    uint16_t s2c_params_16 = 0;
    uint32_t s2c_params_32 = 0;
    uint8_t transport_class = 0;
    uint8_t path_size = 0;
    uint8_t padding_byte = 0;

    bool ok = true;
    ok &= buf_read_u8(input, "secs_per_tick", &secs_per_tick);
    ok &= buf_read_u8(input, "timeout_ticks", &timeout_ticks);
    ok &= buf_read_u32_le(input, "originator_vendor", &originator_vendor);
    ok &= buf_read_u32_le(input, "originator_serial", &originator_serial);
    ok &= buf_read_u16_le(input, "conn_serial", &conn_serial);
    ok &= buf_read_u16_le(input, "vendor_id", &vendor_id);
    ok &= buf_read_u32_le(input, "serial_num", &serial_num);
    ok &= buf_read_u8(input, "timeout_mult", &timeout_mult);
    ok &= buf_read_u8(input, "padding[0]", &padding_byte);
    ok &= buf_read_u8(input, "padding[1]", &padding_byte);
    ok &= buf_read_u8(input, "padding[2]", &padding_byte);
    ok &= buf_read_u32_le(input, "c2s_rpi", &c2s_rpi);

    if(service_code == CIP_SRV_FORWARD_OPEN) {
        ok &= buf_read_u16_le(input, "c2s_params", &c2s_params_16);
    } else {
        ok &= buf_read_u32_le(input, "c2s_params", &c2s_params_32);
    }

    ok &= buf_read_u32_le(input, "s2c_rpi", &s2c_rpi);

    if(service_code == CIP_SRV_FORWARD_OPEN) {
        ok &= buf_read_u16_le(input, "s2c_params", &s2c_params_16);
    } else {
        ok &= buf_read_u32_le(input, "s2c_params", &s2c_params_32);
    }

    ok &= buf_read_u8(input, "transport_class", &transport_class);
    ok &= buf_read_u8(input, "path_size", &path_size);

    if(!ok) {
        pdlog(LOG_MODULE_CONNECTION_MANAGER, LOG_LEVEL_WARN, "Forward Open: failed to parse request");
        cip_build_response(output, service_code, CIP_STATUS_INVALID_PARAM);
        return buf_get_error(input);
    }

    pdlog(LOG_MODULE_CONNECTION_MANAGER, LOG_LEVEL_DETAIL,
          "Forward Open: originator_vendor=0x%04X originator_serial=0x%08X conn_serial=0x%04X path_size=%u", originator_vendor,
          originator_serial, conn_serial, path_size);

    /* Read and validate connection path */
    uint8_t connection_path[32] = {0};
    size_t connection_path_len = path_size * 2; /* Path size is in words (2-byte units) */

    if(connection_path_len > sizeof(connection_path)) {
        pdlog(LOG_MODULE_CONNECTION_MANAGER, LOG_LEVEL_WARN, "Forward Open: connection path too long (%zu bytes, max %zu)",
              connection_path_len, sizeof(connection_path));
        cip_build_response(output, service_code, CIP_STATUS_REPLY_DATA_TOO_LARGE);
        return UTIL_EBOUNDS;
    }

    if(connection_path_len > 0) {
        if(!buf_read_bytes(input, "connection_path", connection_path, connection_path_len)) {
            pdlog(LOG_MODULE_CONNECTION_MANAGER, LOG_LEVEL_WARN, "Forward Open: failed to read connection path");
            cip_build_response(output, service_code, CIP_STATUS_PATH_DEST_UNKNOWN);
            return buf_get_error(input);
        }
    }

    /* Validate path (ControlLogix requires a path to match) */
    if(plc->plc_type == PLC_TYPE_CONTROLLOGIX) {
        if(connection_path_len == 0) {
            pdlog(LOG_MODULE_CONNECTION_MANAGER, LOG_LEVEL_WARN, "Forward Open: ControlLogix requires a path, but none provided");
            cip_build_response(output, service_code, CIP_STATUS_PATH_DEST_UNKNOWN);
            return UTIL_ENOTFOUND;
        }

        if(connection_path_len < plc->path_len || memcmp(connection_path, plc->path, plc->path_len) != 0) {
            pdlog(LOG_MODULE_CONNECTION_MANAGER, LOG_LEVEL_WARN,
                  "Forward Open: path mismatch - got bytes 0x%02X 0x%02X (expected 0x%02X 0x%02X)",
                  (connection_path_len > 0) ? connection_path[0] : 0xFF, (connection_path_len > 1) ? connection_path[1] : 0xFF,
                  plc->path[0], plc->path[1]);
            cip_build_response(output, service_code, CIP_STATUS_PATH_DEST_UNKNOWN);
            return UTIL_ENOTFOUND;
        }
        pdlog(LOG_MODULE_CONNECTION_MANAGER, LOG_LEVEL_DETAIL,
              "Forward Open: ControlLogix path validation passed (0x%02X 0x%02X matches)", plc->path[0], plc->path[1]);
    }

    /* Generate server connection ID (non-zero) */
    uint32_t server_conn_id = (uint32_t)time(NULL);
    if(server_conn_id == 0) { server_conn_id = 1; }

    /* Store connection state */
    client->client_connection_id = originator_vendor;
    client->connection_serial_number = conn_serial;
    client->originator_vendor_id = vendor_id;
    client->originator_serial_number = serial_num;
    client->server_connection_id = server_conn_id;
    client->is_forward_open = true;
    client->client_to_server_max_packet = (service_code == CIP_SRV_FORWARD_OPEN) ? c2s_params_16 : (c2s_params_32 & 0x1FF);
    client->server_to_client_max_packet = (service_code == CIP_SRV_FORWARD_OPEN) ? s2c_params_16 : (s2c_params_32 & 0xFFF);

    /* Build response header */
    cip_build_response(output, service_code, CIP_STATUS_OK);

    /* Write response data */
    ok = true;
    ok &= buf_write_u32_le(output, "server_to_orig_conn_id", server_conn_id);
    ok &= buf_write_u32_le(output, "orig_to_server_conn_id", originator_vendor);
    ok &= buf_write_u16_le(output, "conn_serial", conn_serial);
    ok &= buf_write_u16_le(output, "vendor_id", vendor_id);
    ok &= buf_write_u32_le(output, "serial_number", serial_num);
    ok &= buf_write_u32_le(output, "c2s_rpi", c2s_rpi);
    ok &= buf_write_u32_le(output, "s2c_rpi", s2c_rpi);
    ok &= buf_write_u8(output, "app_data_size", 0);
    ok &= buf_write_u8(output, "reserved2", 0);

    if(!ok) {
        pdlog(LOG_MODULE_CONNECTION_MANAGER, LOG_LEVEL_ERROR, "Forward Open: failed to write response");
        return buf_get_error(output);
    }

    pdlog(LOG_MODULE_CONNECTION_MANAGER, LOG_LEVEL_DETAIL, "Forward Open: connection accepted, server_id=0x%08X", server_conn_id);

    return UTIL_OK;
}

/* ============================================================================
 * Service: Forward Close (0x4E)
 * ============================================================================ */

static util_err_t connection_manager_forward_close(
    uint8_t service_code,
    cip_path_t *path,
    buf_t *input,
    buf_t *output,
    ab_plc_context_t *plc,
    client_context_t *client) {

    (void)path;
    (void)plc;

    pdlog(LOG_MODULE_CONNECTION_MANAGER, LOG_LEVEL_DETAIL, "Forward Close request");

    uint8_t priority_timeout = 0;
    uint8_t timeout_multiplier = 0;
    uint16_t conn_serial = 0;
    uint16_t vendor_id = 0;
    uint32_t serial = 0;
    uint8_t path_size = 0;

    bool ok = true;
    ok &= buf_read_u8(input, "priority_timeout", &priority_timeout);
    ok &= buf_read_u8(input, "timeout_multiplier", &timeout_multiplier);
    ok &= buf_read_u16_le(input, "conn_serial", &conn_serial);
    ok &= buf_read_u16_le(input, "vendor_id", &vendor_id);
    ok &= buf_read_u32_le(input, "serial", &serial);
    ok &= buf_read_u8(input, "path_size", &path_size);

    if(!ok) {
        pdlog(LOG_MODULE_CONNECTION_MANAGER, LOG_LEVEL_WARN, "Forward Close: failed to parse request");
        cip_build_response(output, service_code, CIP_STATUS_INVALID_PARAM);
        return buf_get_error(input);
    }

    pdlog(LOG_MODULE_CONNECTION_MANAGER, LOG_LEVEL_DETAIL, "Forward Close: serial=0x%04X vendor=0x%04X path_size=%u", conn_serial,
          vendor_id, path_size);

    /* Clear connection state */
    client->is_forward_open = false;
    client->server_connection_id = 0;
    client->client_connection_id = 0;

    /* Build response */
    cip_build_response(output, service_code, CIP_STATUS_OK);
    ok = true;
    ok &= buf_write_u8(output, "reserved", 0);
    ok &= buf_write_u8(output, "echo_path_size", path_size);

    if(!ok) {
        pdlog(LOG_MODULE_CONNECTION_MANAGER, LOG_LEVEL_ERROR, "Forward Close: failed to write response");
        return buf_get_error(output);
    }

    pdlog(LOG_MODULE_CONNECTION_MANAGER, LOG_LEVEL_DETAIL, "Forward Close: connection closed");

    return UTIL_OK;
}

/* ============================================================================
 * Registration
 * ============================================================================ */

util_err_t connection_manager_register(cip_class_registry_t *registry) {
    if(!registry) { return UTIL_EINVAL; }

    pdlog(LOG_MODULE_CONNECTION_MANAGER, LOG_LEVEL_INFO, "Registering Connection Manager Object (Class 0x06)");

    util_err_t err = UTIL_OK;

    /* Register ForwardOpen (0x54) */
    err = cip_class_registry_add_service(registry, 0x06, CIP_SRV_FORWARD_OPEN, connection_manager_forward_open);
    if(err != UTIL_OK) { return err; }

    /* Register ForwardOpenExtended (0x5B) */
    err = cip_class_registry_add_service(registry, 0x06, CIP_SRV_LARGE_FORWARD_OPEN, connection_manager_forward_open);
    if(err != UTIL_OK) { return err; }

    /* Register ForwardClose (0x4E) */
    err = cip_class_registry_add_service(registry, 0x06, CIP_SRV_FORWARD_CLOSE, connection_manager_forward_close);
    if(err != UTIL_OK) { return err; }

    pdlog(LOG_MODULE_CONNECTION_MANAGER, LOG_LEVEL_INFO, "Registered Connection Manager Object (Class 0x06)");

    return UTIL_OK;
}
