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

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include "logix_symbol_object.h"
#include "../cip_defs.h"
#include "../cip_path.h"
#include "../cip_message_router.h"
#include "../../tag_storage.h"
#include "../../plc_context.h"
#include "../../../utils/log.h"
#include "../../../utils/buf.h"

/* ============================================================================
 * CIP Service Codes
 * ============================================================================ */

// #define CIP_SRV_READ_TAG ((uint8_t)0x4C)
// #define CIP_SRV_WRITE_TAG ((uint8_t)0x4D)
// #define CIP_SRV_READ_TAG_FRAG_AB ((uint8_t)0x52)
// #define CIP_SRV_WRITE_TAG_FRAG_AB ((uint8_t)0x53)
// #define CIP_SRV_INSTANCE_ATTRS_AB ((uint8_t)0x55)

/* ============================================================================
 * Helper Functions
 * ============================================================================ */

/**
 * Calculate request start and end byte offsets for tag read/write operations.
 *
 * Handles fragmented requests where the offset parameter tracks progress
 * through a multi-fragment logical operation.
 *
 * @param tag Tag definition
 * @param element_count Number of elements to read/write in this logical request
 * @param fragment_offset Bytes already processed in previous fragments (0 for first request)
 * @param request_start_byte Pointer to store calculated byte offset in tag where data starts
 * @param request_bytes_needed Pointer to store total bytes needed for complete logical request
 * @return true if offsets are valid, false if request exceeds tag bounds
 */
static bool calculate_request_offsets(tag_def_t *tag, uint16_t element_count, uint32_t fragment_offset,
                                      size_t *request_start_byte, size_t *request_bytes_needed) {
    size_t total_tag_bytes = tag->elem_count * tag->elem_size;
    size_t bytes_to_read = element_count * tag->elem_size;

    /* Calculate where in the tag we start reading/writing */
    *request_start_byte = fragment_offset;
    *request_bytes_needed = bytes_to_read;

    /* Validate that the complete logical request fits in the tag */
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
                                          cip_object_instance_t *instance, client_context_t *client) {
    plc_context_t *plc = client->plc;

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

    /* Look up tag */
    tag_def_t *tag =
        tag_find_by_name(plc->tags, plc->tag_count, path->segments[0].symbolic.name, path->segments[0].symbolic.length);
    if(!tag) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN, "Read Tag: tag '%.*s' not found", (int)path->segments[0].symbolic.length,
              path->segments[0].symbolic.name);
        cip_build_response(response, service, CIP_STATUS_PATH_DEST_UNKNOWN);
        return UTIL_ENOTFOUND;
    }

    pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_DETAIL, "Read Tag: looking up '%.*s' (element_count=%u)",
          (int)path->segments[0].symbolic.length, path->segments[0].symbolic.name, element_count);

    /* Calculate offsets using helper - non-fragmented read starts at offset 0 */
    size_t request_start_byte = 0;
    size_t request_bytes_needed = 0;
    if(!calculate_request_offsets(tag, element_count, 0, &request_start_byte, &request_bytes_needed)) {
        cip_build_response(response, service, CIP_STATUS_INVALID_PARAM);
        return UTIL_EINVAL;
    }

    /* Calculate how much data we can fit in the response */
    size_t available_response_bytes = buf_write_size(response) - 2; /* -2 for tag type */
    size_t elements_that_fit = available_response_bytes / tag->elem_size;
    size_t bytes_to_send = elements_that_fit * tag->elem_size;

    /* Cap to what we actually need to send */
    if(bytes_to_send > request_bytes_needed) { bytes_to_send = request_bytes_needed; }

    /* Build response header */
    cip_build_response(response, service, CIP_STATUS_OK);

    bool ok = true;
    ok &= buf_write_u16_le(response, "tag_type", tag->tag_type);
    ok &= buf_write_bytes(response, "data", (uint8_t *)tag->data + request_start_byte, bytes_to_send);

    if(!ok) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN, "Read Tag: failed to write response");
        return buf_get_error(response);
    }

    pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_DETAIL, "tag '%.*s' type=0x%04X bytes=%zu", (int)path->segments[0].symbolic.length,
          path->segments[0].symbolic.name, tag->tag_type, bytes_to_send);

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
 *   [2-3]  uint16_le  Tag type
 *   [4+]   uint8[]    Tag data
 *
 * Response:
 *   (empty on success)
 */
static util_err_t symbol_service_write_tag(uint8_t service, const cip_path_t *path, buf_t *request, buf_t *response,
                                           cip_object_instance_t *instance, client_context_t *client) {
    plc_context_t *plc = client->plc;

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

    /* Read tag type from request */
    uint16_t tag_type = 0;
    if(!buf_read_u16_le(request, "tag_type", &tag_type)) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN, "Write Tag: failed to read tag type");
        cip_build_response(response, service, CIP_STATUS_INVALID_PARAM);
        return UTIL_EINVAL;
    }

    /* Find tag by name from path */
    if(path->segments[0].type != CIP_SEGMENT_SYMBOLIC) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN, "Write Tag: no symbolic segment in path");
        cip_build_response(response, service, CIP_STATUS_PATH_DEST_UNKNOWN);
        return UTIL_ENOTFOUND;
    }

    /* Look up tag */
    tag_def_t *tag =
        tag_find_by_name(plc->tags, plc->tag_count, path->segments[0].symbolic.name, path->segments[0].symbolic.length);
    if(!tag) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN, "Write Tag: tag '%.*s' not found", (int)path->segments[0].symbolic.length,
              path->segments[0].symbolic.name);
        cip_build_response(response, service, CIP_STATUS_PATH_DEST_UNKNOWN);
        return UTIL_ENOTFOUND;
    }

    pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_DETAIL, "Write Tag: looking up '%.*s' (element_count=%u, type=0x%04X)",
          (int)path->segments[0].symbolic.length, path->segments[0].symbolic.name, element_count, tag_type);

    /* Validate tag type matches */
    if(tag->tag_type != tag_type) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN, "Write Tag: tag type mismatch (expected 0x%04X, got 0x%04X)",
              tag->tag_type, tag_type);
        cip_build_response(response, service, CIP_STATUS_INVALID_PARAM);
        return UTIL_EINVAL;
    }

    /* Calculate offsets using helper - non-fragmented write starts at offset 0 */
    size_t request_start_byte = 0;
    size_t request_bytes_needed = 0;
    if(!calculate_request_offsets(tag, element_count, 0, &request_start_byte, &request_bytes_needed)) {
        cip_build_response(response, service, CIP_STATUS_INVALID_PARAM);
        return UTIL_EINVAL;
    }

    /* Check if request has enough data */
    size_t remaining = buf_read_size(request);
    if(remaining < request_bytes_needed) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN, "Write Tag: insufficient data (%zu needed, %zu available)",
              request_bytes_needed, remaining);
        cip_build_response(response, service, CIP_STATUS_INVALID_PARAM);
        return UTIL_EINVAL;
    }

    /* Write data to tag storage */
    bool ok = true;
    ok &= buf_read_bytes(request, "data", (uint8_t *)tag->data + request_start_byte, request_bytes_needed);

    if(!ok) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN, "Write Tag: failed to read data from request");
        cip_build_response(response, service, CIP_STATUS_INVALID_PARAM);
        return buf_get_error(request);
    }

    /* Build response */
    cip_build_response(response, service, CIP_STATUS_OK);

    pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_DETAIL, "tag '%.*s' type=0x%04X bytes=%zu", (int)path->segments[0].symbolic.length,
          path->segments[0].symbolic.name, tag->tag_type, request_bytes_needed);

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
 *   [0-1]  uint16_le  Element count (total for complete logical request)
 *   [2-5]  uint32_le  Offset (in bytes, progress through current logical read)
 *
 * Response:
 *   [0-1]  uint16_le  Tag type
 *   [2+]   uint8[]    Tag data (partial)
 */
static util_err_t symbol_service_read_tag_fragmented(uint8_t service, const cip_path_t *path, buf_t *request, buf_t *response,
                                                     cip_object_instance_t *instance, client_context_t *client) {
    plc_context_t *plc = client->plc;

    (void)instance;

    /* Parse request: element count and byte offset */
    uint16_t element_count = 0;
    uint32_t fragment_offset = 0;
    if(!buf_read_u16_le(request, "element_count", &element_count)) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN, "failed to read element count");
        cip_build_response(response, service, CIP_STATUS_INVALID_PARAM);
        return UTIL_EINVAL;
    }

    if(!buf_read_u32_le(request, "offset", &fragment_offset)) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN, "failed to read offset");
        cip_build_response(response, service, CIP_STATUS_INVALID_PARAM);
        return UTIL_EINVAL;
    }

    if(element_count == 0) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN, "element count is 0");
        cip_build_response(response, service, CIP_STATUS_INVALID_PARAM);
        return UTIL_EINVAL;
    }

    /* sanity check, this is only supported with symbolic segments */
    if(path->segments[0].type != CIP_SEGMENT_SYMBOLIC) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN, "no symbolic segment in path");
        cip_build_response(response, service, CIP_STATUS_PATH_DEST_UNKNOWN);
        return UTIL_ENOTFOUND;
    }

    /* Look up tag */
    tag_def_t *tag =
        tag_find_by_name(plc->tags, plc->tag_count, path->segments[0].symbolic.name, path->segments[0].symbolic.length);
    if(!tag) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN, "tag '%.*s' not found", (int)path->segments[0].symbolic.length,
              path->segments[0].symbolic.name);
        cip_build_response(response, service, CIP_STATUS_PATH_DEST_UNKNOWN);
        return UTIL_ENOTFOUND;
    }

    pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_DETAIL, "tag '%.*s' (element_count=%u, fragment_offset=%u)",
          (int)path->segments[0].symbolic.length, path->segments[0].symbolic.name, element_count, fragment_offset);

    /* Calculate offsets using helper */
    size_t request_start_byte = 0;
    size_t request_bytes_needed = 0;
    if(!calculate_request_offsets(tag, element_count, fragment_offset, &request_start_byte, &request_bytes_needed)) {
        cip_build_response(response, service, CIP_STATUS_INVALID_PARAM);
        return UTIL_EINVAL;
    }

    pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_DETAIL, "Space in response buffer: %zu bytes", buf_write_size(response));

    /* reserve space for the CIP response header */
    buf_t response_header = {0};
    ;
    if(!buf_reserve_write(response, 4, &response_header)) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN, "failed to reserve response header");
        return buf_get_error(response);
    }

    /* Calculate how much we can send in this response */
    size_t available_response_bytes = buf_write_size(response) - 2; /* -2 for tag type */

    pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_DETAIL,
          "available_response_bytes=%zu, request_bytes_needed=%zu, fragment_offset=%u", available_response_bytes,
          request_bytes_needed, fragment_offset);

    /* How many complete elements fit in the response? */
    size_t elements_that_fit = available_response_bytes / tag->elem_size;
    size_t bytes_to_send = elements_that_fit * tag->elem_size;

    /* Cap to remaining bytes we need to send for this logical request */
    size_t remaining_bytes = request_bytes_needed - fragment_offset;


    // if(bytes_to_send > remaining_bytes) { bytes_to_send = remaining_bytes; }

    pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_DETAIL,
          "available_response_bytes=%zu, elements_that_fit=%zu, bytes_to_send=%zu, remaining_bytes=%zu", available_response_bytes,
          elements_that_fit, bytes_to_send, remaining_bytes);

    /* Determine if this is the last fragment */
    bool is_last_fragment = (fragment_offset + bytes_to_send >= request_bytes_needed);
    uint8_t response_status = is_last_fragment ? CIP_STATUS_OK : CIP_STATUS_PARTIAL_TRANSFER;

    /* Build response header */
    cip_build_response(&response_header, service, response_status);

    bool ok = true;
    ok &= buf_write_u16_le(response, "tag_type", tag->tag_type);
    ok &= buf_write_bytes(response, "data", (uint8_t *)tag->data + request_start_byte, bytes_to_send);

    if(!ok) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN, "failed to write response");
        return buf_get_error(response);
    }

    pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_DETAIL, "tag '%.*s' type=0x%04X bytes=%zu fragment_offset=%u final=%d",
          (int)path->segments[0].symbolic.length, path->segments[0].symbolic.name, tag->tag_type, bytes_to_send, fragment_offset,
          is_last_fragment);

    return UTIL_OK;
}

/* ============================================================================
 * Service: Write Tag Fragmented (0x53)
 * ============================================================================ */

/**
 * Service 0x53: Write Tag Fragmented
 *
 * Allows writing large tags in multiple fragments.
 *
 * Request:
 *   [0-1]  uint16_le  Tag type
 *   [2-3]  uint16_le  Element count (total for complete logical request)
 *   [4-7]  uint32_le  Offset (in bytes, progress through current logical write)
 *   [8+]   uint8[]    Tag data (partial)
 *
 * Response:
 *   (empty on success)
 */
static util_err_t symbol_service_write_tag_fragmented(uint8_t service, const cip_path_t *path, buf_t *request, buf_t *response,
                                                      cip_object_instance_t *instance, client_context_t *client) {
    plc_context_t *plc = client->plc;

    (void)instance;

    /* Parse request: tag type, element count, and byte offset */
    uint16_t tag_type = 0;
    uint16_t element_count = 0;
    uint32_t fragment_offset = 0;

    if(!buf_read_u16_le(request, "tag_type", &tag_type)) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN, "Write Tag Fragmented: failed to read tag type");
        cip_build_response(response, service, CIP_STATUS_INVALID_PARAM);
        return UTIL_EINVAL;
    }

    if(!buf_read_u16_le(request, "element_count", &element_count)) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN, "Write Tag Fragmented: failed to read element count");
        cip_build_response(response, service, CIP_STATUS_INVALID_PARAM);
        return UTIL_EINVAL;
    }

    if(!buf_read_u32_le(request, "offset", &fragment_offset)) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN, "Write Tag Fragmented: failed to read offset");
        cip_build_response(response, service, CIP_STATUS_INVALID_PARAM);
        return UTIL_EINVAL;
    }

    if(element_count == 0) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN, "Write Tag Fragmented: element count is 0");
        cip_build_response(response, service, CIP_STATUS_INVALID_PARAM);
        return UTIL_EINVAL;
    }

    /* sanity check, this is only supported with symbolic segments */
    if(path->segments[0].type != CIP_SEGMENT_SYMBOLIC) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN, "Write Tag Fragmented: no symbolic segment in path");
        cip_build_response(response, service, CIP_STATUS_PATH_DEST_UNKNOWN);
        return UTIL_ENOTFOUND;
    }

    /* Look up tag */
    tag_def_t *tag =
        tag_find_by_name(plc->tags, plc->tag_count, path->segments[0].symbolic.name, path->segments[0].symbolic.length);
    if(!tag) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN, "Write Tag Fragmented: tag '%.*s' not found",
              (int)path->segments[0].symbolic.length, path->segments[0].symbolic.name);
        cip_build_response(response, service, CIP_STATUS_PATH_DEST_UNKNOWN);
        return UTIL_ENOTFOUND;
    }

    pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_DETAIL, "Write Tag Fragmented: tag '%.*s' (element_count=%u, fragment_offset=%u)",
          (int)path->segments[0].symbolic.length, path->segments[0].symbolic.name, element_count, fragment_offset);

    /* Validate tag type matches */
    if(tag->tag_type != tag_type) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN, "Write Tag Fragmented: tag type mismatch (expected 0x%04X, got 0x%04X)",
              tag->tag_type, tag_type);
        cip_build_response(response, service, CIP_STATUS_INVALID_PARAM);
        return UTIL_EINVAL;
    }

    /* Calculate offsets using helper */
    size_t request_start_byte = 0;
    size_t request_bytes_needed = 0;
    if(!calculate_request_offsets(tag, element_count, fragment_offset, &request_start_byte, &request_bytes_needed)) {
        cip_build_response(response, service, CIP_STATUS_INVALID_PARAM);
        return UTIL_EINVAL;
    }

    /* How much data do we have in this fragment? */
    size_t data_available = buf_read_size(request);

    /* How much data can we write in this fragment? */
    size_t remaining_bytes = request_bytes_needed - fragment_offset;
    size_t bytes_to_write = (data_available < remaining_bytes) ? data_available : remaining_bytes;

    /* Validate we're not trying to write past the tag */
    if(fragment_offset + bytes_to_write > request_bytes_needed) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN,
              "Write Tag Fragmented: writing past end of request (offset=%u, bytes=%zu, needed=%zu)", fragment_offset,
              bytes_to_write, request_bytes_needed);
        cip_build_response(response, service, CIP_STATUS_INVALID_PARAM);
        return UTIL_EINVAL;
    }

    /* Write data to tag storage */
    bool ok = true;
    ok &= buf_read_bytes(request, "data", (uint8_t *)tag->data + request_start_byte, bytes_to_write);

    if(!ok) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN, "Write Tag Fragmented: failed to read data from request");
        cip_build_response(response, service, CIP_STATUS_INVALID_PARAM);
        return buf_get_error(request);
    }

    /* Build response */
    cip_build_response(response, service, CIP_STATUS_OK);

    pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_DETAIL, "tag '%.*s' type=0x%04X bytes=%zu fragment_offset=%u",
          (int)path->segments[0].symbolic.length, path->segments[0].symbolic.name, tag->tag_type, bytes_to_write,
          fragment_offset);

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
                                           cip_object_instance_t *instance, client_context_t *client) {
    plc_context_t *plc = client->plc;

    (void)instance;

    pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_DETAIL, "List Tags: starting list_tags service");

    /* Parse pagination from path if present */
    uint32_t start_instance = 0;

    if(path && path->segment_count > 1) {
        if(path->segments[1].type == CIP_SEGMENT_LOGICAL_INSTANCE_8BIT
           || path->segments[1].type == CIP_SEGMENT_LOGICAL_INSTANCE_16BIT
           || path->segments[1].type == CIP_SEGMENT_LOGICAL_INSTANCE_32BIT) {
            start_instance = path->segments[1].logical.id;
            pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_DETAIL, "List Tags: pagination instance = %u", start_instance);
        }
    }
    /* FIXME - this is just plain wrong. The response buffer has already been adjusted for size. */

    /* Get negotiated max packet size */
    uint16_t max_packet_size = client->server_to_client_max_packet;

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

    pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_DETAIL, "List Tags: using packet size=%u, max_tag_data=%zu", max_packet_size,
          max_tag_data);

    /* Find starting position for pagination */
    size_t start_index = 0;
    size_t total_tags = plc->tag_count;

    if(start_instance > 0) {
        /* Find first tag with instance_id >= start_instance */
        for(size_t i = 0; i < plc->tag_count; i++) {
            if(plc->tags[i]->instance_id >= start_instance) {
                start_index = i;
                break;
            }
        }
    }

    pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_DETAIL, "List Tags: found %zu total tags, starting from index %zu (instance %u)",
          total_tags, start_index, start_instance);

    /* Reserve space for CIP response header (reply service + reserved + status) */
    buf_t cip_header_buf;
    if(!buf_reserve_write(response, 4, &cip_header_buf)) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN, "List Tags: failed to reserve CIP header space");
        return buf_get_error(response);
    }

    /* Write tag entries starting from start_index, checking space before each */
    bool ok = true;
    bool more_data = false;
    size_t entries_written = 0;
    size_t tag_data_written = 0;

    for(size_t i = start_index; i < plc->tag_count && ok; i++) {
        tag_def_t *tag = plc->tags[i];
        /* Calculate size of this tag entry */
        size_t name_len = strlen(tag->name);
        size_t entry_size = 4 +       /* instance_id */
                            2 +       /* symbol_type */
                            2 +       /* element_length */
                            12 +      /* array dimensions (3 x 4) */
                            2 +       /* string_length */
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
        if(dim_count_encoded > 3) { dim_count_encoded = 3; /* Cap at 3 bits */ }

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

        if(!ok) { break; }

        /* Update counters for this successful entry */
        tag_data_written += entry_size;
        entries_written++;
    }

    if(!ok) {
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN, "List Tags: failed to write response");
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
        pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_WARN, "List Tags: failed to write CIP header");
        return buf_get_error(&cip_header_buf);
    }

    pdlog(LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_DETAIL, "List Tags: sent %zu entries (%zu bytes), status=0x%02X", entries_written,
          tag_data_written, status_code);

    return UTIL_OK;
}

/* ============================================================================
 * Instance Management
 * ============================================================================ */

static cip_object_instance_t *symbol_get_instance(uint32_t instance_id, client_context_t *client) {
    (void)instance_id;
    (void)client;

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

void logix_symbol_object_register(cip_object_registry_t *registry) {
    cip_object_class_t *cls = (cip_object_class_t *)calloc(1, sizeof(*cls));
    if(!cls) { return; }

    cls->class_id = 0x6B;
    cls->class_name = "Symbol Object";

    /* Register service handlers - supports both ControlLogix and Micro800 */
    cls->service_handlers[CIP_SRV_READ_TAG] = symbol_service_read_tag;                      /* Read Tag */
    cls->service_handlers[CIP_SRV_WRITE_TAG] = symbol_service_write_tag;                    /* Write Tag */
    cls->service_handlers[CIP_SRV_READ_TAG_FRAG_AB] = symbol_service_read_tag_fragmented;   /* Read Tag Fragmented */
    cls->service_handlers[CIP_SRV_WRITE_TAG_FRAG_AB] = symbol_service_write_tag_fragmented; /* Write Tag Fragmented */
    cls->service_handlers[CIP_SRV_INSTANCE_ATTRS_AB] = symbol_service_list_tags;            /* List Tags */

    /* Instance management */
    cls->get_instance = symbol_get_instance;

    /* Register in registry */
    cip_registry_register_class(registry, cls);

    pdlog(
        LOG_MODULE_SYMBOL_OBJECT, LOG_LEVEL_INFO,
        "Registered Symbol Object (Class 0x6B) with Services 0x%02X (Read Tag), 0x%02X (Write Tag), 0x%02X (Read Tag Fragmented), 0x%02X (Write Tag Fragmented), and 0x%02X (List Tags)",
        CIP_SRV_READ_TAG, CIP_SRV_WRITE_TAG, CIP_SRV_READ_TAG_FRAG_AB, CIP_SRV_WRITE_TAG_FRAG_AB, CIP_SRV_INSTANCE_ATTRS_AB);
}
