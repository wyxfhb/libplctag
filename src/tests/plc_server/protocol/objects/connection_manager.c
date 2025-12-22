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
#include "../cip_message_router.h"
#include "../cip_path.h"
#include "../../plc_context.h"
#include "../../../utils/log.h"
#include "../../../utils/buf.h"

/* CIP Service codes */
#define CIP_SRV_FORWARD_CLOSE ((uint8_t)0x4E)
#define CIP_SRV_FORWARD_OPEN ((uint8_t)0x54)
#define CIP_SRV_FORWARD_OPEN_EX ((uint8_t)0x5B)

/* ============================================================================
 * Service: Forward Open (0x54) and Forward Open Extended (0x5B)
 * ============================================================================ */

/**
 * Service 0x54: Forward Open
 * Service 0x5B: Forward Open Extended
 *
 * Opens a connection to the PLC.
 *
 * Request format (0x54):
 *   [0]     uint8      Priority/timeout (bits 7-4: priority, 3-0: timeout ticks)
 *   [1]     uint8      Connection timeout multiplier
 *   [2-5]   uint32_le  Originator vendor ID
 *   [6-9]   uint32_le  Originator serial number
 *   [10]    uint8      Connection path size (in words)
 *   [11+]   uint8[]    Connection path
 *
 * Response format:
 *   [0]     uint8      Reserved
 *   [1]     uint8      Connection path size (echo)
 *   [2-5]   uint32_le  Server connection ID
 *   [6-9]   uint32_le  Client connection ID
 *   [10-11] uint16_le  Connection serial number
 *   [12-13] uint16_le  Originator vendor ID
 *   [14-17] uint32_le  Originator serial number
 *   [18-21] uint32_le  RPI to server
 *   [22-25] uint32_le  RPI to client
 *   [26-27] uint16_le  Connection parameters (reserved, must be 0)
 */
static util_err_t connection_manager_forward_open(uint8_t service, const cip_path_t *path, buf_t *request, buf_t *response,
                                                  cip_object_instance_t *instance, client_context_t *client) {
    plc_context_t *plc = client->plc;
    (void)path;
    (void)instance;

    pdlog(LOG_MODULE_CONNECTION_MANAGER, LOG_LEVEL_DETAIL, "Forward Open%s request",
          (service == CIP_SRV_FORWARD_OPEN_EX) ? " Extended" : "");

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
    ok &= buf_read_u8(request, "secs_per_tick", &secs_per_tick);
    ok &= buf_read_u8(request, "timeout_ticks", &timeout_ticks);
    ok &= buf_read_u32_le(request, "originator_vendor", &originator_vendor);
    ok &= buf_read_u32_le(request, "originator_serial", &originator_serial);
    ok &= buf_read_u16_le(request, "conn_serial", &conn_serial);
    ok &= buf_read_u16_le(request, "vendor_id", &vendor_id);
    ok &= buf_read_u32_le(request, "serial_num", &serial_num);
    ok &= buf_read_u8(request, "timeout_mult", &timeout_mult);
    ok &= buf_read_u8(request, "padding[0]", &padding_byte);
    ok &= buf_read_u8(request, "padding[1]", &padding_byte);
    ok &= buf_read_u8(request, "padding[2]", &padding_byte);
    ok &= buf_read_u32_le(request, "c2s_rpi", &c2s_rpi);

    if(service == CIP_SRV_FORWARD_OPEN) {
        ok &= buf_read_u16_le(request, "c2s_params", &c2s_params_16);
    } else {
        ok &= buf_read_u32_le(request, "c2s_params", &c2s_params_32);
    }

    ok &= buf_read_u32_le(request, "s2c_rpi", &s2c_rpi);

    if(service == CIP_SRV_FORWARD_OPEN) {
        ok &= buf_read_u16_le(request, "s2c_params", &s2c_params_16);
    } else {
        ok &= buf_read_u32_le(request, "s2c_params", &s2c_params_32);
    }

    ok &= buf_read_u8(request, "transport_class", &transport_class);
    ok &= buf_read_u8(request, "path_size", &path_size);

    if(!ok) {
        pdlog(LOG_MODULE_CONNECTION_MANAGER, LOG_LEVEL_WARN, "Forward Open: failed to parse request");
        cip_build_response(response, service, CIP_STATUS_INVALID_PARAM);
        return buf_get_error(request);
    }

    pdlog(LOG_MODULE_CONNECTION_MANAGER, LOG_LEVEL_DETAIL,
          "Forward Open: originator_vendor=0x%04X originator_serial=0x%08X conn_serial=0x%04X path_size=%u", originator_vendor,
          originator_serial, conn_serial, path_size);

    /* Read and validate connection path based on PLC type */
    uint8_t connection_path[32] = {0};
    size_t connection_path_len = path_size * 2; /* Path size is in words (2-byte units) */

    if(connection_path_len > sizeof(connection_path)) {
        pdlog(LOG_MODULE_CONNECTION_MANAGER, LOG_LEVEL_WARN, "Forward Open: connection path too long (%zu bytes, max %zu)",
              connection_path_len, sizeof(connection_path));
        cip_build_response(response, service, CIP_STATUS_TOO_MUCH_DATA);
        return UTIL_EBOUNDS;
    }

    if(connection_path_len > 0) {
        if(!buf_read_bytes(request, "connection_path", connection_path, connection_path_len)) {
            pdlog(LOG_MODULE_CONNECTION_MANAGER, LOG_LEVEL_WARN, "Forward Open: failed to read connection path");
            cip_build_response(response, service, CIP_STATUS_PATH_DEST_UNKNOWN);
            return buf_get_error(request);
        }
    }

    /* Validate path based on PLC type */
    if(plc->plc_type == PLC_TYPE_CONTROLLOGIX) {
        /* ControlLogix REQUIRES a path that matches the server's configured path */
        if(connection_path_len == 0) {
            pdlog(LOG_MODULE_CONNECTION_MANAGER, LOG_LEVEL_WARN, "Forward Open: ControlLogix requires a path, but none provided");
            cip_build_response(response, service, CIP_STATUS_PATH_DEST_UNKNOWN);
            return UTIL_ENOTFOUND;
        }

        /* The path may include routing info beyond the backplane/slot.
         * We only validate the first bytes match our configured path.
         * Format: [backplane, slot, optional_routing...] */
        if(connection_path_len < plc->path_len || memcmp(connection_path, plc->path, plc->path_len) != 0) {
            pdlog(LOG_MODULE_CONNECTION_MANAGER, LOG_LEVEL_WARN,
                  "Forward Open: path mismatch - got bytes 0x%02X 0x%02X (expected 0x%02X 0x%02X)",
                  (connection_path_len > 0) ? connection_path[0] : 0xFF, (connection_path_len > 1) ? connection_path[1] : 0xFF,
                  plc->path[0], plc->path[1]);
            cip_build_response(response, service, CIP_STATUS_PATH_DEST_UNKNOWN);
            return UTIL_ENOTFOUND;
        }
        pdlog(LOG_MODULE_CONNECTION_MANAGER, LOG_LEVEL_DETAIL,
              "Forward Open: ControlLogix path validation passed (0x%02X 0x%02X matches)", plc->path[0], plc->path[1]);
    } else {
        /* Micro800, PLC/5, SLC, etc. MUST NOT have a path */
        if(connection_path_len > 0) {
            pdlog(LOG_MODULE_CONNECTION_MANAGER, LOG_LEVEL_WARN, "Forward Open: %s does not accept paths, but got %zu bytes",
                  (plc->plc_type == PLC_TYPE_MICRO800) ? "Micro800" : "PLC", connection_path_len);
            cip_build_response(response, service, CIP_STATUS_INVALID_PARAM);
            return UTIL_ENOTFOUND;
        }
        pdlog(LOG_MODULE_CONNECTION_MANAGER, LOG_LEVEL_DETAIL, "Forward Open: path validation passed (no path required)");
    }

    /* CHECK IF WE SHOULD FAIL THIS REQUEST (testing feature) */
    if(client->reject_fo_count > 0) {
        client->reject_fo_count--; /* Decrement counter */

        pdlog(LOG_MODULE_CONNECTION_MANAGER, LOG_LEVEL_INFO, "Rejecting ForwardOpen for testing (%u more rejections remaining)",
              client->reject_fo_count);

        /* Build error response: Connection ID already in use */
        /* Format: [service|0x80][reserved][general_status][ext_status_size][ext_status_code(2 bytes)] */
        bool ok = true;
        ok &= buf_write_u8(response, "reply_service", service | 0x80); /* Error response */
        ok &= buf_write_u8(response, "reserved", 0);
        ok &= buf_write_u8(response, "general_status", 0x01);        /* Extended error */
        ok &= buf_write_u8(response, "additional_status_size", 2);   /* 2 bytes of extended status follow */
        ok &= buf_write_u16_le(response, "extended_status", 0x0100); /* Duplicate connection (already in use) */

        if(!ok) {
            pdlog(LOG_MODULE_CONNECTION_MANAGER, LOG_LEVEL_WARN, "Forward Open: failed to build error response");
            return buf_get_error(response);
        }

        return UTIL_OK;
    }

    /* Generate server connection ID (non-zero) */
    uint32_t server_conn_id = (uint32_t)time(NULL);
    if(server_conn_id == 0) { server_conn_id = 1; }

    /* Store connection state */
    client->client_connection_id = originator_vendor; /* Originator vendor ID serves as connection ID */
    client->client_connection_serial = conn_serial;
    client->client_vendor_id = vendor_id;
    client->client_serial_number = serial_num;
    client->client_to_server_rpi = c2s_rpi;
    client->server_to_client_rpi = s2c_rpi;
    client->client_to_server_max_packet = (service == CIP_SRV_FORWARD_OPEN) ? c2s_params_16 : (c2s_params_32 & 0x1FF);
    client->server_to_client_max_packet = (service == CIP_SRV_FORWARD_OPEN) ? s2c_params_16 : (s2c_params_32 & 0xFFF);
    client->server_connection_id = server_conn_id;
    client->is_forward_open = true;

    /* set the request and response sizes */

    /* Build response header */
    cip_build_response(response, service, CIP_STATUS_OK);

    /* Response data - per EtherNet/IP spec (Forward Open reply)
     * [0-3]   uint32_le  Server connection ID (O->T, what server sends to client in connected messages)
     * [4-7]   uint32_le  Client connection ID (T->O echo, what client sent in request)
     * [8-9]   uint16_le  Connection serial number (from request)
     * [10-11] uint16_le  Originator vendor ID (from request)
     * [12-15] uint32_le  Originator serial number (from request)
     * [16-19] uint32_le  RPI O->T (client-to-server)
     * [20-23] uint32_le  RPI T->O (server-to-client)
     * [24]    uint8      App data size (0 if no app data)
     * [25]    uint8      Reserved
     */
    ok = true;
    ok &= buf_write_u32_le(response, "server_to_orig_conn_id", server_conn_id);
    ok &= buf_write_u32_le(response, "orig_to_server_conn_id", originator_vendor);
    ok &= buf_write_u16_le(response, "conn_serial", conn_serial);
    ok &= buf_write_u16_le(response, "vendor_id", vendor_id);
    ok &= buf_write_u32_le(response, "serial_number", serial_num);
    ok &= buf_write_u32_le(response, "c2s_rpi", c2s_rpi);
    ok &= buf_write_u32_le(response, "s2c_rpi", s2c_rpi);
    ok &= buf_write_u8(response, "app_data_size", 0);
    ok &= buf_write_u8(response, "reserved2", 0);

    if(!ok) {
        pdlog(LOG_MODULE_CONNECTION_MANAGER, LOG_LEVEL_ERROR, "Forward Open: failed to write response");
        return buf_get_error(response);
    }

    pdlog(LOG_MODULE_CONNECTION_MANAGER, LOG_LEVEL_DETAIL, "Forward Open: connection accepted, server_id=0x%08X", server_conn_id);

    return UTIL_OK;
}

/* ============================================================================
 * Service: Forward Close (0x4E)
 * ============================================================================ */

/**
 * Service 0x4E: Forward Close
 *
 * Closes a connection to the PLC.
 *
 * Request format:
 *   [0]     uint8      Priority/timeout
 *   [1]     uint8      Connection timeout multiplier
 *   [2-3]   uint16_le  Connection serial number
 *   [4-5]   uint16_le  Originator vendor ID
 *   [6-9]   uint32_le  Originator serial number
 *   [10]    uint8      Connection path size (in words)
 *   [11+]   uint8[]    Connection path
 *
 * Response:
 *   [0]     uint8      Reserved
 *   [1]     uint8      Echo connection path size
 */
static util_err_t connection_manager_forward_close(uint8_t service, const cip_path_t *path, buf_t *request, buf_t *response,
                                                   cip_object_instance_t *instance, client_context_t *client) {
    (void)path;
    (void)instance;

    pdlog(LOG_MODULE_CONNECTION_MANAGER, LOG_LEVEL_DETAIL, "Forward Close request");

    uint8_t priority_timeout = 0;
    uint8_t timeout_multiplier = 0;
    uint16_t conn_serial = 0;
    uint16_t vendor_id = 0;
    uint32_t serial = 0;
    uint8_t path_size = 0;

    bool ok = true;
    ok &= buf_read_u8(request, "priority_timeout", &priority_timeout);
    ok &= buf_read_u8(request, "timeout_multiplier", &timeout_multiplier);
    ok &= buf_read_u16_le(request, "conn_serial", &conn_serial);
    ok &= buf_read_u16_le(request, "vendor_id", &vendor_id);
    ok &= buf_read_u32_le(request, "serial", &serial);
    ok &= buf_read_u8(request, "path_size", &path_size);

    if(!ok) {
        pdlog(LOG_MODULE_CONNECTION_MANAGER, LOG_LEVEL_WARN, "Forward Close: failed to parse request");
        cip_build_response(response, service, CIP_STATUS_INVALID_PARAM);
        return buf_get_error(request);
    }

    pdlog(LOG_MODULE_CONNECTION_MANAGER, LOG_LEVEL_DETAIL, "Forward Close: serial=0x%04X vendor=0x%04X path_size=%u", conn_serial,
          vendor_id, path_size);

    /* Clear connection state */
    client->is_forward_open = false;
    client->server_connection_id = 0;
    client->client_connection_id = 0;

    /* Build response */
    cip_build_response(response, service, CIP_STATUS_OK);
    ok = true;
    ok &= buf_write_u8(response, "reserved", 0);
    ok &= buf_write_u8(response, "echo_path_size", path_size);

    if(!ok) {
        pdlog(LOG_MODULE_CONNECTION_MANAGER, LOG_LEVEL_ERROR, "Forward Close: failed to write response");
        return buf_get_error(response);
    }

    pdlog(LOG_MODULE_CONNECTION_MANAGER, LOG_LEVEL_DETAIL, "Forward Close: connection closed");

    return UTIL_OK;
}

/* ============================================================================
 * Instance Management
 * ============================================================================ */

static cip_object_instance_t *connection_manager_get_instance(uint32_t instance_id, client_context_t *client) {
    (void)client;

    /* Connection Manager typically has only instance 1 */
    if(instance_id != 1) { return NULL; }

    static cip_object_instance_t instance = {
        .object_class = NULL, /* Will be set by registry */
        .instance_id = 1,
        .instance_data = NULL,
    };
    return &instance;
}

/* ============================================================================
 * Registration
 * ============================================================================ */

void connection_manager_object_register(cip_object_registry_t *registry) {

    pdlog(LOG_MODULE_CONNECTION_MANAGER, LOG_LEVEL_INFO, "Registering Connection Manager Object (Class 0x06)");

    cip_object_class_t *cls = (cip_object_class_t *)calloc(1, sizeof(*cls));
    if(!cls) { return; }

    cls->class_id = 0x06;
    cls->class_name = "Connection Manager";

    /* Register service handlers */
    cls->service_handlers[0x54] = connection_manager_forward_open;
    cls->service_handlers[0x5B] = connection_manager_forward_open;
    cls->service_handlers[0x4E] = connection_manager_forward_close;

    /* Instance management */
    cls->get_instance = connection_manager_get_instance;

    /* Register in registry */
    cip_registry_register_class(registry, cls);

    pdlog(LOG_MODULE_CONNECTION_MANAGER, LOG_LEVEL_INFO, "Registered Connection Manager Object (Class 0x06)");
}
