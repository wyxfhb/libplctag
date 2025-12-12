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

/* CIP protocol constants */
#define CIP_DONE ((uint8_t)0x80)
#define CIP_OK ((uint8_t)0x00)
#define CIP_ERR_FRAG ((uint8_t)0x06)
#define CIP_ERR_INSUFFICIENT_DATA ((uint8_t)0x13)
#define CIP_ERR_TOO_MUCH_DATA ((uint8_t)0x15)
#define CIP_ERR_UNSUPPORTED ((uint8_t)0x08)
#define CIP_ERR_INVALID_PARAM ((uint8_t)0x03)

#define CIP_SRV_READ_NAMED_TAG ((uint8_t)0x4c)
#define CIP_SRV_WRITE_NAMED_TAG ((uint8_t)0x4d)
#define CIP_SRV_READ_NAMED_TAG_FRAG ((uint8_t)0x52)
#define CIP_SRV_WRITE_NAMED_TAG_FRAG ((uint8_t)0x53)

#define CIP_RESPONSE_HEADER_SIZE ((size_t)4)
#define CIP_RESPONSE_TYPE_INFO_SIZE ((size_t)2)
#define CIP_READ_PAYLOAD_MIN_SIZE ((size_t)2)
#define CIP_WRITE_PAYLOAD_MIN_SIZE ((size_t)5)
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


/**
 * omron_handle_read_request - Handle Omron read request
 *
 * Omron uses:
 * - Service 0x4C (not 0x52 fragmented variant)
 * - 0x80 segment in payload for offset and element count
 * - Returns error if data doesn't fit (no partial responses)
 */
slice_s omron_handle_read_request(uint8_t cip_service, slice_s cip_service_path, slice_s cip_service_payload, slice_s output,
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

    pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Processing Omron Read Named Tag request.");

    /* Omron only supports un-fragmented reads */
    if(cip_service != CIP_SRV_READ_NAMED_TAG) {
        pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Omron PLCs do not support fragmented read CIP service!");
        return make_cip_pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_ERROR, output, cip_service, CIP_ERR_UNSUPPORTED, false, 0);
    }

    /* Check minimum payload size */
    if(slice_len(cip_service_payload) < CIP_READ_PAYLOAD_MIN_SIZE) {
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

    /* Check for 0x80 segment (offset) in payload - Omron specific */
    if(slice_len(cip_service_payload) >= 6 && slice_get_uint8(cip_service_payload, 2) == 0x80) {
        offset_bytes = slice_get_uint32_le(cip_service_payload, 3);
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

    /* Check we have room for response header */
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
    if(!cip_read_tag_data(tag, (uint32_t)request_start_byte_offset,
                          (uint16_t)(request_element_count),
                          min_data_element_size,
                          cip_response_payload_slice.data,
                          slice_len(cip_response_payload_slice),
                          &actual_bytes_read,
                          &would_need_bytes)) {
        /* Data doesn't fit - Omron returns error */
        pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Omron: Read data truncated, returning error");
        return make_cip_pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_ERROR, output, cip_service, CIP_ERR_TOO_MUCH_DATA, false, 0);
    }

    /* Fill in the CIP response header */
    slice_set_uint8(cip_response_header_slice, 0, cip_service | CIP_DONE);
    slice_set_uint8(cip_response_header_slice, 1, 0); /* reserved */
    slice_set_uint8(cip_response_header_slice, 2, CIP_OK); /* status */
    slice_set_uint8(cip_response_header_slice, 3, 0); /* no extended error */

    /* Fill in the tag data type */
    slice_set_uint16_le(cip_response_type_info_slice, 0, tag->tag_type);

    /* Return the response */
    return slice_from_slice(output, 0, CIP_RESPONSE_HEADER_SIZE + CIP_RESPONSE_TYPE_INFO_SIZE + actual_bytes_read);
}


/**
 * omron_handle_write_request - Handle Omron write request
 *
 * Similar to read but validates element type and writes data
 */
slice_s omron_handle_write_request(uint8_t cip_service, slice_s cip_service_path, slice_s cip_service_payload, slice_s output,
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

    pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Processing Omron Write Named Tag request.");

    /* Omron only supports un-fragmented writes */
    if(cip_service != CIP_SRV_WRITE_NAMED_TAG) {
        pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Omron PLCs do not support fragmented write CIP service!");
        return make_cip_pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_ERROR, output, cip_service, CIP_ERR_UNSUPPORTED, false, 0);
    }

    /* Check minimum payload size */
    if(slice_len(cip_service_payload) < CIP_WRITE_PAYLOAD_MIN_SIZE) {
        pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Insufficient data in the CIP write request payload!");
        return make_cip_pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_ERROR, output, cip_service, CIP_ERR_INSUFFICIENT_DATA, false, 0);
    }

    /* Try to get the tag and indexes from the tag path */
    if(!parse_tag_path(cip_service_path, plc, &tag, &num_indexes, &(indexes[0]))) {
        pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Unable to parse tag path");
        return make_cip_pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_ERROR, output, cip_service, CIP_ERR_INVALID_PARAM, false, 0);
    }

    /* Get the element type and count from payload */
    request_element_type = slice_get_uint16_le(cip_service_payload, 0);
    request_element_count = slice_get_uint16_le(cip_service_payload, 2);

    /* Check for 0x80 segment (offset) in payload - Omron specific */
    if(slice_len(cip_service_payload) >= 7 && slice_get_uint8(cip_service_payload, 4) == 0x80) {
        offset_bytes = slice_get_uint32_le(cip_service_payload, 5);
    }

    /* Extract write payload data */
    write_request_payload_slice = slice_from_slice(cip_service_payload, 4,
                                                   slice_len(cip_service_payload) - 4);

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
