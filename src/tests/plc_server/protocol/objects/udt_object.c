/***************************************************************************
 *   Copyright (C) 2025 by Kyle Hayes                                      *
 *   Author Kyle Hayes  kyle.hayes@gmail.com                               *
 *                                                                         *
 * This software is available under either the Mozilla Public License      *
 * version 2.0 or the GNU LGPL version 2 (or later) license, whichever     *
 * you choose.                                                             *
 *
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
#include "udt_object.h"
#include "../cip_message_router.h"
#include "../cip_path.h"
#include "../../plc_context.h"
#include "../../udt_storage.h"
#include "../../../utils/log.h"
#include "../../../utils/buf.h"
#include "../../../utils/err.h"

/* ============================================================================
 * Service: Get Attribute List (0x03)
 * ============================================================================ */

/**
 * Service 0x03: Get Attribute List
 *
 * Returns specific attributes of a UDT definition.
 *
 * Request format:
 *   [0-1]   uint16_le  Number of attributes to request
 *   [2+]    uint16_le[] Attribute IDs (0x01, 0x02, 0x04, 0x05 for UDTs)
 *
 * Response format:
 *   For each attribute, in order:
 *   - Attribute 0x01: struct handle/type (uint16_le)
 *   - Attribute 0x02: number of members (uint16_le)
 *   - Attribute 0x04: definition size in 32-bit words (uint32_le)
 *   - Attribute 0x05: instance size in bytes (uint32_le)
 */
static util_err_t udt_service_get_attribute_list(uint8_t service, const cip_path_t *path,
                                                 buf_t *request, buf_t *response,
                                                 cip_object_instance_t *instance, plc_context_t *plc) {
    (void)instance; /* Unused */

    pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_INFO, "Service 0x03 (Get Attribute List) CALLED");

    /* Get UDT ID from instance segment in path */
    /* Path should have: [class (0x6C)] [instance (UDT_ID)] */
    uint32_t udt_id = 0;
    for (size_t i = 0; i < path->segment_count; i++) {
        if (path->segments[i].type == CIP_SEGMENT_LOGICAL_INSTANCE_16BIT ||
            path->segments[i].type == CIP_SEGMENT_LOGICAL_INSTANCE_32BIT) {
            udt_id = path->segments[i].logical.id;
            break;
        }
    }

    pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_INFO, "Get Attribute List: UDT ID=%u", udt_id);

    /* Find the UDT by ID */
    udt_def_t *udt = NULL;
    for (udt_def_t *u = plc->udts; u; u = u->next) {
        if (u->udt_id == udt_id) {
            udt = u;
            break;
        }
    }

    if (!udt) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN, "Get Attribute List: UDT ID=%u not found", udt_id);
        cip_build_response(response, service, CIP_STATUS_PATH_DEST_UNKNOWN);
        return UTIL_ENOTFOUND;
    }

    /* Parse request to see which attributes are requested */
    uint16_t num_attrs = 0;
    if (!buf_read_u16_le(request, "num_attributes", &num_attrs)) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN, "Get Attribute List: failed to read num_attributes");
        cip_build_response(response, service, CIP_STATUS_INVALID_PARAM);
        return UTIL_EINVAL;
    }

    if (num_attrs == 0 || num_attrs > 10) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN, "Get Attribute List: invalid num_attributes=%u", num_attrs);
        cip_build_response(response, service, CIP_STATUS_INVALID_PARAM);
        return UTIL_EINVAL;
    }

    /* Build response header */
    util_err_t err = cip_build_response(response, service, CIP_STATUS_OK);
    if (err != UTIL_OK) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_ERROR, "Get Attribute List: failed to build response header");
        return err;
    }

    /* Store requested attribute IDs in order (must preserve request order in response) */
    uint16_t attr_ids[10];
    for (uint16_t i = 0; i < num_attrs; i++) {
        if (!buf_read_u16_le(request, "attribute_id", &attr_ids[i])) {
            pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN, "Get Attribute List: failed to read attribute_id[%u]", i);
            cip_build_response(response, service, CIP_STATUS_INVALID_PARAM);
            return UTIL_EIO;
        }
    }

    /* Pre-calculate attribute values - must match calculate_template_size_words() */
    size_t def_bytes = 0;
    /* Field descriptors: 8 bytes each */
    def_bytes += udt->member_count * 8;
    /* UDT name + null terminator */
    def_bytes += strlen(udt->name) + 1;
    /* Field names + null terminators */
    for (size_t j = 0; j < udt->member_count; j++) {
        def_bytes += strlen(udt->members[j].name) + 1;
    }
    uint32_t def_words = (uint32_t)((def_bytes + 3) / 4);
    uint32_t instance_size = (uint32_t)udt->total_size;
    uint16_t num_members = (uint16_t)udt->member_count;
    uint16_t handle = (uint16_t)udt->udt_id;

    /* Response format per CIP protocol:
     * [4 bytes CIP header - already written by cip_build_response]
     * [2 bytes: Attribute count]
     * [For each attribute:
     *   2 bytes: Attribute ID
     *   2 bytes: Status (0x00 0x00 for success)
     *   N bytes: Attribute value (size varies by attribute)
     * ]
     */

    bool ok = true;

    /* Write attribute count */
    ok &= buf_write_u16_le(response, "attr_count", num_attrs);

    /* Write each attribute in request order */
    for (uint16_t i = 0; i < num_attrs && ok; i++) {
        uint16_t attr_id = attr_ids[i];

        /* Write attribute ID */
        ok &= buf_write_u16_le(response, "attr_id", attr_id);

        switch (attr_id) {
            case 0x01: /* Structure handle/UDT ID */
                ok &= buf_write_u16_le(response, "attr_status", 0x0000);
                ok &= buf_write_u16_le(response, "attr_value", handle);
                break;

            case 0x02: /* Number of members */
                ok &= buf_write_u16_le(response, "attr_status", 0x0000);
                ok &= buf_write_u16_le(response, "attr_value", num_members);
                break;

            case 0x04: /* Member description size in 32-bit words */
                ok &= buf_write_u16_le(response, "attr_status", 0x0000);
                ok &= buf_write_u32_le(response, "attr_value", def_words);
                break;

            case 0x05: /* Instance size in bytes */
                ok &= buf_write_u16_le(response, "attr_status", 0x0000);
                ok &= buf_write_u32_le(response, "attr_value", instance_size);
                break;

            default: /* Unsupported attribute */
                ok &= buf_write_u16_le(response, "attr_status", CIP_STATUS_INVALID_ATTRIBUTE);
                /* No value written for error status */
                pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN, "Get Attribute List: unsupported attribute 0x%04x", attr_id);
                break;
        }
    }

    if (!ok) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_ERROR, "Get Attribute List: failed to write response data");
        return buf_get_error(response);
    }

    pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_INFO, "Get Attribute List: returned %u attributes for UDT '%s' (ID=%u, members=%u, def_size=%u words, instance_size=%u bytes)",
          num_attrs, udt->name, udt->udt_id, num_members, def_words, instance_size);

    return UTIL_OK;
}

/* ============================================================================
 * Service: Read Template (0x4C)
 * ============================================================================ */

/**
 * Helper: Calculate UDT template size in 32-bit words
 *
 * Template size includes:
 * - Field descriptors (8 bytes each)
 * - UDT name + null terminator
 * - Field names + null terminators
 */
static uint32_t calculate_template_size_words(udt_def_t *udt) {
    size_t bytes = 0;

    /* Field descriptors: 8 bytes each */
    bytes += udt->member_count * 8;

    /* UDT name + null terminator */
    bytes += strlen(udt->name) + 1;

    /* Field names + null terminators */
    for (size_t i = 0; i < udt->member_count; i++) {
        bytes += strlen(udt->members[i].name) + 1;
    }

    /* Convert to words (round up) */
    return (uint32_t)((bytes + 3) / 4);
}

/**
 * Service 0x4C: Read Template
 *
 * Returns detailed field information for a UDT including field descriptors,
 * UDT name, and field names.
 *
 * Request format:
 *   [0-3]   uint32_le  Byte offset (usually 0)
 *   [4-5]   uint16_le  Request size (bytes to read)
 *
 * Response format:
 *   [0-1]   uint16_le  UDT ID
 *   [2-5]   uint32_le  Member desc size in WORDS
 *   [6-9]   uint32_le  Instance size in BYTES
 *   [10-11] uint16_le  Number of members
 *   [12-13] uint16_le  Structure handle (UDT ID again)
 *   [14+]   Field descriptors (8 bytes each, N entries):
 *           [0-1]   uint16_le  Metadata (bit number for BOOL, element count for arrays, 0 for scalar)
 *           [2-3]   uint16_le  Field type (CIP code)
 *           [4-7]   uint32_le  Field byte offset
 *   [...+]  UDT name (zero-terminated string)
 *   [...+]  Field names (zero-terminated strings, one per member)
 */
static util_err_t udt_service_read_template(uint8_t service, const cip_path_t *path,
                                           buf_t *request, buf_t *response,
                                           cip_object_instance_t *instance, plc_context_t *plc) {
    (void)instance; /* Unused */

    pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_INFO, "Service 0x4C (Read Template) CALLED");

    /* Get UDT ID from instance segment in path */
    uint32_t udt_id = 0;
    for (size_t i = 0; i < path->segment_count; i++) {
        if (path->segments[i].type == CIP_SEGMENT_LOGICAL_INSTANCE_16BIT ||
            path->segments[i].type == CIP_SEGMENT_LOGICAL_INSTANCE_32BIT) {
            udt_id = path->segments[i].logical.id;
            break;
        }
    }

    pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_INFO, "Read Template: UDT ID=%u", udt_id);

    /* Parse request parameters */
    uint32_t byte_offset = 0;
    uint16_t request_size = 0;
    if (!buf_read_u32_le(request, "byte_offset", &byte_offset)) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN, "Read Template: failed to read byte_offset");
        cip_build_response(response, service, CIP_STATUS_INVALID_PARAM);
        return UTIL_EINVAL;
    }
    if (!buf_read_u16_le(request, "request_size", &request_size)) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN, "Read Template: failed to read request_size");
        cip_build_response(response, service, CIP_STATUS_INVALID_PARAM);
        return UTIL_EINVAL;
    }

    if (byte_offset != 0) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN, "Read Template: non-zero byte_offset %u", byte_offset);
        /* Continue anyway - client may request partial template */
    }

    /* Find the UDT by ID */
    udt_def_t *udt = NULL;
    for (udt_def_t *u = plc->udts; u; u = u->next) {
        if (u->udt_id == udt_id) {
            udt = u;
            break;
        }
    }

    if (!udt) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN, "Read Template: UDT ID=%u not found", udt_id);
        cip_build_response(response, service, CIP_STATUS_PATH_DEST_UNKNOWN);
        return UTIL_ENOTFOUND;
    }

    /* Build response header */
    util_err_t err = cip_build_response(response, service, CIP_STATUS_OK);
    if (err != UTIL_OK) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_ERROR, "Read Template: failed to build response header");
        return err;
    }

    bool ok = true;

    /* Write field descriptors (8 bytes each) - start immediately after CIP header */
    for (size_t i = 0; i < udt->member_count && ok; i++) {
        udt_member_t *member = &udt->members[i];

        /* Calculate metadata field */
        uint16_t metadata = 0;
        if (member->symbol_type == 0xC1) {  /* CIP_TYPE_BOOL */
            /* For BOOL: metadata = bit number (0-7) */
            metadata = (uint16_t)member->bit_offset;
        } else if (member->dimensions[0] > 0) {
            /* For arrays: metadata = element count */
            metadata = (uint16_t)member->dimensions[0];
        }
        /* For scalars: metadata = 0 (default) */

        /* Write field descriptor */
        ok &= buf_write_u16_le(response, "field_metadata", metadata);
        ok &= buf_write_u16_le(response, "field_type", member->symbol_type);
        ok &= buf_write_u32_le(response, "field_offset", (uint32_t)member->byte_offset);
    }

    /* Write UDT name (zero-terminated) */
    if (ok) {
        ok &= buf_write_bytes(response, "udt_name", (const uint8_t*)udt->name, strlen(udt->name));
        ok &= buf_write_u8(response, "udt_name_null", 0);
    }

    /* Write field names (zero-terminated) */
    for (size_t i = 0; i < udt->member_count && ok; i++) {
        udt_member_t *member = &udt->members[i];
        ok &= buf_write_bytes(response, "field_name", (const uint8_t*)member->name, strlen(member->name));
        ok &= buf_write_u8(response, "field_name_null", 0);
    }

    if (!ok) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_ERROR, "Read Template: failed to write response data");
        return buf_get_error(response);
    }

    pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_INFO, "Read Template: returned template for UDT '%s' (ID=%u, %zu members)",
          udt->name, udt->udt_id, udt->member_count);

    return UTIL_OK;
}

/* ============================================================================
 * Instance Management
 * ============================================================================ */

/**
 * Get instance - returns instance for the requested UDT ID
 *
 * For UDT Definition Object, the instance_id IS the UDT ID.
 * We accept any UDT ID and store the plc context for the service handler to use.
 */
static cip_object_instance_t *udt_get_instance(uint32_t instance_id, plc_context_t *plc) {
    /* For UDT objects, instance_id is the UDT ID (1-4095) */
    /* We accept all valid UDT IDs */
    if (instance_id == 0 || instance_id > 4095) {
        return NULL;  /* Invalid UDT ID */
    }

    /* Allocate instance */
    cip_object_instance_t *inst = (cip_object_instance_t *)calloc(1, sizeof(*inst));
    if (!inst) return NULL;

    inst->instance_id = instance_id;
    inst->instance_data = (void *)plc;  /* Store plc context */

    return inst;
}

/* ============================================================================
 * CIP Object Registration
 * ============================================================================ */

/**
 * Register UDT Definition Object (Class 0x6C) with CIP registry
 * Supports both ControlLogix and Micro800 PLC types.
 */
int udt_object_register(cip_object_registry_t *registry, plc_context_t *plc) {
    if (!registry || !plc) {
        return -1;
    }

    /* Create object class */
    cip_object_class_t *cls = (cip_object_class_t *)calloc(1, sizeof(*cls));
    if (!cls) {
        return -1;
    }

    cls->class_id = 0x6C;      /* UDT Definition Object */
    cls->class_name = "UDT Definition Object";

    /* Register service handlers */
    cls->service_handlers[0x03] = udt_service_get_attribute_list;  /* Get Attribute List */
    cls->service_handlers[0x4C] = udt_service_read_template;       /* Read Template */

    /* Instance management */
    cls->get_instance = udt_get_instance;

    /* Register in registry */
    if (cip_registry_register_class(registry, cls) != UTIL_OK) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_ERROR, "Failed to register UDT Definition Object");
        free(cls);
        return -1;
    }

    pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_INFO, "Registered UDT Definition Object (Class 0x6C) with Services 0x03 (Get Attribute List) and 0x4C (Read Template)");

    return 0;
}
