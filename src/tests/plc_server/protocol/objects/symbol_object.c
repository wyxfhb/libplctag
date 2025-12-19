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
#include "symbol_object.h"
#include "../cip_path.h"
#include "../cip_message_router.h"
#include "../../tag_storage.h"
#include "../../plc_context.h"
#include "../../../utils/log.h"
#include "../../../utils/buf.h"

/* ============================================================================
 * Service: Read Tag (0x4C)
 * ============================================================================ */

/**
 * Service 0x4C: Read Tag
 *
 * Request:
 *   [0-1]  uint16_le  Element count
 *
 * Response:
 *   [0-1]  uint16_le  Tag type
 *   [2+]   uint8[]    Tag data
 */
static util_err_t symbol_service_read_tag(uint8_t service, const cip_path_t *path, buf_t *request, buf_t *response,
                                          cip_object_instance_t *instance, plc_context_t *plc) {

    (void)instance; /* Symbol object doesn't use instance data */

    /* Parse request: element count */
    uint16_t element_count = 0;
    if(!buf_read_u16_le(request, "element_count", &element_count)) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN, "Read Tag: failed to read element count");
        cip_build_response(response, service, CIP_STATUS_INVALID_PARAM);
        return UTIL_EINVAL;
    }

    if(element_count == 0) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN, "Read Tag: element count is 0");
        cip_build_response(response, service, CIP_STATUS_INVALID_PARAM);
        return UTIL_EINVAL;
    }

    /* Find tag by name from path */
    if(path->segments[0].type != CIP_SEGMENT_SYMBOLIC) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN, "Read Tag: no symbolic segment in path");
        cip_build_response(response, service, CIP_STATUS_PATH_DEST_UNKNOWN);
        return UTIL_ENOTFOUND;
    }

    /* Create null-terminated tag name for lookup */
    char tag_name[256];
    size_t name_len = path->segments[0].symbolic.length;
    if(name_len >= sizeof(tag_name)) { name_len = sizeof(tag_name) - 1; }
    memcpy(tag_name, path->segments[0].symbolic.name, name_len);
    tag_name[name_len] = '\0';

    pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_DETAIL, "Read Tag: looking up '%s' (element_count=%u)", tag_name, element_count);

    /* Look up tag */
    tag_def_t *tag = tag_find_by_name(plc->tags, tag_name);
    if(!tag) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN, "Read Tag: tag '%s' not found", tag_name);
        cip_build_response(response, service, CIP_STATUS_PATH_DEST_UNKNOWN);
        return UTIL_ENOTFOUND;
    }

    /* Validate element count */
    if(element_count > tag->elem_count) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN, "Read Tag: requested %u elements but tag has %zu", element_count,
              tag->elem_count);
        cip_build_response(response, service, CIP_STATUS_INVALID_PARAM);
        return UTIL_EINVAL;
    }

    /* Build response header with tag type */
    cip_build_response(response, service, CIP_STATUS_OK);

    bool ok = true;
    ok &= buf_write_u16_le(response, "tag_type", tag->tag_type);

    /* Calculate bytes to read */
    size_t bytes_to_read = element_count * tag->elem_size;

    /* Check if we have space in response buffer */
    if(buf_write_size(response) < bytes_to_read) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN, "Read Tag: response buffer too small (%zu needed, %zu available)",
              bytes_to_read, buf_write_size(response));
        /* Truncate to what fits */
        bytes_to_read = buf_write_size(response);
    }

    /* Write tag data */
    ok &= buf_write_bytes(response, "data", tag->data, bytes_to_read);

    if(!ok) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_ERROR, "Read Tag: failed to write response");
        return buf_get_error(response);
    }

    pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_DETAIL, "Read Tag: success - tag '%s' type=0x%04X bytes=%zu", tag_name,
          tag->tag_type, bytes_to_read);

    return UTIL_OK;
}

/* ============================================================================
 * Service: Write Tag (0x4D)
 * ============================================================================ */

/**
 * Service 0x4D: Write Tag
 *
 * Request:
 *   [0-1]  uint16_le  Element count
 *   [2+]   uint8[]    Tag type (uint16_le) + tag data
 *
 * Response:
 *   (empty on success)
 */
static util_err_t symbol_service_write_tag(uint8_t service, const cip_path_t *path, buf_t *request, buf_t *response,
                                           cip_object_instance_t *instance, plc_context_t *plc) {

    (void)instance; /* Symbol object doesn't use instance data */

    /* Parse request: element count */
    uint16_t element_count = 0;
    if(!buf_read_u16_le(request, "element_count", &element_count)) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN, "Write Tag: failed to read element count");
        cip_build_response(response, service, CIP_STATUS_INVALID_PARAM);
        return UTIL_EINVAL;
    }

    if(element_count == 0) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN, "Write Tag: element count is 0");
        cip_build_response(response, service, CIP_STATUS_INVALID_PARAM);
        return UTIL_EINVAL;
    }

    /* Find tag by name from path */
    if(path->segments[0].type != CIP_SEGMENT_SYMBOLIC) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN, "Write Tag: no symbolic segment in path");
        cip_build_response(response, service, CIP_STATUS_PATH_DEST_UNKNOWN);
        return UTIL_ENOTFOUND;
    }

    /* Create null-terminated tag name for lookup */
    char tag_name[256];
    size_t name_len = path->segments[0].symbolic.length;
    if(name_len >= sizeof(tag_name)) { name_len = sizeof(tag_name) - 1; }
    memcpy(tag_name, path->segments[0].symbolic.name, name_len);
    tag_name[name_len] = '\0';

    /* Read tag type from request */
    uint16_t tag_type = 0;
    if(!buf_read_u16_le(request, "tag_type", &tag_type)) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN, "Write Tag: failed to read tag type");
        cip_build_response(response, service, CIP_STATUS_INVALID_PARAM);
        return UTIL_EINVAL;
    }

    pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_DETAIL, "Write Tag: looking up '%s' (element_count=%u, type=0x%04X)", tag_name,
          element_count, tag_type);

    /* Look up tag */
    tag_def_t *tag = tag_find_by_name(plc->tags, tag_name);
    if(!tag) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN, "Write Tag: tag '%s' not found", tag_name);
        cip_build_response(response, service, CIP_STATUS_PATH_DEST_UNKNOWN);
        return UTIL_ENOTFOUND;
    }

    /* Validate tag type matches */
    if(tag->tag_type != tag_type) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN, "Write Tag: tag type mismatch (expected 0x%04X, got 0x%04X)",
              tag->tag_type, tag_type);
        cip_build_response(response, service, CIP_STATUS_INVALID_PARAM);
        return UTIL_EINVAL;
    }

    /* Validate element count */
    if(element_count > tag->elem_count) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN, "Write Tag: requested %u elements but tag has %zu", element_count,
              tag->elem_count);
        cip_build_response(response, service, CIP_STATUS_INVALID_PARAM);
        return UTIL_EINVAL;
    }

    /* Calculate bytes to write */
    size_t bytes_to_write = element_count * tag->elem_size;

    /* Check if request has enough data */
    size_t remaining = buf_read_size(request);
    if(remaining < bytes_to_write) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN, "Write Tag: insufficient data (%zu needed, %zu available)",
              bytes_to_write, remaining);
        cip_build_response(response, service, CIP_STATUS_INVALID_PARAM);
        return UTIL_EINVAL;
    }

    /* Write data to tag storage */
    bool ok = true;
    ok &= buf_read_bytes(request, "data", tag->data, bytes_to_write);

    if(!ok) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_ERROR, "Write Tag: failed to read data from request");
        cip_build_response(response, service, CIP_STATUS_INVALID_PARAM);
        return buf_get_error(request);
    }

    /* Build response */
    cip_build_response(response, service, CIP_STATUS_OK);

    pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_DETAIL, "Write Tag: success - tag '%s' type=0x%04X bytes=%zu", tag_name,
          tag->tag_type, bytes_to_write);

    return UTIL_OK;
}

/* ============================================================================
 * Service: Read Tag Fragmented (0x52)
 * ============================================================================ */

/**
 * Service 0x52: Read Tag Fragmented
 *
 * Allows reading large tags in multiple fragments.
 *
 * Request:
 *   [0-1]  uint16_le  Element count
 *   [2-3]  uint16_le  Offset (in elements)
 *
 * Response:
 *   [0-1]  uint16_le  Tag type
 *   [2+]   uint8[]    Tag data (partial)
 */
static util_err_t symbol_service_read_tag_fragmented(uint8_t service, const cip_path_t *path, buf_t *request, buf_t *response,
                                                     cip_object_instance_t *instance, plc_context_t *plc) {

    (void)instance;

    /* Parse request: element count and offset */
    uint16_t element_count = 0;
    uint16_t offset = 0;
    if(!buf_read_u16_le(request, "element_count", &element_count)) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN, "Read Tag Fragmented: failed to read element count");
        cip_build_response(response, service, CIP_STATUS_INVALID_PARAM);
        return UTIL_EINVAL;
    }

    if(!buf_read_u16_le(request, "offset", &offset)) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN, "Read Tag Fragmented: failed to read offset");
        cip_build_response(response, service, CIP_STATUS_INVALID_PARAM);
        return UTIL_EINVAL;
    }

    if(element_count == 0) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN, "Read Tag Fragmented: element count is 0");
        cip_build_response(response, service, CIP_STATUS_INVALID_PARAM);
        return UTIL_EINVAL;
    }

    /* Find tag by name from path */
    if(path->segments[0].type != CIP_SEGMENT_SYMBOLIC) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN, "Read Tag Fragmented: no symbolic segment in path");
        cip_build_response(response, service, CIP_STATUS_PATH_DEST_UNKNOWN);
        return UTIL_ENOTFOUND;
    }

    /* Create null-terminated tag name for lookup */
    char tag_name[256];
    size_t name_len = path->segments[0].symbolic.length;
    if(name_len >= sizeof(tag_name)) { name_len = sizeof(tag_name) - 1; }
    memcpy(tag_name, path->segments[0].symbolic.name, name_len);
    tag_name[name_len] = '\0';

    pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_DETAIL, "Read Tag Fragmented: looking up '%s' (count=%u, offset=%u)",
          tag_name, element_count, offset);

    /* Look up tag */
    tag_def_t *tag = tag_find_by_name(plc->tags, tag_name);
    if(!tag) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN, "Read Tag Fragmented: tag '%s' not found", tag_name);
        cip_build_response(response, service, CIP_STATUS_PATH_DEST_UNKNOWN);
        return UTIL_ENOTFOUND;
    }

    /* Validate offset */
    if(offset >= tag->elem_count) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN, "Read Tag Fragmented: offset %u >= tag size %zu", offset,
              tag->elem_count);
        cip_build_response(response, service, CIP_STATUS_INVALID_PARAM);
        return UTIL_EINVAL;
    }

    /* Validate that we don't read past end of tag */
    size_t available = tag->elem_count - offset;
    if(element_count > available) {
        element_count = (uint16_t)available;
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_DETAIL, "Read Tag Fragmented: truncating to %u elements",
              element_count);
    }

    /* Build response header with tag type */
    cip_build_response(response, service, CIP_STATUS_OK);

    bool ok = true;
    ok &= buf_write_u16_le(response, "tag_type", tag->tag_type);

    /* Calculate bytes to read from offset */
    size_t byte_offset = offset * tag->elem_size;
    size_t bytes_to_read = element_count * tag->elem_size;

    /* Check if we have space in response buffer */
    if(buf_write_size(response) < bytes_to_read) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN,
              "Read Tag Fragmented: response buffer too small (%zu needed, %zu available)", bytes_to_read,
              buf_write_size(response));
        bytes_to_read = buf_write_size(response);
    }

    /* Write tag data from offset */
    uint8_t *data_ptr = (uint8_t *)tag->data + byte_offset;
    ok &= buf_write_bytes(response, "data", data_ptr, bytes_to_read);

    if(!ok) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_ERROR, "Read Tag Fragmented: failed to write response");
        return buf_get_error(response);
    }

    pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_DETAIL,
          "Read Tag Fragmented: success - tag '%s' type=0x%04X bytes=%zu offset=%u", tag_name, tag->tag_type,
          bytes_to_read, offset);

    return UTIL_OK;
}

/* ============================================================================
 * Service: List Tags (0x55)
 * ============================================================================ */

/**
 * Service 0x55: List Tags
 *
 * Response format:
 *   [0-3]   uint32_le  Instance ID
 *   [4-5]   uint16_le  Symbol type
 *   [6-7]   uint16_le  Element length (bytes)
 *   [8-19]  uint32_le  Array dimensions (3 x uint32_le)
 *   [20-21] uint16_le  String length (name length in bytes)
 *   [22+]   uint8[]    Tag name (raw bytes, no padding)
 *
 * Supports pagination: status code 0x06 indicates more data available
 */
static util_err_t symbol_service_list_tags(uint8_t service, const cip_path_t *path, buf_t *request, buf_t *response,
                                           cip_object_instance_t *instance, plc_context_t *plc) {

    (void)instance;

    pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_DETAIL, "List Tags: starting list_tags service");

    /* Parse pagination from path if present */
    uint32_t start_instance = 0;

    if(path && path->segment_count > 1) {
        if(path->segments[1].type == CIP_SEGMENT_LOGICAL_INSTANCE_8BIT ||
           path->segments[1].type == CIP_SEGMENT_LOGICAL_INSTANCE_16BIT ||
           path->segments[1].type == CIP_SEGMENT_LOGICAL_INSTANCE_32BIT) {
            start_instance = path->segments[1].logical.id;
            pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_DETAIL, "List Tags: pagination instance = %u", start_instance);
        }
    }

    /* Get negotiated max packet size */
    uint16_t max_packet_size = plc->server_to_client_max_packet;

    /* Sanity check: if packet size is unreasonably large (> 1000 bytes), use default 504 */
    if(max_packet_size == 0 || max_packet_size > 1000) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_DETAIL, "List Tags: suspicious negotiated packet size=%u, using default 504",
              max_packet_size);
        max_packet_size = 504;
    }

    /* Calculate available space for tag data:
     * - max_packet_size = 504 bytes (negotiated during Forward Open)
     * - CPF data item overhead = 4 bytes (type field: 2 + length field: 2)
     * - Sequence ID = 2 bytes (counted within the 504 bytes)
     * - CIP response header = 4 bytes
     * - Available for tag data = 504 - 4 - 2 - 4 = 494 bytes
     */
    size_t cpf_overhead = 4;
    size_t sequence_id_size = 2;
    size_t cip_header_size = 4;
    size_t max_tag_data = max_packet_size - cpf_overhead - sequence_id_size - cip_header_size;

    pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_DETAIL, "List Tags: using packet size=%u, max_tag_data=%zu",
          max_packet_size, max_tag_data);

    /* Find starting position for pagination */
    tag_def_t *tag = plc->tags;
    tag_def_t *start_tag = NULL;
    size_t total_tags = 0;

    while(tag) {
        if(start_tag == NULL && tag->instance_id >= start_instance) {
            start_tag = tag;
        }
        total_tags++;
        tag = tag->next;
    }

    pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_DETAIL, "List Tags: found %zu total tags, starting from instance %u",
          total_tags, start_instance);

    /* Reserve space for CIP response header (reply service + reserved + status) */
    buf_t cip_header_buf;
    if(!buf_reserve_write(response, 4, &cip_header_buf)) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_ERROR, "List Tags: failed to reserve CIP header space");
        return buf_get_error(response);
    }

    /* Write tag entries starting from start_tag, checking space before each */
    bool ok = true;
    bool more_data = false;
    tag = start_tag;
    size_t entries_written = 0;
    size_t tag_data_written = 0;

    while(tag && ok) {
        /* Calculate size of this tag entry */
        size_t name_len = strlen(tag->name);
        size_t entry_size = 4 +      /* instance_id */
                           2 +      /* symbol_type */
                           2 +      /* element_length */
                           12 +     /* array dimensions (3 x 4) */
                           2 +      /* string_length */
                           name_len; /* tag name */

        /* Check if this entry fits in remaining space */
        if(tag_data_written + entry_size > max_tag_data) {
            /* Can't fit this tag, we have more data */
            more_data = true;
            break;
        }

        /* Instance ID (4 bytes) - use the permanent instance_id assigned at tag creation */
        ok &= buf_write_u32_le(response, "instance_id", tag->instance_id);

        /* Symbol type with dimension flags (2 bytes) */
        /* Dimension encoding: bits [14:13] = dimension count - 1 (0-3, for 1-4 dimensions) */
        uint16_t dim_count_encoded = (uint16_t)((tag->dim_count > 0) ? (tag->dim_count - 1) : 0);
        if(dim_count_encoded > 3) dim_count_encoded = 3; /* Cap at 3 bits */

        uint16_t symbol_type;
        if(tag->udt_id != 0) {
            /* UDT-based tag: set bit 15 (0x8000) and include UDT ID in bits 11-0 */
            symbol_type = 0x8000 | (uint16_t)(((dim_count_encoded & 0x3) << 13)) | (tag->udt_id & 0x0FFF);
        } else {
            /* Built-in type: dimension flags in bits 14-13, type code in lower bits */
            symbol_type = tag->tag_type | (uint16_t)(((dim_count_encoded & 0x3) << 13));
        }
        ok &= buf_write_u16_le(response, "symbol_type", symbol_type);

        /* Element length in bytes (2 bytes) */
        ok &= buf_write_u16_le(response, "element_length", (uint16_t)tag->elem_size);

        /* Array dimensions (3 x 4 bytes = 12 bytes total) */
        for(size_t i = 0; i < 3; i++) {
            uint32_t dim = (i < tag->dim_count) ? (uint32_t)tag->dimensions[i] : 0;
            ok &= buf_write_u32_le(response, "array_dim", dim);
        }

        /* Tag name length in BYTES (2 bytes) */
        ok &= buf_write_u16_le(response, "string_length", (uint16_t)name_len);

        /* Tag name (no padding, just raw bytes) */
        ok &= buf_write_bytes(response, "tag_name", (const uint8_t *)tag->name, name_len);

        if(!ok) break;

        /* Update counters for this successful entry */
        tag_data_written += entry_size;
        entries_written++;
        tag = tag->next;
    }

    if(!ok) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_ERROR, "List Tags: failed to write response");
        return buf_get_error(response);
    }

    /* Now fill in the CIP header at the reserved location */
    uint8_t status_code = more_data ? 0x06 : CIP_STATUS_OK;

    buf_reset(&cip_header_buf);
    bool header_ok = true;
    header_ok &= buf_write_u8(&cip_header_buf, "reply_service", service | 0x80);
    header_ok &= buf_write_u8(&cip_header_buf, "reserved", 0x00);
    header_ok &= buf_write_u8(&cip_header_buf, "status_code", status_code);
    header_ok &= buf_write_u8(&cip_header_buf, "ext_status_size", 0x00);

    if(!header_ok) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_ERROR, "List Tags: failed to write CIP header");
        return buf_get_error(&cip_header_buf);
    }

    pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_DETAIL, "List Tags: sent %zu entries (%zu bytes), status=0x%02X",
          entries_written, tag_data_written, status_code);

    return UTIL_OK;
}

/* ============================================================================
 * Instance Management
 * ============================================================================ */

static cip_object_instance_t *symbol_get_instance(uint32_t instance_id, plc_context_t *plc) {
    (void)instance_id;
    (void)plc;

    /* Symbol object doesn't use instances - all requests route through tags.
     * We return a dummy instance just to satisfy the registry dispatch. */
    static cip_object_instance_t instance = {
        .object_class = NULL, /* Will be set by registry */
        .instance_id = 0,
        .instance_data = NULL,
    };
    return &instance;
}

/* ============================================================================
 * Registration
 * ============================================================================ */

void symbol_object_register(cip_object_registry_t *registry) {
    cip_object_class_t *cls = (cip_object_class_t *)calloc(1, sizeof(*cls));
    if(!cls) { return; }

    cls->class_id = 0x6B;
    cls->class_name = "Symbol Object";

    /* Register service handlers - supports both ControlLogix and Micro800 */
    cls->service_handlers[0x4C] = symbol_service_read_tag;             /* Read Tag */
    cls->service_handlers[0x4D] = symbol_service_write_tag;            /* Write Tag */
    cls->service_handlers[0x52] = symbol_service_read_tag_fragmented;  /* Read Tag Fragmented */
    cls->service_handlers[0x55] = symbol_service_list_tags;            /* List Tags */

    /* Instance management */
    cls->get_instance = symbol_get_instance;

    /* Register in registry */
    cip_registry_register_class(registry, cls);

    pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_INFO, "Registered Symbol Object (Class 0x6B) with Services 0x4C (Read Tag), 0x4D (Write Tag), 0x52 (Read Tag Fragmented), and 0x55 (List Tags)");
}
