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
#include "identity_object.h"
#include "../cip_message_router.h"
#include "../../plc_context.h"
#include "../../../utils/log.h"
#include "../../../utils/buf.h"

/* Identity Object Device Information */
#define VENDOR_ID 0x0002    /* Rockwell Automation */
#define PRODUCT_TYPE 0x0002 /* Programmable Logic Controller */
#define PRODUCT_CODE 0x2F58 /* Micro800 */
#define REVISION_MAJOR 0x01
#define REVISION_MINOR 0x00
#define SERIAL_NUMBER 0x00001234 /* Dummy serial */
#define PRODUCT_NAME "libplctag PLC Simulator - Micro800"

/* ============================================================================
 * Service: Get Attributes All (0x01)
 * ============================================================================ */

/**
 * Service 0x01: Get Attributes All
 *
 * Returns all attributes for the Identity Object instance.
 *
 * Request:
 *   (empty)
 *
 * Response:
 *   [0-1]   uint16_le  Vendor ID
 *   [2-3]   uint16_le  Device Type
 *   [4-5]   uint16_le  Product Code
 *   [6]     uint8      Revision Major
 *   [7]     uint8      Revision Minor
 *   [8-11]  uint32_le  Serial Number
 *   [12-13] uint16_le  Product Name Length
 *   [14+]   uint8[]    Product Name (padded to word boundary)
 */
static util_err_t identity_service_get_attributes_all(uint8_t service, const cip_path_t *path, buf_t *request, buf_t *response,
                                                      cip_object_instance_t *instance, client_context_t *client) {

    (void)path;
    (void)request;
    (void)client;
    (void)instance;

    pdlog(LOG_MODULE_IDENTITY_OBJECT, LOG_LEVEL_DETAIL, "Get Attributes All: building device information");

    /* Build response header */
    cip_build_response(response, service, CIP_STATUS_OK);

    bool ok = true;

    /* Attribute 1: Vendor ID */
    ok &= buf_write_u16_le(response, "vendor_id", VENDOR_ID);

    /* Attribute 2: Device Type */
    ok &= buf_write_u16_le(response, "device_type", PRODUCT_TYPE);

    /* Attribute 3: Product Code */
    ok &= buf_write_u16_le(response, "product_code", PRODUCT_CODE);

    /* Attribute 4: Revision */
    ok &= buf_write_u8(response, "revision_major", REVISION_MAJOR);
    ok &= buf_write_u8(response, "revision_minor", REVISION_MINOR);

    /* Attribute 5: Serial Number */
    ok &= buf_write_u32_le(response, "serial_number", SERIAL_NUMBER);

    /* Attribute 7: Product Name Length */
    size_t name_len = strlen(PRODUCT_NAME);
    size_t name_words = (name_len + 1) / 2;
    ok &= buf_write_u16_le(response, "product_name_len", (uint16_t)name_words);

    /* Attribute 8: Product Name (with padding) */
    /* FIXME - this is completely broken, do we count the product name length in bytes or words? */
    /* This should be fixed by the PLC type in the plc context. */
    uint8_t padded_name[256];
    memset(padded_name, 0, sizeof(padded_name));
    memcpy(padded_name, PRODUCT_NAME, name_len);
    size_t padded_len = name_words * 2;
    ok &= buf_write_bytes(response, "product_name", padded_name, padded_len);

    if(!ok) {
        pdlog(LOG_MODULE_IDENTITY_OBJECT, LOG_LEVEL_ERROR, "Get Attributes All: failed to write response");
        return buf_get_error(response);
    }

    pdlog(LOG_MODULE_IDENTITY_OBJECT, LOG_LEVEL_DETAIL, "Get Attributes All: vendor=0x%04X device=0x%04X serial=0x%08X",
          VENDOR_ID, PRODUCT_TYPE, SERIAL_NUMBER);

    return UTIL_OK;
}

/* ============================================================================
 * Instance Management
 * ============================================================================ */

static cip_object_instance_t *identity_get_instance(uint32_t instance_id, client_context_t *client) {
    (void)client;

    /* Identity object typically has only instance 1 */
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

void identity_object_register(cip_object_registry_t *registry) {
    cip_object_class_t *cls = (cip_object_class_t *)calloc(1, sizeof(*cls));
    if(!cls) { return; }

    cls->class_id = 0x01;
    cls->class_name = "Identity Object";

    /* Register service handlers */
    cls->service_handlers[0x01] = identity_service_get_attributes_all;

    /* Instance management */
    cls->get_instance = identity_get_instance;

    /* Register in registry */
    cip_registry_register_class(registry, cls);

    pdlog(LOG_MODULE_IDENTITY_OBJECT, LOG_LEVEL_INFO, "Registered Identity Object (Class 0x01)");
}
