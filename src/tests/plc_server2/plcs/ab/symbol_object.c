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

#include <string.h>
#include <inttypes.h>
#include "symbol_object.h"
#include "ab_context.h"
#include "ab_plc_logix.h"
#include "../generic/tag_storage.h"
#include "../../protocols/cip/cip_defs.h"
#include "../../protocols/cip/cip_path.h"
#include "../../protocols/cip/cip_protocol.h"
#include "../../../utils/log.h"

/* ============================================================================
 * Helper Functions
 * ============================================================================ */

/**
 * Calculate request offsets for tag read/write with fragmentation support
 */
static bool calculate_request_offsets(tag_def_t *tag, uint16_t element_count, uint32_t fragment_offset,
                                      size_t *request_start_byte, size_t *request_bytes_needed) {
    size_t total_tag_bytes = tag->elem_count * tag->elem_size;
    size_t bytes_to_read = element_count * tag->elem_size;

    *request_start_byte = fragment_offset;
    *request_bytes_needed = bytes_to_read;

    if(bytes_to_read + fragment_offset > total_tag_bytes) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN,
              "Request exceeds tag bounds: offset=%zu, bytes_needed=%zu, total_tag_bytes=%zu", fragment_offset, bytes_to_read,
              total_tag_bytes);
        return false;
    }

    return true;
}

/* ============================================================================
 * Service: Read Tag (0x4C)
 * ============================================================================ */

static util_err_t handle_read_tag(
    uint8_t service_code,
    cip_path_t *path,
    buf_t *input,
    buf_t *output,
    ab_plc_context_t *plc,
    client_context_t *client) {

    (void)client;

    if(!plc) {
        cip_build_response(output, service_code, CIP_STATUS_INVALID_PARAM);
        return UTIL_EINVAL;
    }

    /* Parse request: element count */
    uint16_t element_count = 0;
    if(!buf_read_u16_le(input, "element_count", &element_count)) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN, "Read Tag: failed to read element count");
        cip_build_response(output, service_code, CIP_STATUS_INVALID_PARAM);
        return UTIL_EINVAL;
    }

    if(element_count == 0) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN, "Read Tag: element count is 0");
        cip_build_response(output, service_code, CIP_STATUS_INVALID_PARAM);
        return UTIL_EINVAL;
    }

    /* Find tag by name from path */
    if(path->segment_count == 0 || path->segments[0].type != CIP_SEGMENT_SYMBOLIC) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN, "Read Tag: no symbolic segment in path");
        cip_build_response(output, service_code, CIP_STATUS_PATH_DEST_UNKNOWN);
        return UTIL_ENOTFOUND;
    }

    /* Look up tag */
    tag_def_t *tag = tag_find_by_name(plc->tags, plc->tag_count, path->segments[0].symbolic.name,
                                       path->segments[0].symbolic.length);
    if(!tag) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN, "Read Tag: tag '%.*s' not found",
              (int)path->segments[0].symbolic.length, path->segments[0].symbolic.name);
        cip_build_response(output, service_code, CIP_STATUS_PATH_DEST_UNKNOWN);
        return UTIL_ENOTFOUND;
    }

    pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_DETAIL, "Read Tag: '%.*s' (element_count=%u)",
          (int)path->segments[0].symbolic.length, path->segments[0].symbolic.name, element_count);

    /* Calculate offsets */
    size_t request_start_byte = 0;
    size_t request_bytes_needed = 0;
    if(!calculate_request_offsets(tag, element_count, 0, &request_start_byte, &request_bytes_needed)) {
        cip_build_response(output, service_code, CIP_STATUS_INVALID_PARAM);
        return UTIL_EINVAL;
    }

    /* Calculate how much data fits in response */
    size_t available_response_bytes = buf_write_size(output) - 2; /* -2 for tag type */
    size_t bytes_to_send = available_response_bytes;
    if(bytes_to_send > request_bytes_needed) {
        bytes_to_send = request_bytes_needed;
    }

    /* Build response header */
    cip_build_response(output, service_code, CIP_STATUS_OK);

    bool ok = true;
    ok &= buf_write_u16_le(output, "tag_type", tag->tag_type);
    ok &= buf_write_bytes(output, "data", (uint8_t *)tag->data + request_start_byte, bytes_to_send);

    if(!ok) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN, "Read Tag: failed to write response");
        return buf_get_error(output);
    }

    pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_DETAIL, "Read Tag: '%.*s' type=0x%04X bytes=%zu",
          (int)path->segments[0].symbolic.length, path->segments[0].symbolic.name, tag->tag_type, bytes_to_send);

    return UTIL_OK;
}

/* ============================================================================
 * Service: Write Tag (0x4D)
 * ============================================================================ */

static util_err_t handle_write_tag(
    uint8_t service_code,
    cip_path_t *path,
    buf_t *input,
    buf_t *output,
    ab_plc_context_t *plc,
    client_context_t *client) {

    (void)client;

    if(!plc) {
        cip_build_response(output, service_code, CIP_STATUS_INVALID_PARAM);
        return UTIL_EINVAL;
    }

    /* Parse request: element count */
    uint16_t element_count = 0;
    if(!buf_read_u16_le(input, "element_count", &element_count)) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN, "Write Tag: failed to read element count");
        cip_build_response(output, service_code, CIP_STATUS_INVALID_PARAM);
        return UTIL_EINVAL;
    }

    if(element_count == 0) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN, "Write Tag: element count is 0");
        cip_build_response(output, service_code, CIP_STATUS_INVALID_PARAM);
        return UTIL_EINVAL;
    }

    /* Read tag type from request */
    uint16_t tag_type = 0;
    if(!buf_read_u16_le(input, "tag_type", &tag_type)) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN, "Write Tag: failed to read tag type");
        cip_build_response(output, service_code, CIP_STATUS_INVALID_PARAM);
        return UTIL_EINVAL;
    }

    /* Find tag by name from path */
    if(path->segment_count == 0 || path->segments[0].type != CIP_SEGMENT_SYMBOLIC) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN, "Write Tag: no symbolic segment in path");
        cip_build_response(output, service_code, CIP_STATUS_PATH_DEST_UNKNOWN);
        return UTIL_ENOTFOUND;
    }

    /* Look up tag */
    tag_def_t *tag = tag_find_by_name(plc->tags, plc->tag_count, path->segments[0].symbolic.name,
                                       path->segments[0].symbolic.length);
    if(!tag) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN, "Write Tag: tag '%.*s' not found",
              (int)path->segments[0].symbolic.length, path->segments[0].symbolic.name);
        cip_build_response(output, service_code, CIP_STATUS_PATH_DEST_UNKNOWN);
        return UTIL_ENOTFOUND;
    }

    pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_DETAIL, "Write Tag: '%.*s' (element_count=%u, type=0x%04X)",
          (int)path->segments[0].symbolic.length, path->segments[0].symbolic.name, element_count, tag_type);

    /* Validate tag type matches */
    if(tag->tag_type != tag_type) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN, "Write Tag: tag type mismatch (expected 0x%04X, got 0x%04X)",
              tag->tag_type, tag_type);
        cip_build_response(output, service_code, CIP_STATUS_INVALID_PARAM);
        return UTIL_EINVAL;
    }

    /* Calculate offsets */
    size_t request_start_byte = 0;
    size_t request_bytes_needed = 0;
    if(!calculate_request_offsets(tag, element_count, 0, &request_start_byte, &request_bytes_needed)) {
        cip_build_response(output, service_code, CIP_STATUS_INVALID_PARAM);
        return UTIL_EINVAL;
    }

    /* Check if request has enough data */
    size_t remaining = buf_read_size(input);
    if(remaining < request_bytes_needed) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN, "Write Tag: insufficient data (%zu needed, %zu available)",
              request_bytes_needed, remaining);
        cip_build_response(output, service_code, CIP_STATUS_INVALID_PARAM);
        return UTIL_EINVAL;
    }

    /* Write data to tag storage */
    bool ok = true;
    ok &= buf_read_bytes(input, "data", (uint8_t *)tag->data + request_start_byte, request_bytes_needed);

    if(!ok) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN, "Write Tag: failed to read data from request");
        cip_build_response(output, service_code, CIP_STATUS_INVALID_PARAM);
        return buf_get_error(input);
    }

    /* Build response */
    cip_build_response(output, service_code, CIP_STATUS_OK);

    pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_DETAIL, "Write Tag: '%.*s' type=0x%04X bytes=%zu",
          (int)path->segments[0].symbolic.length, path->segments[0].symbolic.name, tag->tag_type, request_bytes_needed);

    return UTIL_OK;
}

/* ============================================================================
 * Service: Read Tag Fragmented (0x52)
 * ============================================================================ */

static util_err_t handle_read_tag_fragmented(
    uint8_t service_code,
    cip_path_t *path,
    buf_t *input,
    buf_t *output,
    ab_plc_context_t *plc,
    client_context_t *client) {

    (void)client;

    if(!plc) {
        cip_build_response(output, service_code, CIP_STATUS_INVALID_PARAM);
        return UTIL_EINVAL;
    }

    /* Parse request: element count and byte offset */
    uint16_t element_count = 0;
    uint32_t fragment_offset = 0;
    if(!buf_read_u16_le(input, "element_count", &element_count)) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN, "Read Tag Fragmented: failed to read element count");
        cip_build_response(output, service_code, CIP_STATUS_INVALID_PARAM);
        return UTIL_EINVAL;
    }

    if(!buf_read_u32_le(input, "offset", &fragment_offset)) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN, "Read Tag Fragmented: failed to read offset");
        cip_build_response(output, service_code, CIP_STATUS_INVALID_PARAM);
        return UTIL_EINVAL;
    }

    if(element_count == 0) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN, "Read Tag Fragmented: element count is 0");
        cip_build_response(output, service_code, CIP_STATUS_INVALID_PARAM);
        return UTIL_EINVAL;
    }

    /* Find tag by name from path */
    if(path->segment_count == 0 || path->segments[0].type != CIP_SEGMENT_SYMBOLIC) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN, "Read Tag Fragmented: no symbolic segment in path");
        cip_build_response(output, service_code, CIP_STATUS_PATH_DEST_UNKNOWN);
        return UTIL_ENOTFOUND;
    }

    /* Look up tag */
    tag_def_t *tag = tag_find_by_name(plc->tags, plc->tag_count, path->segments[0].symbolic.name,
                                       path->segments[0].symbolic.length);
    if(!tag) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN, "Read Tag Fragmented: tag '%.*s' not found",
              (int)path->segments[0].symbolic.length, path->segments[0].symbolic.name);
        cip_build_response(output, service_code, CIP_STATUS_PATH_DEST_UNKNOWN);
        return UTIL_ENOTFOUND;
    }

    pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_DETAIL, "Read Tag Fragmented: '%.*s' (element_count=%u, offset=%u)",
          (int)path->segments[0].symbolic.length, path->segments[0].symbolic.name, element_count, fragment_offset);

    /* Calculate offsets */
    size_t request_start_byte = 0;
    size_t request_bytes_needed = 0;
    if(!calculate_request_offsets(tag, element_count, fragment_offset, &request_start_byte, &request_bytes_needed)) {
        cip_build_response(output, service_code, CIP_STATUS_INVALID_PARAM);
        return UTIL_EINVAL;
    }

    /* Calculate how much we can send */
    size_t available_response_bytes = buf_write_size(output) - 2; /* -2 for tag type */
    size_t bytes_to_send = available_response_bytes;

    size_t remaining_bytes = request_bytes_needed - fragment_offset;
    if(bytes_to_send > remaining_bytes) {
        bytes_to_send = remaining_bytes;
    }

    /* Determine if this is the last fragment */
    bool is_last_fragment = (fragment_offset + bytes_to_send >= request_bytes_needed);
    uint8_t response_status = is_last_fragment ? CIP_STATUS_OK : CIP_STATUS_PARTIAL_TRANSFER;

    /* Build response header */
    cip_build_response(output, service_code, response_status);

    bool ok = true;
    ok &= buf_write_u16_le(output, "tag_type", tag->tag_type);
    ok &= buf_write_bytes(output, "data", (uint8_t *)tag->data + request_start_byte, bytes_to_send);

    if(!ok) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN, "Read Tag Fragmented: failed to write response");
        return buf_get_error(output);
    }

    pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_DETAIL, "Read Tag Fragmented: '%.*s' type=0x%04X bytes=%zu offset=%u final=%d",
          (int)path->segments[0].symbolic.length, path->segments[0].symbolic.name, tag->tag_type, bytes_to_send, fragment_offset,
          is_last_fragment);

    return UTIL_OK;
}

/* ============================================================================
 * Service: Write Tag Fragmented (0x53)
 * ============================================================================ */

static util_err_t handle_write_tag_fragmented(
    uint8_t service_code,
    cip_path_t *path,
    buf_t *input,
    buf_t *output,
    ab_plc_context_t *plc,
    client_context_t *client) {

    (void)client;

    if(!plc) {
        cip_build_response(output, service_code, CIP_STATUS_INVALID_PARAM);
        return UTIL_EINVAL;
    }

    /* Parse request: tag type, element count, and byte offset */
    uint16_t tag_type = 0;
    uint16_t element_count = 0;
    uint32_t fragment_offset = 0;

    if(!buf_read_u16_le(input, "tag_type", &tag_type)) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN, "Write Tag Fragmented: failed to read tag type");
        cip_build_response(output, service_code, CIP_STATUS_INVALID_PARAM);
        return UTIL_EINVAL;
    }

    if(!buf_read_u16_le(input, "element_count", &element_count)) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN, "Write Tag Fragmented: failed to read element count");
        cip_build_response(output, service_code, CIP_STATUS_INVALID_PARAM);
        return UTIL_EINVAL;
    }

    if(!buf_read_u32_le(input, "offset", &fragment_offset)) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN, "Write Tag Fragmented: failed to read offset");
        cip_build_response(output, service_code, CIP_STATUS_INVALID_PARAM);
        return UTIL_EINVAL;
    }

    if(element_count == 0) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN, "Write Tag Fragmented: element count is 0");
        cip_build_response(output, service_code, CIP_STATUS_INVALID_PARAM);
        return UTIL_EINVAL;
    }

    /* Find tag by name from path */
    if(path->segment_count == 0 || path->segments[0].type != CIP_SEGMENT_SYMBOLIC) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN, "Write Tag Fragmented: no symbolic segment in path");
        cip_build_response(output, service_code, CIP_STATUS_PATH_DEST_UNKNOWN);
        return UTIL_ENOTFOUND;
    }

    /* Look up tag */
    tag_def_t *tag = tag_find_by_name(plc->tags, plc->tag_count, path->segments[0].symbolic.name,
                                       path->segments[0].symbolic.length);
    if(!tag) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN, "Write Tag Fragmented: tag '%.*s' not found",
              (int)path->segments[0].symbolic.length, path->segments[0].symbolic.name);
        cip_build_response(output, service_code, CIP_STATUS_PATH_DEST_UNKNOWN);
        return UTIL_ENOTFOUND;
    }

    pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_DETAIL, "Write Tag Fragmented: '%.*s' (element_count=%u, offset=%u)",
          (int)path->segments[0].symbolic.length, path->segments[0].symbolic.name, element_count, fragment_offset);

    /* Validate tag type matches */
    if(tag->tag_type != tag_type) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN, "Write Tag Fragmented: tag type mismatch (expected 0x%04X, got 0x%04X)",
              tag->tag_type, tag_type);
        cip_build_response(output, service_code, CIP_STATUS_INVALID_PARAM);
        return UTIL_EINVAL;
    }

    /* Calculate offsets */
    size_t request_start_byte = 0;
    size_t request_bytes_needed = 0;
    if(!calculate_request_offsets(tag, element_count, fragment_offset, &request_start_byte, &request_bytes_needed)) {
        cip_build_response(output, service_code, CIP_STATUS_INVALID_PARAM);
        return UTIL_EINVAL;
    }

    /* How much data do we have in this fragment? */
    size_t data_available = buf_read_size(input);

    /* How much data can we write in this fragment? */
    size_t remaining_bytes = request_bytes_needed - fragment_offset;
    size_t bytes_to_write = (data_available < remaining_bytes) ? data_available : remaining_bytes;

    /* Validate we're not trying to write past the tag */
    if(fragment_offset + bytes_to_write > request_bytes_needed) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN,
              "Write Tag Fragmented: writing past end (offset=%u, bytes=%zu, needed=%zu)", fragment_offset,
              bytes_to_write, request_bytes_needed);
        cip_build_response(output, service_code, CIP_STATUS_INVALID_PARAM);
        return UTIL_EINVAL;
    }

    /* Write data to tag storage */
    bool ok = true;
    ok &= buf_read_bytes(input, "data", (uint8_t *)tag->data + request_start_byte, bytes_to_write);

    if(!ok) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN, "Write Tag Fragmented: failed to read data from request");
        cip_build_response(output, service_code, CIP_STATUS_INVALID_PARAM);
        return buf_get_error(input);
    }

    /* Build response */
    cip_build_response(output, service_code, CIP_STATUS_OK);

    pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_DETAIL, "Write Tag Fragmented: '%.*s' type=0x%04X bytes=%zu offset=%u",
          (int)path->segments[0].symbolic.length, path->segments[0].symbolic.name, tag->tag_type, bytes_to_write,
          fragment_offset);

    return UTIL_OK;
}

/* ============================================================================
 * Symbol Object Registration
 * ============================================================================ */

util_err_t symbol_object_register(cip_class_registry_t *registry) {
    if(!registry) {
        return UTIL_EINVAL;
    }

    util_err_t err = UTIL_OK;

    /* Register read tag handler (0x4C) */
    err = cip_class_registry_add_service(registry, 0x6B, 0x4C, handle_read_tag);
    if(err != UTIL_OK) { return err; }

    /* Register write tag handler (0x4D) */
    err = cip_class_registry_add_service(registry, 0x6B, 0x4D, handle_write_tag);
    if(err != UTIL_OK) { return err; }

    /* Register read tag fragmented handler (0x52) */
    err = cip_class_registry_add_service(registry, 0x6B, 0x52, handle_read_tag_fragmented);
    if(err != UTIL_OK) { return err; }

    /* Register write tag fragmented handler (0x53) */
    err = cip_class_registry_add_service(registry, 0x6B, 0x53, handle_write_tag_fragmented);
    if(err != UTIL_OK) { return err; }

    return UTIL_OK;
}
