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

#include "cip_common.h"
#include "../plc.h"
#include "../slice.h"
#include "../../../utils/log.h"
#include <inttypes.h>
#include <limits.h>
#include <string.h>

/* CIP protocol constants */
#define CIP_DONE ((uint8_t)0x80)
#define CIP_OK ((uint8_t)0x00)
#define CIP_ERR_FRAG ((uint8_t)0x06)
#define CIP_ERR_INSUFFICIENT_DATA ((uint8_t)0x13)
#define CIP_ERR_INVALID_PARAM ((uint8_t)0x03)

#define CIP_SRV_READ_NAMED_TAG ((uint8_t)0x4c)
#define CIP_SRV_WRITE_NAMED_TAG ((uint8_t)0x4d)
#define CIP_SRV_READ_NAMED_TAG_FRAG ((uint8_t)0x52)
#define CIP_SRV_WRITE_NAMED_TAG_FRAG ((uint8_t)0x53)
#define CIP_SRV_LIST_TAGS ((uint8_t)0x55)

#define CIP_RESPONSE_HEADER_SIZE ((size_t)4)
#define CIP_RESPONSE_TYPE_INFO_SIZE ((size_t)2)
#define CIP_READ_PAYLOAD_MIN_SIZE ((size_t)2)
#define CIP_READ_FRAG_PAYLOAD_MIN_SIZE ((size_t)6)
#define CIP_WRITE_PAYLOAD_MIN_SIZE ((size_t)5)
#define CIP_WRITE_FRAG_PAYLOAD_MIN_SIZE ((size_t)9)
#define CIP_TAG_MAX_INDEXES ((uint32_t)3)
#define CIP_MIN_ATOMIC_ELEMENT_SIZE ((size_t)8)

/* Forward declarations */
extern slice_s make_cip_pdlog(log_module_t module, log_level_t level, slice_s output, uint8_t cip_cmd, uint8_t cip_err, bool extend, uint16_t extended_error);
extern bool parse_tag_path(slice_s tag_path, plc_s *plc, tag_def_s **tag, uint32_t *num_indexes, uint32_t *indexes);
extern bool calculate_request_start_and_end_offsets(tag_def_s *tag, uint32_t num_indexes, uint32_t *indexes,
                                                    uint16_t request_element_count, size_t *request_start_byte_offset,
                                                    size_t *request_end_byte_offset);
extern bool cip_read_tag_data(tag_def_s *tag, uint32_t offset_bytes, uint16_t element_count, size_t min_element_size,
                              uint8_t *output_buffer, size_t output_buffer_size, size_t *actual_bytes_read,
                              size_t *would_need_bytes);
extern bool cip_write_tag_data(tag_def_s *tag, uint32_t offset_bytes, uint16_t element_type,
                               const uint8_t *input_buffer, size_t input_buffer_size);
extern tag_def_s* find_tag_by_instance_id(plc_s *plc, uint32_t instance_id);


/**
 * logix_handle_read_request - Handle ControlLogix read request
 *
 * ControlLogix uses:
 * - Service 0x4C for normal/first fragment read
 * - Service 0x52 for continuation fragment
 * - Supports status 0x06 for partial responses with more data available
 * - Element count and offset are in payload, not in 0x80 segment
 */
slice_s logix_handle_read_request(uint8_t cip_service, slice_s cip_service_path, slice_s cip_service_payload, slice_s output,
                                  plc_s *plc) {
    tag_def_s *tag = NULL;
    uint32_t num_indexes = CIP_TAG_MAX_INDEXES;
    uint32_t indexes[CIP_TAG_MAX_INDEXES] = {0};
    uint16_t request_element_count = 0;
    uint32_t offset_bytes = 0;
    size_t request_start_byte_offset = 0;
    size_t request_end_byte_offset = 0;
    size_t min_data_element_size = 0;
    slice_s cip_response_header_slice = {0};
    slice_s cip_response_type_info_slice = {0};
    slice_s cip_response_payload_slice = {0};
    size_t actual_bytes_read = 0;
    size_t would_need_bytes = 0;
    bool all_data_fit = false;
    size_t required_request_payload_size = 0;

    pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Processing ControlLogix Read Named Tag request (service 0x%02x)", cip_service);

    /* Determine payload size requirement based on service code */
    if(cip_service == CIP_SRV_READ_NAMED_TAG) {
        required_request_payload_size = CIP_READ_PAYLOAD_MIN_SIZE;
    } else if(cip_service == CIP_SRV_READ_NAMED_TAG_FRAG) {
        required_request_payload_size = CIP_READ_FRAG_PAYLOAD_MIN_SIZE;
    } else {
        pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Invalid ControlLogix read service code: 0x%02x", cip_service);
        return make_cip_pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_ERROR, output, cip_service, CIP_ERR_INVALID_PARAM, false, 0);
    }

    /* Check minimum payload size */
    if(slice_len(cip_service_payload) < required_request_payload_size) {
        pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Insufficient data in the CIP read request payload!");
        return make_cip_pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_ERROR, output, cip_service, CIP_ERR_INSUFFICIENT_DATA, false, 0);
    }

    /* Try to get the tag and indexes from the tag path */
    if(!parse_tag_path(cip_service_path, plc, &tag, &num_indexes, &(indexes[0]))) {
        pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Unable to parse tag path");
        return make_cip_pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_ERROR, output, cip_service, CIP_ERR_INVALID_PARAM, false, 0);
    }

    /* Get the element count from payload */
    request_element_count = slice_get_uint16_le(cip_service_payload, 0);

    /* Get the optional byte offset for fragmented reads */
    if(cip_service == CIP_SRV_READ_NAMED_TAG_FRAG) {
        offset_bytes = slice_get_uint32_le(cip_service_payload, 2);
    }

    /* Calculate the starting offset and validate */
    if(!calculate_request_start_and_end_offsets(tag, num_indexes, indexes, request_element_count,
                                                &request_start_byte_offset, &request_end_byte_offset)) {
        pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Unable to calculate the starting offset of the read request!");
        return make_cip_pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_ERROR, output, cip_service, CIP_ERR_INVALID_PARAM, false, 0);
    }

    /* Adjust for fragment offset */
    request_start_byte_offset += offset_bytes;

    /* Determine minimum atomic element size */
    if(tag->elem_size > CIP_MIN_ATOMIC_ELEMENT_SIZE) {
        min_data_element_size = CIP_MIN_ATOMIC_ELEMENT_SIZE;
    } else {
        min_data_element_size = tag->elem_size;
    }

    /* Check we have room for response header and type info */
    if(slice_len(output) < (CIP_RESPONSE_HEADER_SIZE + CIP_RESPONSE_TYPE_INFO_SIZE + min_data_element_size)) {
        pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Insufficient space in the output buffer for the response!");
        return make_cip_pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_ERROR, output, cip_service, CIP_ERR_FRAG, false, 0);
    }

    /* Extract slices for response components */
    cip_response_header_slice = slice_from_slice(output, 0, CIP_RESPONSE_HEADER_SIZE);
    cip_response_type_info_slice = slice_from_slice(output, CIP_RESPONSE_HEADER_SIZE, CIP_RESPONSE_TYPE_INFO_SIZE);
    cip_response_payload_slice = slice_from_slice(output, CIP_RESPONSE_HEADER_SIZE + CIP_RESPONSE_TYPE_INFO_SIZE,
                                                  slice_len(output) - (CIP_RESPONSE_HEADER_SIZE + CIP_RESPONSE_TYPE_INFO_SIZE));

    /* Read the tag data */
    all_data_fit = cip_read_tag_data(tag, (uint32_t)request_start_byte_offset,
                                     (uint16_t)(request_element_count),
                                     min_data_element_size,
                                     cip_response_payload_slice.data,
                                     slice_len(cip_response_payload_slice),
                                     &actual_bytes_read,
                                     &would_need_bytes);

    /* Fill in the CIP response header */
    slice_set_uint8(cip_response_header_slice, 0, cip_service | CIP_DONE);
    slice_set_uint8(cip_response_header_slice, 1, 0); /* reserved */
    /* Status: 0x06 if partial, 0x00 if complete */
    slice_set_uint8(cip_response_header_slice, 2, all_data_fit ? CIP_OK : CIP_ERR_FRAG);
    slice_set_uint8(cip_response_header_slice, 3, 0); /* no extended error */

    /* Fill in the tag data type */
    slice_set_uint16_le(cip_response_type_info_slice, 0, tag->tag_type);

    /* Return the response */
    return slice_from_slice(output, 0, CIP_RESPONSE_HEADER_SIZE + CIP_RESPONSE_TYPE_INFO_SIZE + actual_bytes_read);
}


/**
 * logix_handle_write_request - Handle ControlLogix write request
 *
 * Similar to read but supports fragmentation and writes data
 */
slice_s logix_handle_write_request(uint8_t cip_service, slice_s cip_service_path, slice_s cip_service_payload, slice_s output,
                                   plc_s *plc) {
    tag_def_s *tag = NULL;
    uint32_t num_indexes = CIP_TAG_MAX_INDEXES;
    uint32_t indexes[CIP_TAG_MAX_INDEXES] = {0};
    uint16_t request_element_type = 0;
    uint16_t request_element_count = 0;
    uint32_t offset_bytes = 0;
    size_t request_start_byte_offset = 0;
    size_t request_end_byte_offset = 0;
    slice_s cip_response_header_slice = {0};
    slice_s write_request_payload_slice = {0};
    size_t required_request_payload_size = 0;

    pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Processing ControlLogix Write Named Tag request (service 0x%02x)", cip_service);

    /* Determine payload size requirement based on service code */
    if(cip_service == CIP_SRV_WRITE_NAMED_TAG) {
        required_request_payload_size = CIP_WRITE_PAYLOAD_MIN_SIZE;
    } else if(cip_service == CIP_SRV_WRITE_NAMED_TAG_FRAG) {
        required_request_payload_size = CIP_WRITE_FRAG_PAYLOAD_MIN_SIZE;
    } else {
        pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Invalid ControlLogix write service code: 0x%02x", cip_service);
        return make_cip_pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_ERROR, output, cip_service, CIP_ERR_INVALID_PARAM, false, 0);
    }

    /* Check minimum payload size */
    if(slice_len(cip_service_payload) < required_request_payload_size) {
        pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Insufficient data in the CIP write request payload!");
        return make_cip_pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_ERROR, output, cip_service, CIP_ERR_INSUFFICIENT_DATA, false, 0);
    }

    /* Try to get the tag and indexes from the tag path */
    if(!parse_tag_path(cip_service_path, plc, &tag, &num_indexes, &(indexes[0]))) {
        pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Unable to parse tag path");
        return make_cip_pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_ERROR, output, cip_service, CIP_ERR_INVALID_PARAM, false, 0);
    }

    /* Get the element type from payload */
    request_element_type = slice_get_uint16_le(cip_service_payload, 0);

    /* Get the element count */
    request_element_count = slice_get_uint16_le(cip_service_payload, 2);

    /* Get the optional byte offset for fragmented writes */
    size_t payload_offset = 4;
    if(cip_service == CIP_SRV_WRITE_NAMED_TAG_FRAG) {
        offset_bytes = slice_get_uint32_le(cip_service_payload, 4);
        payload_offset = 8;
    }

    /* Extract write payload data */
    write_request_payload_slice = slice_from_slice(cip_service_payload, payload_offset,
                                                   slice_len(cip_service_payload) - payload_offset);

    /* Calculate the starting offset and validate */
    if(!calculate_request_start_and_end_offsets(tag, num_indexes, indexes, request_element_count,
                                                &request_start_byte_offset, &request_end_byte_offset)) {
        pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Unable to calculate the starting offset of the write request!");
        return make_cip_pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_ERROR, output, cip_service, CIP_ERR_INVALID_PARAM, false, 0);
    }

    /* Adjust for fragment offset */
    request_start_byte_offset += offset_bytes;

    /* Write the tag data */
    if(!cip_write_tag_data(tag, (uint32_t)request_start_byte_offset, request_element_type,
                           write_request_payload_slice.data, slice_len(write_request_payload_slice))) {
        pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Unable to write data to tag");
        return make_cip_pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_ERROR, output, cip_service, CIP_ERR_INVALID_PARAM, false, 0);
    }

    /* Fill in the CIP response header */
    cip_response_header_slice = slice_from_slice(output, 0, CIP_RESPONSE_HEADER_SIZE);
    slice_set_uint8(cip_response_header_slice, 0, cip_service | CIP_DONE);
    slice_set_uint8(cip_response_header_slice, 1, 0); /* reserved */
    slice_set_uint8(cip_response_header_slice, 2, CIP_OK); /* status */
    slice_set_uint8(cip_response_header_slice, 3, 0); /* no extended error */

    /* Return the response header only */
    return cip_response_header_slice;
}


/**
 * logix_handle_list_tags - Handle ControlLogix tag enumeration (Service 0x55)
 *
 * ControlLogix uses Service 0x55 to enumerate tags with attributes.
 * When the client sends instance 0, the PLC returns all tags >= 0 that fit in the packet.
 * When the client sends instance N, the PLC returns all tags >= N that fit in the packet.
 * The client then requests (last_returned_id + 1) to get the next batch.
 *
 * Response includes multiple tag entries, each with:
 * - instance_id (4 bytes)
 * - symbol_type (2 bytes)
 * - element_length (2 bytes)
 * - array_dims[3] (3x4 = 12 bytes)
 * - string_len (2 bytes)
 * - string_name (N bytes, padded to word alignment)
 *
 * Status 0x06 indicates more data available, 0x00 indicates this is the last batch.
 */
slice_s logix_handle_list_tags(uint8_t cip_service, slice_s cip_service_path, slice_s cip_service_payload, slice_s output,
                               plc_s *plc) {
    (void)cip_service_payload;  /* Not used in List Tags request */
    size_t offset = 0;
    uint32_t start_instance_id = 0;
    tag_def_s *tag = NULL;
    size_t name_len = 0;
    size_t name_padding = 0;
    uint8_t response_status = CIP_OK;
    size_t payload_space_remaining = 0;
    tag_def_s *last_tag_in_response = NULL;

    pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Processing ControlLogix List Tags request (Service 0x55)");

    /* Parse the CIP path to extract instance ID
     * Path format: 0x20 (get class), 0x6B (symbol class), 0x25 (16-bit instance), 0x00 (padding),
     *            instance byte 0, instance byte 1
     */

    if(slice_len(cip_service_path) < 6) {
        pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Path too short for List Tags request");
        return make_cip_pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_ERROR, output, cip_service, CIP_ERR_INVALID_PARAM, false, 0);
    }

    /* Verify path structure */
    if(slice_get_uint8(cip_service_path, 0) != 0x20) {
        pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Invalid class selector in List Tags path");
        return make_cip_pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_ERROR, output, cip_service, CIP_ERR_INVALID_PARAM, false, 0);
    }

    if(slice_get_uint8(cip_service_path, 1) != 0x6B) {
        pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Invalid class ID in List Tags path (expected 0x6B)");
        return make_cip_pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_ERROR, output, cip_service, CIP_ERR_INVALID_PARAM, false, 0);
    }

    if(slice_get_uint8(cip_service_path, 2) != 0x25) {
        pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Invalid instance type in List Tags path (expected 0x25)");
        return make_cip_pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_ERROR, output, cip_service, CIP_ERR_INVALID_PARAM, false, 0);
    }

    /* Extract start instance ID (16-bit little-endian) from path */
    start_instance_id = slice_get_uint16_le(cip_service_path, 4);
    pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "List Tags request starting from instance ID: %u", start_instance_id);

    /* Build response header */
    offset = 0;
    if(offset + 4 > slice_len(output)) {
        return make_cip_pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_ERROR, output, cip_service, CIP_ERR_INSUFFICIENT_DATA, false, 0);
    }

    slice_set_uint8(output, offset, cip_service | CIP_DONE);  /* Reply service */
    offset++;
    slice_set_uint8(output, offset, 0);  /* Reserved */
    offset++;
    size_t status_offset = offset;  /* Save offset to update status later */
    slice_set_uint8(output, offset, CIP_OK);  /* Status - will update if more data */
    offset++;
    slice_set_uint8(output, offset, 0);  /* Extended status size */
    offset++;

    /* Now pack tag entries that fit in the remaining space */
    payload_space_remaining = slice_len(output) - offset;

    /* Pack tags in increasing instance ID order */
    uint32_t tags_returned = 0;
    uint32_t current_search_id = start_instance_id;

    while(payload_space_remaining > 0) {
        /* Find the tag with the smallest instance_id >= current_search_id */
        tag_def_s *next_tag_to_add = NULL;
        uint32_t min_id_found = UINT32_MAX;

        tag = plc->tags;
        while(tag) {
            if(tag->instance_id >= current_search_id && tag->instance_id < min_id_found) {
                min_id_found = tag->instance_id;
                next_tag_to_add = tag;
            }
            tag = tag->next_tag;
        }

        /* If no more tags found, we're done */
        if(!next_tag_to_add) {
            break;
        }

        tag = next_tag_to_add;

        /* Calculate space needed for this tag entry */
        name_len = strlen(tag->name);
        name_padding = (name_len % 2 == 1) ? 1 : 0;

        /* Space needed: instance(4) + type(2) + elem_size(2) + dims(12) + strlen(2) + name + padding */
        size_t entry_size = 4 + 2 + 2 + 12 + 2 + name_len + name_padding;

        /* Check if this entry fits */
        if(entry_size > payload_space_remaining) {
            /* This entry doesn't fit, so we're done with this batch */
            response_status = CIP_ERR_FRAG;  /* 0x06 = more data available */
            break;
        }

        /* Instance ID */
        slice_set_uint32_le(output, offset, (uint32_t)tag->instance_id);
        offset += 4;
        payload_space_remaining -= 4;

        /* Symbol type (tag CIP type) */
        slice_set_uint16_le(output, offset, (uint16_t)tag->tag_type);
        offset += 2;
        payload_space_remaining -= 2;

        /* Element length in bytes */
        slice_set_uint16_le(output, offset, (uint16_t)tag->elem_size);
        offset += 2;
        payload_space_remaining -= 2;

        /* Array dimensions (3 x uint32) */
        for(size_t i = 0; i < 3; i++) {
            uint32_t dim = (i < tag->num_dimensions) ? (uint32_t)tag->dimensions[i] : 0;
            slice_set_uint32_le(output, offset, dim);
            offset += 4;
            payload_space_remaining -= 4;
        }

        /* String length */
        slice_set_uint16_le(output, offset, (uint16_t)name_len);
        offset += 2;
        payload_space_remaining -= 2;

        /* Tag name string */
        uint8_t *name_ptr = slice_get_bytes(output, offset);
        if(name_ptr) {
            memcpy(name_ptr, tag->name, name_len);
        }
        offset += name_len;
        payload_space_remaining -= name_len;

        /* Padding if name is odd length */
        if(name_padding) {
            slice_set_uint8(output, offset, 0);
            offset++;
            payload_space_remaining -= 1;
        }

        last_tag_in_response = tag;
        tags_returned++;
        pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "  Added tag to response: instance %u, name %s", tag->instance_id, tag->name);

        /* Set next search ID to be one more than the ID we just added */
        current_search_id = tag->instance_id + 1;
    }

    /* Update response status */
    slice_set_uint8(output, status_offset, response_status);

    if(last_tag_in_response) {
        pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "List Tags response: Returned %u tag(s), last instance ID: %u, MoreData: %s",
                 tags_returned, last_tag_in_response->instance_id,
                 (response_status == CIP_ERR_FRAG) ? "yes" : "no");
    } else {
        pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "List Tags response: No tags found matching instance ID >= %u", start_instance_id);
    }

    return slice_from_slice(output, 0, offset);
}
