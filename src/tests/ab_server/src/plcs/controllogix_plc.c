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

#include "../plc.h"
#include "../slice.h"
#include "../ethernet_ip/eip.h"
#include "../ethernet_ip/cip.h"
#include "../ethernet_ip/cip_logix.h"
#include "../../../utils/log.h"
#include <stdint.h>
#include <stdlib.h>

/**
 * ControlLogix PLC-specific CIP protocol handlers
 *
 * This module contains the ControlLogix-specific implementations of CIP protocol
 * handlers. ControlLogix PLCs support:
 * - Fragmented read/write operations with service codes 0x4C/0x52 and 0x4D/0x53
 * - Status 0x06 responses for partial data with continuation
 * - Complex data types and structure definitions
 * - Richer tag enumeration with attribute support
 */

/* Forward declarations for shared handler functions in cip.c */
extern slice_s handle_get_attribute_all(uint8_t cip_service, slice_s cip_service_path,
                                        slice_s cip_service_payload, slice_s output,
                                        plc_s *plc);
extern slice_s handle_get_instance_list(uint8_t cip_service, slice_s cip_service_path,
                                        slice_s cip_service_payload, slice_s output,
                                        plc_s *plc);
extern slice_s handle_multi_request(uint8_t cip_service, slice_s cip_service_path,
                                    slice_s cip_service_payload, slice_s output,
                                    plc_s *plc);
extern slice_s handle_forward_open(uint8_t cip_service, slice_s cip_service_path,
                                   slice_s cip_service_payload, slice_s output,
                                   plc_s *plc);
extern slice_s handle_forward_close(uint8_t cip_service, slice_s cip_service_path,
                                    slice_s cip_service_payload, slice_s output,
                                    plc_s *plc);

/* CIP service constants */
#define CIP_SRV_GET_ATTR_ALL          ((uint8_t)0x01)
#define CIP_SRV_MULTI                 ((uint8_t)0x0a)
#define CIP_SRV_READ_NAMED_TAG        ((uint8_t)0x4c)
#define CIP_SRV_WRITE_NAMED_TAG       ((uint8_t)0x4d)
#define CIP_SRV_FORWARD_CLOSE         ((uint8_t)0x4e)
#define CIP_SRV_READ_NAMED_TAG_FRAG   ((uint8_t)0x52)
#define CIP_SRV_WRITE_NAMED_TAG_FRAG  ((uint8_t)0x53)
#define CIP_SRV_FORWARD_OPEN          ((uint8_t)0x54)
#define CIP_SRV_LIST_TAGS             ((uint8_t)0x55)
#define CIP_SRV_GET_INSTANCE_LIST     ((uint8_t)0x5f)
#define CIP_SRV_FORWARD_OPEN_EX       ((uint8_t)0x5b)

/**
 * controllogix_dispatch_cip_request - Main ControlLogix CIP protocol dispatcher
 *
 * Routes CIP requests to appropriate handlers based on service code and path.
 * This is the main entry point for CIP protocol processing for ControlLogix PLCs.
 *
 * The dispatcher handles:
 * - Service 0x54/0x5B (Forward Open/Open Extended) - connection establishment
 * - Service 0x4E (Forward Close) - connection teardown
 * - Service 0x4C/0x52 (Read Named Tag/Fragmented) - tag read operations with fragmentation support
 * - Service 0x4D/0x53 (Write Named Tag/Fragmented) - tag write operations with fragmentation support
 * - Service 0x01 (Get Attributes All) - tag/type metadata queries
 * - Service 0x5F (Get Instance List) - tag enumeration pagination
 * - Service 0x0A (Multi-Service) - multiple sub-requests
 */
static slice_s controllogix_dispatch_cip_request(uint8_t cip_service,
                                                 slice_s cip_service_path,
                                                 slice_s cip_service_payload,
                                                 slice_s output,
                                                 plc_s *plc) {
    pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "ControlLogix dispatcher: service 0x%02x", cip_service);

    switch(cip_service) {
        case CIP_SRV_MULTI:
            return handle_multi_request(cip_service, cip_service_path, cip_service_payload, output, plc);

        case CIP_SRV_FORWARD_OPEN:
        case CIP_SRV_FORWARD_OPEN_EX:
            return handle_forward_open(cip_service, cip_service_path, cip_service_payload, output, plc);

        case CIP_SRV_FORWARD_CLOSE:
            return handle_forward_close(cip_service, cip_service_path, cip_service_payload, output, plc);

        case CIP_SRV_READ_NAMED_TAG:
        case CIP_SRV_READ_NAMED_TAG_FRAG:
            return logix_handle_read_request(cip_service, cip_service_path, cip_service_payload, output, plc);

        case CIP_SRV_WRITE_NAMED_TAG:
        case CIP_SRV_WRITE_NAMED_TAG_FRAG:
            return logix_handle_write_request(cip_service, cip_service_path, cip_service_payload, output, plc);

        case CIP_SRV_LIST_TAGS:
            return logix_handle_list_tags(cip_service, cip_service_path, cip_service_payload, output, plc);

        case CIP_SRV_GET_ATTR_ALL:
            return handle_get_attribute_all(cip_service, cip_service_path, cip_service_payload, output, plc);

        case CIP_SRV_GET_INSTANCE_LIST:
            return handle_get_instance_list(cip_service, cip_service_path, cip_service_payload, output, plc);

        default:
            pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "ControlLogix dispatcher: unsupported service 0x%02x", cip_service);
            return slice_make_err(-1);  /* TODO: proper error response */
    }
}

/**
 * ControlLogix PLC dispatcher implementation
 *
 * This dispatcher provides the CIP protocol handlers for ControlLogix PLCs.
 * ControlLogix supports more advanced features like fragmentation with status 0x06
 * and service code switching between 0x4C/0x52 for reads and 0x4D/0x53 for writes.
 */
static plc_dispatcher_t controllogix_dispatcher = {
    .name = "ControlLogix",
    .dispatch_cip_request = controllogix_dispatch_cip_request,
    .handle_read_tag = logix_handle_read_request,
    .handle_write_tag = logix_handle_write_request,
    .handle_get_attributes = handle_get_attribute_all,
    .handle_get_template_attributes = NULL,  /* Not implemented yet */
    .handle_read_template_data = NULL,         /* Not implemented yet */
};

/**
 * controllogix_get_dispatcher - Get the ControlLogix PLC dispatcher
 *
 * Returns a pointer to the dispatcher structure for ControlLogix PLCs.
 * This is called during initialization to set up the PLC type-specific
 * protocol handlers.
 */
plc_dispatcher_t* controllogix_get_dispatcher(void) {
    return &controllogix_dispatcher;
}
