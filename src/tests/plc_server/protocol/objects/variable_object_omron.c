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

/* Include cip_path.h before other headers that might forward-declare it */
#include "../cip_path.h"

#include "variable_object_omron.h"
#include "../cip_message_router.h"
#include "../../omron_storage.h"
#include "../../plc_context.h"
#include "../../../utils/log.h"
#include "../../../utils/buf.h"

/* ============================================================================
 * Variable Object Context Storage
 * ============================================================================ */

typedef struct {
    omron_registry_t *registry;
} variable_object_omron_context_t;

static variable_object_omron_context_t *vo_context = NULL;

/* ============================================================================
 * Service: Get Attribute All (0x01)
 * ============================================================================ */

/**
 * Service 0x01: Get Attributes All on Instance N
 *
 * Returns all attributes for a variable instance.
 *
 * Request:
 *   (empty)
 *
 * Response:
 *   [0-1]   uint16_le  Size in bytes
 *   [2]     uint8      Type code (CIP type or 0xA0/0xA2 for structures)
 *   [3]     uint8      Array type (0=scalar, 1-3=array)
 *   [4]     uint8      Dimension count (0 or array dim count)
 *   [5]     uint8      Reserved/padding
 *   [6-9]   uint32_le  Dimension 1 size (0 if not applicable)
 *   [10-13] uint32_le  Dimension 2 size (0 if not applicable)
 *   [14-17] uint32_le  Dimension 3 size (0 if not applicable)
 *   [18]    uint8      Bit position (0 for byte-oriented)
 *   [19-20] uint16_le  Reserved
 *   [21-24] uint32_le  Type instance ID (for structures, 0 for primitives)
 */
static util_err_t variable_service_get_attributes_all(uint8_t service, const cip_path_t *path, buf_t *request, buf_t *response,
                                                      cip_object_instance_t *instance, plc_context_t *plc) {

    (void)request;

    omron_variable_t *var = NULL;

    /* If path has a symbolic segment, look up the variable by tag name */
    if(path && path->segment_count > 0 && path->segments[0].type == CIP_SEGMENT_SYMBOLIC) {
        if(!vo_context || !vo_context->registry) {
            pdlog(LOG_MODULE_OMRON_VARIABLE_OBJECT, LOG_LEVEL_WARN, "Get Attributes All: registry not initialized");
            cip_build_response(response, service, CIP_STATUS_PATH_DEST_UNKNOWN);
            return UTIL_ENOTFOUND;
        }

        /* Look up variable by tag name */
        const char *tag_name = path->segments[0].symbolic.name;
        size_t tag_name_len = path->segments[0].symbolic.length;
        var = omron_variable_find_by_name_len(vo_context->registry, tag_name, tag_name_len);

        if(!var) {
            pdlog(LOG_MODULE_OMRON_VARIABLE_OBJECT, LOG_LEVEL_WARN, "Get Attributes All: variable '%.*s' not found",
                  (int)tag_name_len, tag_name);
            cip_build_response(response, service, CIP_STATUS_PATH_DEST_UNKNOWN);
            return UTIL_ENOTFOUND;
        }
    } else if(instance && instance->instance_data) {
        /* Use provided instance */
        var = (omron_variable_t *)instance->instance_data;
    } else {
        pdlog(LOG_MODULE_OMRON_VARIABLE_OBJECT, LOG_LEVEL_WARN, "Get Attributes All: invalid instance");
        cip_build_response(response, service, CIP_STATUS_PATH_DEST_UNKNOWN);
        return UTIL_ENOTFOUND;
    }

    pdlog(LOG_MODULE_OMRON_VARIABLE_OBJECT, LOG_LEVEL_DETAIL, "Get Attributes All: variable '%s' id=%u size=%zu type=0x%02X",
          var->name, var->instance_id, var->data_size, var->type_code);

    /* Build response */
    cip_build_response(response, service, CIP_STATUS_OK);

    bool ok = true;
    ok &= buf_write_u16_le(response, "size", (uint16_t)var->data_size);
    ok &= buf_write_u8(response, "type_code", var->type_code);
    ok &= buf_write_u8(response, "array_type", var->dim_count > 0 ? 1 : 0);
    ok &= buf_write_u8(response, "dim_count", var->dim_count);
    ok &= buf_write_u8(response, "reserved1", 0);

    /* Write dimensions */
    ok &= buf_write_u32_le(response, "dim1", var->dim_count > 0 ? var->dimensions[0] : 0);
    ok &= buf_write_u32_le(response, "dim2", var->dim_count > 1 ? var->dimensions[1] : 0);
    ok &= buf_write_u32_le(response, "dim3", var->dim_count > 2 ? var->dimensions[2] : 0);

    /* Bit position and reserved */
    ok &= buf_write_u8(response, "bit_position", 0);
    ok &= buf_write_u16_le(response, "reserved2", 0);

    /* Type instance ID (for structures) */
    ok &= buf_write_u32_le(response, "type_instance_id", var->type_instance_id);

    if(!ok) {
        pdlog(LOG_MODULE_OMRON_VARIABLE_OBJECT, LOG_LEVEL_ERROR, "Get Attributes All: failed to write response");
        return buf_get_error(response);
    }

    return UTIL_OK;
}

/* ============================================================================
 * Service: Read Tag (0x4C)
 * ============================================================================ */

/**
 * Service 0x4C: Read Tag
 *
 * Reads variable data by instance ID.
 * Supports partial reads using 0x80 data segments in the path.
 *
 * Request:
 *   [0-1]  uint16_le  Element count (ignored if 0x80 data segment in path)
 *
 * Response:
 *   [0]    uint8      Type code
 *   [1]    uint8      Reserved
 *   [2-3]  uint16_le  Element count returned
 *   [4+]   uint8[]    Data
 */
static util_err_t variable_service_read_tag(uint8_t service, const cip_path_t *path, buf_t *request, buf_t *response,
                                            cip_object_instance_t *instance, plc_context_t *plc) {

    (void)plc;

    if(!instance || !instance->instance_data) {
        pdlog(LOG_MODULE_OMRON_VARIABLE_OBJECT, LOG_LEVEL_WARN, "Read Tag: invalid instance");
        cip_build_response(response, service, CIP_STATUS_PATH_DEST_UNKNOWN);
        return UTIL_ENOTFOUND;
    }

    omron_variable_t *var = (omron_variable_t *)instance->instance_data;

    /* Parse request: element count */
    uint16_t element_count = 0;
    if(!buf_read_u16_le(request, "element_count", &element_count)) {
        pdlog(LOG_MODULE_OMRON_VARIABLE_OBJECT, LOG_LEVEL_WARN, "Read Tag: failed to read element count");
        cip_build_response(response, service, CIP_STATUS_INVALID_PARAM);
        return UTIL_EINVAL;
    }

    /* Check for 0x80 data segment in path (partial read) */
    uint32_t offset_bytes = 0;
    uint16_t segment_count = 0;

    if(path && path->segment_count > 0) {
        /* Look for data segment (0x80) */
        for(size_t i = 0; i < path->segment_count; i++) {
            if(path->segments[i].type == CIP_SEGMENT_DATA) {
                offset_bytes = path->segments[i].data.offset_bytes;
                segment_count = path->segments[i].data.element_count;
                break;
            }
        }
    }

    /* Determine read parameters */
    uint32_t read_offset = offset_bytes;
    uint16_t read_count = segment_count > 0 ? segment_count : element_count;

    if(read_count == 0) {
        pdlog(LOG_MODULE_OMRON_VARIABLE_OBJECT, LOG_LEVEL_WARN, "Read Tag: element count is 0");
        cip_build_response(response, service, CIP_STATUS_INVALID_PARAM);
        return UTIL_EINVAL;
    }

    /* Validate offset and count */
    if(read_offset >= var->data_size) {
        pdlog(LOG_MODULE_OMRON_VARIABLE_OBJECT, LOG_LEVEL_WARN, "Read Tag: offset %u beyond variable size %zu", read_offset,
              var->data_size);
        cip_build_response(response, service, CIP_STATUS_INVALID_PARAM);
        return UTIL_EINVAL;
    }

    size_t bytes_available = var->data_size - read_offset;
    size_t bytes_to_read = read_count;
    if(bytes_to_read > bytes_available) { bytes_to_read = bytes_available; }

    pdlog(LOG_MODULE_OMRON_VARIABLE_OBJECT, LOG_LEVEL_DETAIL, "Read Tag: variable '%s' offset=%u count=%u bytes=%zu", var->name,
          read_offset, read_count, bytes_to_read);

    /* Build response header */
    cip_build_response(response, service, CIP_STATUS_OK);

    bool ok = true;
    ok &= buf_write_u8(response, "type_code", var->type_code);
    ok &= buf_write_u8(response, "reserved", 0);
    ok &= buf_write_u16_le(response, "count", (uint16_t)(bytes_to_read));

    /* Write data */
    if(bytes_to_read > 0) { ok &= buf_write_bytes(response, "data", var->data + read_offset, bytes_to_read); }

    if(!ok) {
        pdlog(LOG_MODULE_OMRON_VARIABLE_OBJECT, LOG_LEVEL_ERROR, "Read Tag: failed to write response");
        return buf_get_error(response);
    }

    pdlog(LOG_MODULE_OMRON_VARIABLE_OBJECT, LOG_LEVEL_DETAIL, "Read Tag: success - variable '%s' bytes=%zu", var->name,
          bytes_to_read);

    return UTIL_OK;
}

/* ============================================================================
 * Service: Write Tag (0x4D)
 * ============================================================================ */

/**
 * Service 0x4D: Write Tag
 *
 * Writes variable data by instance ID.
 * Supports partial writes using 0x80 data segments in the path.
 *
 * Request:
 *   [0]    uint8      Type code
 *   [1]    uint8      Reserved
 *   [2-3]  uint16_le  Element count
 *   [4+]   uint8[]    Data
 *
 * Response:
 *   (empty on success)
 */
static util_err_t variable_service_write_tag(uint8_t service, const cip_path_t *path, buf_t *request, buf_t *response,
                                             cip_object_instance_t *instance, plc_context_t *plc) {

    (void)plc;

    if(!instance || !instance->instance_data) {
        pdlog(LOG_MODULE_OMRON_VARIABLE_OBJECT, LOG_LEVEL_WARN, "Write Tag: invalid instance");
        cip_build_response(response, service, CIP_STATUS_PATH_DEST_UNKNOWN);
        return UTIL_ENOTFOUND;
    }

    omron_variable_t *var = (omron_variable_t *)instance->instance_data;

    /* Parse request header */
    uint8_t tag_type = 0;
    uint16_t element_count = 0;

    if(!buf_read_u8(request, "type_code", &tag_type)) {
        pdlog(LOG_MODULE_OMRON_VARIABLE_OBJECT, LOG_LEVEL_WARN, "Write Tag: failed to read type code");
        cip_build_response(response, service, CIP_STATUS_INVALID_PARAM);
        return UTIL_EINVAL;
    }

    if(!buf_read_u8(request, "reserved", NULL)) {
        pdlog(LOG_MODULE_OMRON_VARIABLE_OBJECT, LOG_LEVEL_WARN, "Write Tag: failed to read reserved byte");
        cip_build_response(response, service, CIP_STATUS_INVALID_PARAM);
        return UTIL_EINVAL;
    }

    if(!buf_read_u16_le(request, "element_count", &element_count)) {
        pdlog(LOG_MODULE_OMRON_VARIABLE_OBJECT, LOG_LEVEL_WARN, "Write Tag: failed to read element count");
        cip_build_response(response, service, CIP_STATUS_INVALID_PARAM);
        return UTIL_EINVAL;
    }

    /* Check for 0x80 data segment in path (partial write) */
    uint32_t offset_bytes = 0;
    uint16_t segment_count = 0;

    if(path && path->segment_count > 0) {
        /* Look for data segment (0x80) */
        for(size_t i = 0; i < path->segment_count; i++) {
            if(path->segments[i].type == CIP_SEGMENT_DATA) {
                offset_bytes = path->segments[i].data.offset_bytes;
                segment_count = path->segments[i].data.element_count;
                break;
            }
        }
    }

    /* Determine write parameters */
    uint32_t write_offset = offset_bytes;
    uint16_t write_count = segment_count > 0 ? segment_count : element_count;

    if(write_count == 0) {
        pdlog(LOG_MODULE_OMRON_VARIABLE_OBJECT, LOG_LEVEL_WARN, "Write Tag: element count is 0");
        cip_build_response(response, service, CIP_STATUS_INVALID_PARAM);
        return UTIL_EINVAL;
    }

    /* Validate type code (basic check) */
    if(tag_type != var->type_code) {
        pdlog(LOG_MODULE_OMRON_VARIABLE_OBJECT, LOG_LEVEL_WARN, "Write Tag: type mismatch (expected 0x%02X, got 0x%02X)",
              var->type_code, tag_type);
        cip_build_response(response, service, CIP_STATUS_INVALID_PARAM);
        return UTIL_EINVAL;
    }

    /* Validate offset and count */
    if(write_offset >= var->data_size) {
        pdlog(LOG_MODULE_OMRON_VARIABLE_OBJECT, LOG_LEVEL_WARN, "Write Tag: offset %u beyond variable size %zu", write_offset,
              var->data_size);
        cip_build_response(response, service, CIP_STATUS_INVALID_PARAM);
        return UTIL_EINVAL;
    }

    size_t bytes_available = var->data_size - write_offset;
    size_t bytes_to_write = write_count;
    if(bytes_to_write > bytes_available) { bytes_to_write = bytes_available; }

    /* Check if request has enough data */
    size_t remaining = buf_read_size(request);
    if(remaining < bytes_to_write) {
        pdlog(LOG_MODULE_OMRON_VARIABLE_OBJECT, LOG_LEVEL_WARN, "Write Tag: insufficient data (%zu needed, %zu available)",
              bytes_to_write, remaining);
        cip_build_response(response, service, CIP_STATUS_INVALID_PARAM);
        return UTIL_EINVAL;
    }

    pdlog(LOG_MODULE_OMRON_VARIABLE_OBJECT, LOG_LEVEL_DETAIL, "Write Tag: variable '%s' offset=%u count=%u bytes=%zu", var->name,
          write_offset, write_count, bytes_to_write);

    /* Read data from request and write to variable */
    bool ok = true;
    ok &= buf_read_bytes(request, "data", var->data + write_offset, bytes_to_write);

    if(!ok) {
        pdlog(LOG_MODULE_OMRON_VARIABLE_OBJECT, LOG_LEVEL_ERROR, "Write Tag: failed to read data from request");
        cip_build_response(response, service, CIP_STATUS_INVALID_PARAM);
        return buf_get_error(request);
    }

    /* Build response (empty on success) */
    cip_build_response(response, service, CIP_STATUS_OK);

    pdlog(LOG_MODULE_OMRON_VARIABLE_OBJECT, LOG_LEVEL_DETAIL, "Write Tag: success - variable '%s' bytes=%zu", var->name,
          bytes_to_write);

    return UTIL_OK;
}

/* ============================================================================
 * Instance Management
 * ============================================================================ */

static cip_object_instance_t *variable_get_instance(uint32_t instance_id, plc_context_t *plc) {
    (void)plc;

    if(!vo_context || !vo_context->registry) { return NULL; }

    /* Find variable by instance ID */
    omron_variable_t *var = omron_variable_find_by_id(vo_context->registry, instance_id);
    if(!var) { return NULL; }

    /* Allocate instance structure and wrap variable */
    cip_object_instance_t *instance = (cip_object_instance_t *)calloc(1, sizeof(*instance));
    if(!instance) { return NULL; }

    instance->object_class = NULL; /* Will be set by registry */
    instance->instance_id = instance_id;
    instance->instance_data = (void *)var;

    return instance;
}

/* ============================================================================
 * Registration
 * ============================================================================ */

void variable_object_omron_register(cip_object_registry_t *registry, omron_registry_t *omron_registry) {
    if(!registry || !omron_registry) { return; }

    /* Store registry for service handlers */
    if(!vo_context) {
        vo_context = (variable_object_omron_context_t *)calloc(1, sizeof(*vo_context));
        if(!vo_context) { return; }
    }
    vo_context->registry = omron_registry;

    /* Create and register the object class */
    cip_object_class_t *cls = (cip_object_class_t *)calloc(1, sizeof(*cls));
    if(!cls) { return; }

    cls->class_id = 0x6B;
    cls->class_name = "Variable Object (Omron)";

    /* Register service handlers */
    cls->service_handlers[0x01] = variable_service_get_attributes_all;
    cls->service_handlers[0x4C] = variable_service_read_tag;
    cls->service_handlers[0x4D] = variable_service_write_tag;

    /* Instance management */
    cls->get_instance = variable_get_instance;

    /* Register in registry */
    cip_registry_register_class(registry, cls);

    pdlog(LOG_MODULE_OMRON_VARIABLE_OBJECT, LOG_LEVEL_INFO, "Registered Variable Object (Class 0x6B)");
}
