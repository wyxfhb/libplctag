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
#include "symbol_object_controllogix.h"
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
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN, "Read Tag (CLogix): failed to read element count");
        cip_build_response(response, service, CIP_STATUS_INVALID_PARAM);
        return UTIL_EINVAL;
    }

    if(element_count == 0) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN, "Read Tag (CLogix): element count is 0");
        cip_build_response(response, service, CIP_STATUS_INVALID_PARAM);
        return UTIL_EINVAL;
    }

    /* Find tag by name from path */
    if(path->segments[0].type != CIP_SEGMENT_SYMBOLIC) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN, "Read Tag (CLogix): no symbolic segment in path");
        cip_build_response(response, service, CIP_STATUS_PATH_DEST_UNKNOWN);
        return UTIL_ENOTFOUND;
    }

    /* Create null-terminated tag name for lookup */
    char tag_name[256] = {0};
    size_t name_len = path->segments[0].symbolic.length;
    if(name_len >= sizeof(tag_name)) { name_len = sizeof(tag_name) - 1; }
    memcpy(tag_name, path->segments[0].symbolic.name, name_len);
    tag_name[name_len] = '\0'; /* not needed because of the {0} above? */

    pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_DETAIL, "Read Tag (CLogix): looking up '%s' (element_count=%u)", tag_name,
          element_count);

    /* Look up tag */
    tag_def_t *tag = tag_find_by_name(plc->tags, tag_name);
    if(!tag) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN, "Read Tag (CLogix): tag '%s' not found", tag_name);
        cip_build_response(response, service, CIP_STATUS_PATH_DEST_UNKNOWN);
        return UTIL_ENOTFOUND;
    }

    /* Validate element count */
    if(element_count > tag->elem_count) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN, "Read Tag (CLogix): requested %u elements but tag has %zu", element_count,
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
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN,
              "Read Tag (CLogix): response buffer too small (%zu needed, %zu available)", bytes_to_read,
              buf_write_size(response));
        /* Truncate to what fits */
        bytes_to_read = buf_write_size(response);
    }

    /* Write tag data */
    ok &= buf_write_bytes(response, "data", tag->data, bytes_to_read);

    if(!ok) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_ERROR, "Read Tag (CLogix): failed to write response");
        return buf_get_error(response);
    }

    pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_DETAIL, "Read Tag (CLogix): success - tag '%s' type=0x%04X bytes=%zu", tag_name,
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
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN, "Write Tag (CLogix): failed to read element count");
        cip_build_response(response, service, CIP_STATUS_INVALID_PARAM);
        return UTIL_EINVAL;
    }

    if(element_count == 0) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN, "Write Tag (CLogix): element count is 0");
        cip_build_response(response, service, CIP_STATUS_INVALID_PARAM);
        return UTIL_EINVAL;
    }

    /* Find tag by name from path */
    if(path->segments[0].type != CIP_SEGMENT_SYMBOLIC) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN, "Write Tag (CLogix): no symbolic segment in path");
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
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN, "Write Tag (CLogix): failed to read tag type");
        cip_build_response(response, service, CIP_STATUS_INVALID_PARAM);
        return UTIL_EINVAL;
    }

    pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_DETAIL, "Write Tag (CLogix): looking up '%s' (element_count=%u, type=0x%04X)",
          tag_name, element_count, tag_type);

    /* Look up tag */
    tag_def_t *tag = tag_find_by_name(plc->tags, tag_name);
    if(!tag) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN, "Write Tag (CLogix): tag '%s' not found", tag_name);
        cip_build_response(response, service, CIP_STATUS_PATH_DEST_UNKNOWN);
        return UTIL_ENOTFOUND;
    }

    /* Validate tag type matches */
    if(tag->tag_type != tag_type) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN, "Write Tag (CLogix): tag type mismatch (expected 0x%04X, got 0x%04X)",
              tag->tag_type, tag_type);
        cip_build_response(response, service, CIP_STATUS_INVALID_PARAM);
        return UTIL_EINVAL;
    }

    /* Validate element count */
    if(element_count > tag->elem_count) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN, "Write Tag (CLogix): requested %u elements but tag has %zu",
              element_count, tag->elem_count);
        cip_build_response(response, service, CIP_STATUS_INVALID_PARAM);
        return UTIL_EINVAL;
    }

    /* Calculate bytes to write */
    size_t bytes_to_write = element_count * tag->elem_size;

    /* Check if request has enough data */
    size_t remaining = buf_read_size(request);
    if(remaining < bytes_to_write) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN, "Write Tag (CLogix): insufficient data (%zu needed, %zu available)",
              bytes_to_write, remaining);
        cip_build_response(response, service, CIP_STATUS_INVALID_PARAM);
        return UTIL_EINVAL;
    }

    /* Write data to tag storage */
    bool ok = true;
    ok &= buf_read_bytes(request, "data", tag->data, bytes_to_write);

    if(!ok) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_ERROR, "Write Tag (CLogix): failed to read data from request");
        cip_build_response(response, service, CIP_STATUS_INVALID_PARAM);
        return buf_get_error(request);
    }

    /* Build response */
    cip_build_response(response, service, CIP_STATUS_OK);

    pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_DETAIL, "Write Tag (CLogix): success - tag '%s' type=0x%04X bytes=%zu", tag_name,
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
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN, "Read Tag Fragmented (CLogix): failed to read element count");
        cip_build_response(response, service, CIP_STATUS_INVALID_PARAM);
        return UTIL_EINVAL;
    }

    if(!buf_read_u16_le(request, "offset", &offset)) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN, "Read Tag Fragmented (CLogix): failed to read offset");
        cip_build_response(response, service, CIP_STATUS_INVALID_PARAM);
        return UTIL_EINVAL;
    }

    if(element_count == 0) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN, "Read Tag Fragmented (CLogix): element count is 0");
        cip_build_response(response, service, CIP_STATUS_INVALID_PARAM);
        return UTIL_EINVAL;
    }

    /* Find tag by name from path */
    if(path->segments[0].type != CIP_SEGMENT_SYMBOLIC) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN, "Read Tag Fragmented (CLogix): no symbolic segment in path");
        cip_build_response(response, service, CIP_STATUS_PATH_DEST_UNKNOWN);
        return UTIL_ENOTFOUND;
    }

    /* Create null-terminated tag name for lookup */
    char tag_name[256];
    size_t name_len = path->segments[0].symbolic.length;
    if(name_len >= sizeof(tag_name)) { name_len = sizeof(tag_name) - 1; }
    memcpy(tag_name, path->segments[0].symbolic.name, name_len);
    tag_name[name_len] = '\0';

    pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_DETAIL, "Read Tag Fragmented (CLogix): looking up '%s' (count=%u, offset=%u)",
          tag_name, element_count, offset);

    /* Look up tag */
    tag_def_t *tag = tag_find_by_name(plc->tags, tag_name);
    if(!tag) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN, "Read Tag Fragmented (CLogix): tag '%s' not found", tag_name);
        cip_build_response(response, service, CIP_STATUS_PATH_DEST_UNKNOWN);
        return UTIL_ENOTFOUND;
    }

    /* Validate offset */
    if(offset >= tag->elem_count) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN, "Read Tag Fragmented (CLogix): offset %u >= tag size %zu", offset,
              tag->elem_count);
        cip_build_response(response, service, CIP_STATUS_INVALID_PARAM);
        return UTIL_EINVAL;
    }

    /* Validate that we don't read past end of tag */
    size_t available = tag->elem_count - offset;
    if(element_count > available) {
        element_count = (uint16_t)available;
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_DETAIL, "Read Tag Fragmented (CLogix): truncating to %u elements",
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
              "Read Tag Fragmented (CLogix): response buffer too small (%zu needed, %zu available)", bytes_to_read,
              buf_write_size(response));
        bytes_to_read = buf_write_size(response);
    }

    /* Write tag data from offset */
    uint8_t *data_ptr = (uint8_t *)tag->data + byte_offset;
    ok &= buf_write_bytes(response, "data", data_ptr, bytes_to_read);

    if(!ok) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_ERROR, "Read Tag Fragmented (CLogix): failed to write response");
        return buf_get_error(response);
    }

    pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_DETAIL,
          "Read Tag Fragmented (CLogix): success - tag '%s' type=0x%04X bytes=%zu offset=%u", tag_name, tag->tag_type,
          bytes_to_read, offset);

    return UTIL_OK;
}

/* ============================================================================
 * Service: List Tags (0x55)
 * ============================================================================ */

/**
 * Service 0x55: List Tags
 *
 * Request:
 *   (empty)
 *
 * Response:
 *   [0-1]  uint16_le  Tag count
 *   [2+]   Tag list (tag structure repeated)
 *
 * Each tag structure:
 *   [0-1]  uint16_le  Tag type
 *   [2-3]  uint16_le  Tag name length (words)
 *   [4+]   uint8[]    Tag name (padded to word boundary)
 */
static util_err_t symbol_service_list_tags(uint8_t service, const cip_path_t *path, buf_t *request, buf_t *response,
                                           cip_object_instance_t *instance, plc_context_t *plc) {

    (void)path;
    (void)instance;
    (void)request;

    /* Build response header */
    cip_build_response(response, service, CIP_STATUS_OK);

    /* Count tags */
    size_t tag_count = 0;
    tag_def_t *tag = plc->tags;
    while(tag) {
        tag_count++;
        tag = tag->next;
    }

    pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_DETAIL, "List Tags (CLogix): found %zu tags", tag_count);

    /* Write tag count */
    bool ok = true;
    ok &= buf_write_u16_le(response, "tag_count", (uint16_t)tag_count);

    /* Write each tag */
    tag = plc->tags;
    while(tag && ok) {
        /* Write tag type */
        ok &= buf_write_u16_le(response, "tag_type", tag->tag_type);

        /* Calculate tag name length in words (including padding) */
        size_t name_len = strlen(tag->name);
        size_t name_words = (name_len + 1) / 2;
        if(name_len % 2 == 0) { name_words = name_len / 2; }

        /* Write name length in words */
        ok &= buf_write_u16_le(response, "name_length", (uint16_t)name_words);

        /* Write name with padding */
        uint8_t padded_name[256];
        memset(padded_name, 0, sizeof(padded_name));
        memcpy(padded_name, tag->name, name_len);
        size_t padded_len = name_words * 2;
        ok &= buf_write_bytes(response, "name", padded_name, padded_len);

        tag = tag->next;
    }

    if(!ok) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_ERROR, "List Tags (CLogix): failed to write response");
        return buf_get_error(response);
    }

    pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_DETAIL, "List Tags (CLogix): response built successfully");

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

void symbol_object_controllogix_register(cip_object_registry_t *registry) {
    cip_object_class_t *cls = (cip_object_class_t *)calloc(1, sizeof(*cls));
    if(!cls) { return; }

    cls->class_id = 0x6B;
    cls->class_name = "Symbol Object (ControlLogix)";

    /* Register service handlers */
    cls->service_handlers[0x4C] = symbol_service_read_tag;            /* Read Tag */
    cls->service_handlers[0x4D] = symbol_service_write_tag;           /* Write Tag */
    cls->service_handlers[0x52] = symbol_service_read_tag_fragmented; /* Read Tag Fragmented */
    cls->service_handlers[0x55] = symbol_service_list_tags;           /* List Tags */

    /* Instance management */
    cls->get_instance = symbol_get_instance;

    /* Register in registry */
    cip_registry_register_class(registry, cls);

    pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_INFO, "Registered Symbol Object (Class 0x6B) for ControlLogix");
}
