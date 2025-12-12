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
#include "../utils.h"
#include "../../../utils/log.h"
#include "../mutex.h"
#include <string.h>

/* CIP Error codes */
#define CIP_OK ((uint8_t)0x00)
#define CIP_ERR_EXT_ERR ((uint8_t)0x01)
#define CIP_ERR_INVALID_PARAM ((uint8_t)0x03)
#define CIP_ERR_PATH_SEGMENT ((uint8_t)0x04)
#define CIP_ERR_PATH_DEST_UNKNOWN ((uint8_t)0x05)
#define CIP_ERR_FRAG ((uint8_t)0x06)
#define CIP_ERR_UNSUPPORTED ((uint8_t)0x08)
#define CIP_ERR_INSUFFICIENT_DATA ((uint8_t)0x13)
#define CIP_ERR_TOO_MUCH_DATA ((uint8_t)0x15)
#define CIP_ERR_EXTENDED ((uint8_t)0xff)

#define CIP_ERR_EX_DUPLICATE_CONN ((uint16_t)0x0100)
#define CIP_ERR_EX_INVALID_CONN_SIZE ((uint16_t)0x0109)
#define CIP_ERR_EX_TOO_LONG ((uint16_t)0x2105)

/* CIP protocol constants */
#define CIP_DONE ((uint8_t)0x80)
#define CIP_SYMBOLIC_SEGMENT_MARKER ((uint8_t)0x91)
#define CIP_RESPONSE_HEADER_SIZE ((size_t)4)
#define CIP_TAG_MAX_INDEXES ((uint32_t)3)
#define CIP_MIN_TAG_PATH_SIZE 2
#define CIP_MIN_REQUEST_SIZE 4
#define MAX_SUB_PACKETS ((uint16_t)1000)
#define CIP_MINIMAL_RESPONSE_SIZE ((size_t)6)

/* Dispatcher forward declaration for multi-request */
extern slice_s cip_dispatch_request(slice_s input, slice_s output, plc_s *context);


/* make_cip_pdlog is now implemented in utils.c with logging support */


/**
 * parse_tag_path - Extract tag name and indexes from CIP path
 *
 * Parses a CIP symbolic path to find the tag and any array indexes.
 *
 * Returns: true if tag found, false otherwise
 */
bool parse_tag_path(
    slice_s tag_path,
    plc_s *plc,
    tag_def_s **tag,
    uint32_t *num_indexes,
    uint32_t *indexes
) {
    size_t offset = 0;
    uint8_t name_len = 0;
    uint8_t segment_marker = 0;
    slice_s tag_name_slice = {0};
    uint32_t max_indexes = *num_indexes; /* contains the max possible */

    /* Check if the tag path is long enough to contain the segment marker and name length */
    if(slice_len(tag_path) < CIP_MIN_TAG_PATH_SIZE) {
        pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Tag path is too short to contain segment marker and name length!");
        return false;
    }

    /* Get the segment marker */
    segment_marker = slice_get_uint8(tag_path, offset);
    offset++;
    if(segment_marker != CIP_SYMBOLIC_SEGMENT_MARKER) {
        pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Expected symbolic segment marker but found %x!", segment_marker);
        return false;
    }

    /* Get the name length */
    name_len = slice_get_uint8(tag_path, offset);
    offset++;
    if(name_len + offset > slice_len(tag_path)) {
        pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Name length %d exceeds remaining tag path length %d!", name_len, slice_len(tag_path) - offset);
        return false;
    }

    /* Extract the tag name slice */
    tag_name_slice = slice_from_slice(tag_path, offset, name_len);
    offset += name_len;

    /* Align to 16-bit boundary if necessary */
    if(offset % 2 != 0) { offset++; }

    /* find the tag */
    *tag = plc->tags;

    while(*tag) {
        if(slice_match_string_exact(tag_name_slice, (*tag)->name)) {
            pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Found tag %s", (*tag)->name);
            break;
        }

        (*tag) = (*tag)->next_tag;
    }

    if(!*tag) {
        pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Tag %.*s not found!", slice_len(tag_name_slice), (const char *)(tag_name_slice.data));
        return false;
    }

    /* Initialize the number of indexes to zero */
    *num_indexes = 0;

    /* Parse the numeric segments (indexes) */
    while(offset < slice_len(tag_path)) {
        uint8_t segment_type = slice_get_uint8(tag_path, offset);

        switch(segment_type) {
            case 0x28:        // Single byte value
                offset += 1;  // skip segment type
                indexes[*num_indexes] = (uint32_t)slice_get_uint8(tag_path, offset);
                pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Numeric segment: %u", indexes[*num_indexes]);
                offset += 1;
                break;

            case 0x29:        // Two byte value
                offset += 2;  // skip segment type and padding
                indexes[*num_indexes] = (uint32_t)slice_get_uint16_le(tag_path, offset);
                pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Numeric segment: %u", indexes[*num_indexes]);
                offset += 2;
                break;

            case 0x2A:        // Four byte value
                offset += 2;  // skip segment type and padding
                indexes[*num_indexes] = (uint32_t)slice_get_uint32_le(tag_path, offset);
                pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Numeric segment: %u", indexes[*num_indexes]);
                offset += 4;
                break;

            default:
                pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Unexpected numeric segment marker %x at position %zu!", segment_type, offset);
                return false;
                break;
        }

        (*num_indexes)++;

        if(*num_indexes > max_indexes) {
            pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "More numeric segments, %zu, than expected, %zu!", *num_indexes, max_indexes);
            return false;
        }
    }

    /* the only valid number of indexes is zero or the number of dimensions in the tag. */
    if(*num_indexes != 0 && *num_indexes != (*tag)->num_dimensions) {
        pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Required zero or %zu numeric segments, but only found %zu!", (*tag)->num_dimensions, (size_t)*num_indexes);
        return false;
    }

    return true;
}


/**
 * extract_cip_path - Helper to extract path from CIP request
 *
 * Returns: true if path extracted, false if invalid
 */
bool extract_cip_path(
    slice_s input,
    size_t *offset,
    bool padded,
    slice_s *output
) {
    uint8_t path_len = 0;

    /* Check if the input slice is long enough to contain the path length and path */
    if(slice_len(input) < ((*offset) + (padded ? 2 : 1))) {
        pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "CIP path is too short to contain path length and path!");
        return false;
    }

    /* Get the path length */
    path_len = slice_get_uint8(input, (*offset));
    (*offset)++;
    if(path_len == 0) {
        pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "CIP path length is zero!");
        return false;
    }

    if(padded) { *offset += 1; /* skip the padding byte */ }

    /* Check if the input slice is long enough to contain the path */
    if(slice_len(input) < ((*offset) + (path_len * 2))) {
        pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "CIP path is too short to contain the specified path length %u!", path_len * 2);
        return false;
    }

    /* return the slice with the path data. */
    *output = slice_from_slice(input, *offset, path_len * 2);

    /* update the offset to past the path. */
    *offset += path_len * 2;

    return true;
}


/**
 * calculate_request_start_and_end_offsets - Calculate byte offsets for multi-dimensional array access
 *
 * Handles:
 * - Index boundary validation
 * - Linear offset calculation from multi-dimensional indexes
 * - Tag size validation
 * - Calculation of start and end byte offsets for the requested element range
 *
 * Returns: true if valid, false if out of bounds
 */
bool calculate_request_start_and_end_offsets(
    tag_def_s *tag,
    uint32_t num_indexes,
    uint32_t *indexes,
    uint16_t request_element_count,
    size_t *request_start_byte_offset,
    size_t *request_end_byte_offset
) {
    size_t total_elements = 1;
    size_t tag_size = 0;
    size_t element_offset = 0;

    /* Calculate the total number of elements in the tag */
    total_elements = 1;
    for(uint32_t i = 0; i < tag->num_dimensions; i++) { total_elements *= tag->dimensions[i]; }

    tag_size = total_elements * tag->elem_size;

    /* check index bounds */
    for(size_t index = 0; index < num_indexes; index++) {
        if(indexes[index] >= tag->dimensions[index]) {
            pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Index %zu out of bounds for dimension %zu.", (size_t)indexes[index], index);
            return false;
        }
    }

    /* calculate the linear element offset */
    switch(num_indexes) {
        case 0: element_offset = 0; break;

        case 1: element_offset = indexes[0]; break;

        case 2: element_offset = (indexes[0] * tag->dimensions[1]) + indexes[1]; break;

        case 3:
            element_offset =
                (indexes[0] * tag->dimensions[1] * tag->dimensions[2]) + (indexes[1] * tag->dimensions[2]) + indexes[2];
            break;

        default:
            pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Too many indexes!");
            return false;
            break;
    }

    /* probably not needed, but just in case */
    if(element_offset >= total_elements) {
        pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Element offset %d exceeds total elements %d!", element_offset, total_elements);
        return false;
    }

    /*
     * Calculate the start and end byte offsets.
     * Note that these are absolute for the whole request.
     * Not for the fragment if there is one.
     */
    *request_start_byte_offset = element_offset * tag->elem_size;
    *request_end_byte_offset = *request_start_byte_offset + (request_element_count * tag->elem_size);

    pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Request start byte offset: %d", *request_start_byte_offset);
    pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Request end byte offset: %d", *request_end_byte_offset);

    /* Check if the start offset exceeds the total size of the tag */
    if(*request_start_byte_offset > tag_size) {
        pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Request start byte offset %d exceeds total tag size %zu", (size_t)*request_start_byte_offset, tag_size);
        return false;
    }

    /* Check if the end offset exceeds the total size of the tag */
    if(*request_end_byte_offset > tag_size) {
        pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Request end byte offset %d exceeds total tag size %zu", (size_t)*request_end_byte_offset, tag_size);
        return false;
    }

    return true;
}


/**
 * parse_cip_request - Parse raw CIP request into components
 *
 * Extracts from raw EIP/CIP packet:
 * - Service code
 * - Path (for tag lookup)
 * - Payload (service-specific data)
 *
 * Returns: true if valid, false if parse error
 */
bool parse_cip_request(
    slice_s input,
    uint8_t *cip_service,
    slice_s *cip_service_path,
    slice_s *cip_service_payload
) {
    size_t offset = 0;

    /* Check if the input slice is long enough to contain the service code and path size */
    if(slice_len(input) < CIP_MIN_REQUEST_SIZE) {
        pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "CIP request is too short to contain service code and path size!");
        return false;
    }

    /* Get the service code */
    *cip_service = slice_get_uint8(input, offset);
    offset++;

    /* extract the service path slice */
    if(!extract_cip_path(input, &offset, false, cip_service_path)) {
        pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Unable to extract CIP service path!");
        return false;
    }

    /* Extract the service payload slice */
    *cip_service_payload = slice_from_slice(input, offset, slice_len(input) - offset);

    return true;
}


/**
 * handle_multi_request - Process multi-service CIP request
 *
 * This is completely generic - handles:
 * - Parsing multi-request packet structure (identical for all PLCs)
 * - Looping through sub-requests
 * - Calling cip_dispatch_request for each (which routes to PLC-specific handler)
 * - Packing responses (offset tracking identical for all PLCs)
 * - Handling response overflow
 *
 * The PLC-specific fragmentation is handled per sub-request by the dispatcher.
 *
 * Returns: Response slice
 */
slice_s handle_multi_request(
    uint8_t cip_service,
    slice_s cip_service_path,
    slice_s cip_service_payload,
    slice_s output,
    plc_s *plc
) {
    uint16_t service_count = 0;
    size_t i = 0;

    (void)cip_service_path; /* static for multi-service requests, but we should really check it. */

    size_t output_offset = 0;

    pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Processing Multi-Service request");

    /* Phase 1: Parse request structure */
    if(slice_len(cip_service_payload) < 2) {
        pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Multi-service payload too small for service count");
        return make_cip_pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_ERROR, output, cip_service, CIP_ERR_INSUFFICIENT_DATA, false, 0);
    }

    service_count = slice_get_uint16_le(cip_service_payload, 0);

    pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Multi-service request contains %d services", service_count);

    if(service_count == 0 || service_count > MAX_SUB_PACKETS) {
        pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Invalid service count: %d", service_count);
        return make_cip_pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_ERROR, output, cip_service, CIP_ERR_INVALID_PARAM, false, 0);
    }

    /* Need space for offsets array */
    if(slice_len(cip_service_payload) < (size_t)(2 + service_count * 2)) {
        pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Multi-service payload too small for offset array");
        return make_cip_pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_ERROR, output, cip_service, CIP_ERR_INSUFFICIENT_DATA, false, 0);
    }

    /* calculate the overhead of the multi-response payload */
    size_t multi_response_overhead = 4                                              /* CIP response header size */
                                    + sizeof(uint16_t)                              /* service count */
                                    + (service_count * sizeof(uint16_t));           /* offsets array */

    /* do we have room? Guess using a minimal response size for all requests */
    if(slice_len(output) < (multi_response_overhead + (service_count * CIP_MINIMAL_RESPONSE_SIZE))) {
        pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Output buffer too small for multi-response header");
        return make_cip_pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_ERROR, output, cip_service, CIP_ERR_INSUFFICIENT_DATA, false, 0);
    }

    /* start the offset at the beginning of the first response */
    output_offset = multi_response_overhead;

    /* calculate the offset from the response count word */
    size_t offset_from_response_count =  sizeof(uint16_t)                       /* service count */
                                    + (service_count * sizeof(uint16_t));  /* offsets array */


    /* calculate the maximum slice of the multi-response payload */
    slice_s multi_response_payload = slice_from_slice(output, output_offset, slice_len(output) - output_offset);

    pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Response payload starts at offset %zu", output_offset);
    pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Response payload length: %zu", slice_len(multi_response_payload));

    /* Track if any sub-response has an error status */
    bool any_error = false;

    /* Phase 3: Process each request */
    for(i = 0; i < service_count; i++) {
        /* calculate the slice of the request */
        uint16_t request_offset = slice_get_uint16_le(cip_service_payload, 2 + (i * 2));
        /* Offsets are from the start of the multi-service payload (byte 0) */
        size_t request_start = request_offset;
        size_t next_request_start = (i + 1 < service_count)
            ? slice_get_uint16_le(cip_service_payload, 2 + (i + 1) * 2)
            : slice_len(cip_service_payload);

        if(request_start >= slice_len(cip_service_payload)) {
            pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Request %zu offset %u is out of bounds", i, request_offset);
            return make_cip_pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_ERROR, output, cip_service, CIP_ERR_INVALID_PARAM, false, 0);
        }

        /* Validate that offsets are in ascending order */
        if(request_start >= next_request_start) {
            pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Request %zu has invalid offset range: start=%zu >= next=%zu", i, request_start, next_request_start);
            return make_cip_pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_ERROR, output, cip_service, CIP_ERR_INVALID_PARAM, false, 0);
        }

        /* Extract the individual request - it's a complete CIP request */
        slice_s request = slice_from_slice(cip_service_payload, request_start, next_request_start - request_start);

        /* determine the maximum possible response size for this request */
        slice_s response_output = slice_from_slice(output, output_offset, (slice_len(output) - (output_offset + ((service_count - i) * CIP_MINIMAL_RESPONSE_SIZE))));

        pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Response output slice starts at offset %zu with length %zu", output_offset, slice_len(response_output));

        pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Sub-request %zu:", i);

        /* Process the request - each is a complete CIP request */
        slice_s response = cip_dispatch_request(request, response_output, plc);

        pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Sub-request %zu response:", i);

        /* Check the CIP status byte (byte 2) of the response - if non-zero, there's an error */
        if(!slice_has_err(response) && slice_len(response) > 2) {
            uint8_t cip_status = slice_get_uint8(response, 2);
            if(cip_status != CIP_OK) {
                pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Sub-request %zu returned CIP status error: 0x%02x", i, cip_status);
                any_error = true;
            }
        }

        /* fill in the offset array */
        slice_set_uint16_le(output, 4 + 2 + (i * 2), (uint16_t)offset_from_response_count);

        output_offset += slice_len(response);
        offset_from_response_count += slice_len(response);

        pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Output up to request %zu: offset=%zu", i, output_offset);
    }

    /* Phase 4: Build multi-service response */
    size_t response_offset_pos = 0;

    /* CIP response header (4 bytes) */
    slice_set_uint8(output, response_offset_pos++, cip_service | CIP_DONE); /* 0x8a */
    slice_set_uint8(output, response_offset_pos++, 0); /* reserved */
    /* Status: 0x1E if any sub-request had an error, else CIP_OK (0x00) */
    slice_set_uint8(output, response_offset_pos++, any_error ? 0x1E : CIP_OK);
    slice_set_uint8(output, response_offset_pos++, 0); /* additional status size */

    /* Multi-service response payload starts after CIP header */
    size_t multi_payload_start = response_offset_pos;

    pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Filled in header at offset %zu", response_offset_pos);

    /* Service count */
    slice_set_uint16_le(output, multi_payload_start, service_count);

    pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Multi-service response completed, total size %zu bytes", output_offset);

    return slice_from_slice(output, 0, output_offset);
}


/**
 * cip_read_tag_data - Read data from a tag into an output buffer
 *
 * This is completely generic - handles:
 * - Boundary validation (offset + count within tag)
 * - Mutual exclusion on tag data
 * - Data copy respecting output buffer size
 * - Tracking how much space would be needed for all data
 *
 * The PLC-specific handler calls this with the available output space.
 * If the data doesn't fit, this function returns false so the PLC-specific
 * code can decide how to handle truncation (error for Omron, status 0x06 for ControlLogix).
 *
 * Returns: true if all requested data fit, false if truncated
 */
bool cip_read_tag_data(
    tag_def_s *tag,
    uint32_t offset_bytes,
    uint16_t element_count,
    size_t min_element_size,
    uint8_t *output_buffer,
    size_t output_buffer_size,
    size_t *actual_bytes_read,
    size_t *would_need_bytes
) {
    size_t copy_size = 0;
    size_t remaining_bytes = 0;
    bool all_data_fit = true;

    if(!tag || !output_buffer || !actual_bytes_read || !would_need_bytes) {
        pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Invalid parameters to cip_read_tag_data");
        return false;
    }

    /* Calculate remaining data in tag from offset */
    if(offset_bytes >= (tag->elem_size * tag->dimensions[0] * tag->dimensions[1] * tag->dimensions[2])) {
        pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Offset %u exceeds tag size", offset_bytes);
        return false;
    }

    remaining_bytes = (tag->elem_size * tag->dimensions[0] * tag->dimensions[1] * tag->dimensions[2]) - offset_bytes;

    /* Calculate how much the caller wants to read */
    *would_need_bytes = remaining_bytes;
    if(element_count > 0) {
        size_t requested_bytes = element_count * tag->elem_size;
        if(requested_bytes < remaining_bytes) {
            *would_need_bytes = requested_bytes;
        }
    }

    /* How much can we actually copy? */
    copy_size = output_buffer_size;

    /* Align to minimum element size boundary (don't split atomic values) */
    copy_size = (copy_size / min_element_size) * min_element_size;

    /* Don't copy more than what's available */
    if(copy_size > *would_need_bytes) {
        copy_size = *would_need_bytes;
        all_data_fit = true;
    } else {
        all_data_fit = false;
    }

    /* Copy data with mutual exclusion */
    critical_block(tag->data_mutex) {
        /* Use memcpy directly for raw buffer copy */
        memcpy(output_buffer, tag->data + offset_bytes, copy_size);
    }

    *actual_bytes_read = copy_size;
    return all_data_fit;
}


/**
 * cip_write_tag_data - Write data from buffer into a tag
 *
 * This is completely generic - handles:
 * - Type validation (must match tag type)
 * - Boundary validation (offset + data within tag)
 * - Mutual exclusion on tag data
 * - Data copy
 *
 * Returns: true if successful, false if validation failed
 */
bool cip_write_tag_data(
    tag_def_s *tag,
    uint32_t offset_bytes,
    uint16_t element_type,
    const uint8_t *input_buffer,
    size_t input_buffer_size
) {
    if(!tag || !input_buffer) {
        pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Invalid parameters to cip_write_tag_data");
        return false;
    }

    /* Validate element type matches tag type */
    if(element_type != tag->tag_type) {
        pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Write element type 0x%x does not match tag type 0x%x", element_type, tag->tag_type);
        return false;
    }

    /* Validate we're not writing past the end of the tag */
    size_t tag_total_bytes = tag->elem_size * tag->dimensions[0] * tag->dimensions[1] * tag->dimensions[2];
    if((offset_bytes + input_buffer_size) > tag_total_bytes) {
        pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Write would exceed tag size: offset=%u + size=%zu > tag_size=%zu", offset_bytes, input_buffer_size, tag_total_bytes);
        return false;
    }

    /* Write data with mutual exclusion */
    critical_block(tag->data_mutex) {
        /* Use memcpy directly for raw buffer copy */
        memcpy(tag->data + offset_bytes, input_buffer, input_buffer_size);
    }

    return true;
}
