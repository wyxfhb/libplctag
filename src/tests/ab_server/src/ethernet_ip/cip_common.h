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

#pragma once

#include "../plc.h"
#include "../slice.h"

/**
 * Generic CIP operations - identical for all PLC types
 * These functions handle the core data movement and lookup logic.
 * PLC-specific fragmentation is handled by cip_omron.c, cip_logix.c, etc.
 */

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
 * Arguments:
 *   tag                 - Tag to read from (already looked up)
 *   offset_bytes        - Byte offset into tag data
 *   element_count       - Number of elements to read (0 = all remaining)
 *   min_element_size    - Minimum atomic element size (for truncation decisions)
 *   output_buffer       - Buffer to copy data into
 *   output_buffer_size  - Maximum size available in output_buffer
 *   actual_bytes_read   - OUT: How many bytes were actually copied
 *   would_need_bytes    - OUT: Total bytes that would be needed for all data
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
);

/**
 * cip_write_tag_data - Write data from buffer into a tag
 *
 * This is completely generic - handles:
 * - Type validation (must match tag type)
 * - Boundary validation (offset + data within tag)
 * - Mutual exclusion on tag data
 * - Data copy
 *
 * Arguments:
 *   tag                 - Tag to write to (already looked up)
 *   offset_bytes        - Byte offset into tag data
 *   element_type        - CIP type code from request (must match tag->tag_type)
 *   input_buffer        - Data to write
 *   input_buffer_size   - Number of bytes to write
 *
 * Returns: true if successful, false if validation failed
 */
bool cip_write_tag_data(
    tag_def_s *tag,
    uint32_t offset_bytes,
    uint16_t element_type,
    const uint8_t *input_buffer,
    size_t input_buffer_size
);

/**
 * parse_tag_path - Parse CIP tag path to find tag
 *
 * Extracts:
 * - Tag name from symbolic segment (0x91 <len> <name>)
 * - Array indexes from numeric segments (0x28, 0x29, 0x2A)
 * - Looks up tag by name in PLC tag list
 *
 * Returns: true if tag found, false otherwise
 */
bool parse_tag_path(
    slice_s tag_path,
    plc_s *plc,
    tag_def_s **tag,
    uint32_t *num_indexes,
    uint32_t *indexes
);

/**
 * calculate_request_start_and_end_offsets - Calculate byte offsets for multi-dim arrays
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
);

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
);

/**
 * make_cip_log_error - Encode CIP error response
 *
 * Identical for all PLCs - creates standard CIP error format.
 *
 * Returns: Response slice with error encoded
 */
slice_s make_cip_log_error(
    slice_s output,
    uint8_t cip_cmd,
    uint8_t cip_err,
    bool extend,
    uint16_t extended_error
);

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
);

/**
 * extract_cip_path - Helper to extract path from request
 *
 * Returns: true if path extracted, false if invalid
 */
bool extract_cip_path(
    slice_s input,
    size_t *offset,
    bool padded,
    slice_s *output
);
