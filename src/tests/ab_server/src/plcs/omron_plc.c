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
#include "../../../utils/log.h"
#include <stdint.h>
#include <stdlib.h>

/**
 * Omron PLC-specific CIP protocol handlers
 *
 * This module contains the Omron-specific implementations of CIP protocol
 * handlers. Omron PLCs use their own extensions to the CIP protocol,
 * particularly for tag and structure enumeration.
 */

/* Forward declarations for existing handler functions in cip.c */
extern slice_s handle_read_request(uint8_t cip_service, slice_s cip_service_path,
                                   slice_s cip_service_payload, slice_s output,
                                   plc_s *plc);
extern slice_s handle_write_request(uint8_t cip_service, slice_s cip_service_path,
                                    slice_s cip_service_payload, slice_s output,
                                    plc_s *plc);
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
#define CIP_SRV_INSTANCES_ATTRIBS     ((uint8_t)0x55)
#define CIP_SRV_GET_INSTANCE_LIST     ((uint8_t)0x5f)
#define CIP_SRV_FORWARD_OPEN_EX       ((uint8_t)0x5b)

/**
 * omron_dispatch_cip_request - Main Omron CIP protocol dispatcher
 *
 * Routes CIP requests to appropriate handlers based on service code and path.
 * This is the main entry point for CIP protocol processing for Omron PLCs.
 *
 * The dispatcher handles:
 * - Service 0x54/0x5B (Forward Open/Open Extended) - connection establishment
 * - Service 0x4E (Forward Close) - connection teardown
 * - Service 0x4C/0x52 (Read Named Tag/Fragmented) - tag read operations
 * - Service 0x4D/0x53 (Write Named Tag/Fragmented) - tag write operations
 * - Service 0x01 (Get Attributes All) - tag/type metadata queries
 * - Service 0x5F (Get Instance List) - tag enumeration pagination
 * - Service 0x0A (Multi-Service) - multiple sub-requests
 * - Service 0x4B (PCCC Execute) - legacy PCCC protocol
 */
static slice_s omron_dispatch_cip_request(uint8_t cip_service,
                                          slice_s cip_service_path,
                                          slice_s cip_service_payload,
                                          slice_s output,
                                          plc_s *plc) {
    pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Omron dispatcher: service 0x%02x", cip_service);

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
            return handle_read_request(cip_service, cip_service_path, cip_service_payload, output, plc);

        case CIP_SRV_WRITE_NAMED_TAG:
        case CIP_SRV_WRITE_NAMED_TAG_FRAG:
            return handle_write_request(cip_service, cip_service_path, cip_service_payload, output, plc);

        case CIP_SRV_GET_ATTR_ALL:
            return handle_get_attribute_all(cip_service, cip_service_path, cip_service_payload, output, plc);

        case CIP_SRV_GET_INSTANCE_LIST:
            return handle_get_instance_list(cip_service, cip_service_path, cip_service_payload, output, plc);

        default:
            pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Omron dispatcher: unsupported service 0x%02x", cip_service);
            /* Omron PLCs don't support PCCC or other non-CIP services */
            return slice_make_err(-1);  /* TODO: proper error response */
    }
}

/**
 * Omron PLC dispatcher implementation
 *
 * This dispatcher provides the standard CIP protocol handlers for Omron PLCs.
 * The dispatcher pattern allows different PLC types to have their own
 * implementations of protocol handlers without cluttering a single file with
 * conditional logic.
 */
static plc_dispatcher_t omron_dispatcher = {
    .name = "Omron",
    .dispatch_cip_request = omron_dispatch_cip_request,
    .handle_read_tag = handle_read_request,
    .handle_write_tag = handle_write_request,
    .handle_get_attributes = handle_get_attribute_all,
    .handle_get_template_attributes = NULL,  /* Not implemented for Omron yet */
    .handle_read_template_data = NULL,         /* Not implemented for Omron yet */
};

/**
 * omron_get_dispatcher - Get the Omron PLC dispatcher
 *
 * Returns a pointer to the dispatcher structure for Omron PLCs.
 * This is called during initialization to set up the PLC type-specific
 * protocol handlers.
 */
plc_dispatcher_t* omron_get_dispatcher(void) {
    return &omron_dispatcher;
}
