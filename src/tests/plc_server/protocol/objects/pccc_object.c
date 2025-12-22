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
#include "pccc_object.h"
#include "../cip_defs.h"
#include "../cip_message_router.h"
#include "../../plc_context.h"
#include "../../tag_storage.h"
#include "../../../utils/log.h"
#include "../../../utils/buf.h"

/* PCCC Protocol Constants */
#define PCCC_PREFIX_BYTE0 0x0F
#define PCCC_PREFIX_BYTE1 0x00

#define PCCC_CMD_PLC5_READ 0x01
#define PCCC_CMD_PLC5_WRITE 0x00
#define PCCC_CMD_PLC5_RMW 0x26

#define PCCC_CMD_SLC_READ 0xA2
#define PCCC_CMD_SLC_WRITE 0xAA
#define PCCC_CMD_SLC_RMW 0xAB

/* PCCC Response Codes */
#define PCCC_RESP_CODE 0x4F
#define PCCC_ERR_FLAGS 0xF0

/* PCCC Error Codes */
#define PCCC_ERR_ADDR_NOT_USABLE 0x06
#define PCCC_ERR_FILE_WRONG_SIZE 0x07
#define PCCC_ERR_UNSUPPORTED_CMD 0x0E

/* Forward declarations */
static util_err_t pccc_execute_service(uint8_t service, const cip_path_t *path, buf_t *request, buf_t *response,
                                       cip_object_instance_t *instance, client_context_t *client);

/* ============================================================================
 * PCCC Error Response Building
 * ============================================================================ */

/**
 * Build a PCCC error response
 *
 * Response format:
 *   [0]     uint8   0x4F (success status indicator)
 *   [1]     uint8   0xF0 (error flags)
 *   [2-3]   uint16  Sequence ID (little-endian)
 *   [4]     uint8   Error code
 */
static util_err_t pccc_build_error_response(buf_t *response, uint16_t sequence_id, uint8_t error_code) {
    bool ok = true;

    ok &= buf_write_u8(response, "pccc_status", PCCC_RESP_CODE);
    ok &= buf_write_u8(response, "pccc_error_flags", PCCC_ERR_FLAGS);
    ok &= buf_write_u16_le(response, "pccc_seq_id", sequence_id);
    ok &= buf_write_u8(response, "pccc_error_code", error_code);

    if(!ok) {
        pdlog(LOG_MODULE_PCCC_OBJECT, LOG_LEVEL_WARN, "PCCC: failed to build error response");
        return buf_get_error(response);
    }

    return UTIL_OK;
}

/**
 * Build a successful PCCC response header
 *
 * Response format:
 *   [0]     uint8   0x4F (success status indicator)
 *   [1]     uint8   0x00 (no error)
 *   [2-3]   uint16  Sequence ID (little-endian)
 */
static util_err_t pccc_build_response_header(buf_t *response, uint16_t sequence_id) {
    bool ok = true;

    ok &= buf_write_u8(response, "pccc_status", PCCC_RESP_CODE);
    ok &= buf_write_u8(response, "pccc_error_code", 0x00);
    ok &= buf_write_u16_le(response, "pccc_seq_id", sequence_id);

    if(!ok) {
        pdlog(LOG_MODULE_PCCC_OBJECT, LOG_LEVEL_WARN, "PCCC: failed to build response header");
        return buf_get_error(response);
    }

    return UTIL_OK;
}

/* ============================================================================
 * PCCC PLC5 Command Handlers
 * ============================================================================ */

/**
 * PLC5 Read Command (0x01)
 *
 * Request format:
 *   [0]     uint8   0x01 (command)
 *   [1-2]   uint16  Offset within element (LE)
 *   [3-4]   uint16  Transfer size in elements (LE)
 *   [5]     uint8   File prefix (0x06)
 *   [6]     uint8   File number
 *   [7]     uint8   Element number
 *
 * Response format (appended to header):
 *   [0+]    uint8[] Data bytes
 */
static util_err_t pccc_handle_plc5_read(buf_t *request, buf_t *response, plc_context_t *plc, uint16_t sequence_id) {
    uint16_t offset = 0;
    uint16_t transfer_size = 0;
    uint8_t file_prefix = 0;
    uint8_t file_num = 0;
    uint8_t element_num = 0;
    size_t start_byte_offset = 0;
    size_t end_byte_offset = 0;
    size_t tag_size = 0;
    bool ok = true;

    /* Parse request */
    ok &= buf_read_u16_le(request, "offset", &offset);
    ok &= buf_read_u16_le(request, "transfer_size", &transfer_size);
    ok &= buf_read_u8(request, "file_prefix", &file_prefix);
    ok &= buf_read_u8(request, "file_num", &file_num);
    ok &= buf_read_u8(request, "element_num", &element_num);

    if(!ok || file_prefix != 0x06) {
        pdlog(LOG_MODULE_PCCC_OBJECT, LOG_LEVEL_WARN, "PLC5 Read: invalid request format or file prefix");
        return pccc_build_error_response(response, sequence_id, PCCC_ERR_ADDR_NOT_USABLE);
    }

    /* Find tag by data file number */
    tag_def_t *tag = tag_find_by_file_num(plc->tags, plc->tag_count, file_num);
    if(!tag) {
        pdlog(LOG_MODULE_PCCC_OBJECT, LOG_LEVEL_WARN, "PLC5 Read: unable to find tag with data file %u", file_num);
        return pccc_build_error_response(response, sequence_id, PCCC_ERR_ADDR_NOT_USABLE);
    }

    /* Calculate byte offsets */
    tag_size = tag->elem_count * tag->elem_size;
    start_byte_offset = offset + (element_num * tag->elem_size);
    end_byte_offset = start_byte_offset + (transfer_size * tag->elem_size);

    pdlog(LOG_MODULE_PCCC_OBJECT, LOG_LEVEL_DETAIL, "PLC5 Read: file=%u elem=%u offset=%u size=%u bytes -> [%zu..%zu of %zu]",
          file_num, element_num, offset, (transfer_size * tag->elem_size), start_byte_offset, end_byte_offset, tag_size);

    /* Bounds check */
    if(start_byte_offset >= tag_size) {
        pdlog(LOG_MODULE_PCCC_OBJECT, LOG_LEVEL_WARN, "PLC5 Read: start offset %zu >= tag size %zu", start_byte_offset, tag_size);
        return pccc_build_error_response(response, sequence_id, PCCC_ERR_FILE_WRONG_SIZE);
    }

    if(end_byte_offset > tag_size) {
        pdlog(LOG_MODULE_PCCC_OBJECT, LOG_LEVEL_WARN, "PLC5 Read: end offset %zu > tag size %zu", end_byte_offset, tag_size);
        return pccc_build_error_response(response, sequence_id, PCCC_ERR_FILE_WRONG_SIZE);
    }

    if((end_byte_offset - start_byte_offset) > 240) {
        pdlog(LOG_MODULE_PCCC_OBJECT, LOG_LEVEL_WARN, "PLC5 Read: request too large (%zu bytes)",
              end_byte_offset - start_byte_offset);
        return pccc_build_error_response(response, sequence_id, PCCC_ERR_FILE_WRONG_SIZE);
    }

    /* Build response header */
    util_err_t err = pccc_build_response_header(response, sequence_id);
    if(err != UTIL_OK) { return err; }

    /* Copy data */
    for(size_t i = 0; i < (transfer_size * tag->elem_size); i++) {
        ok &= buf_write_u8(response, "data", tag->data[start_byte_offset + i]);
    }

    if(!ok) {
        pdlog(LOG_MODULE_PCCC_OBJECT, LOG_LEVEL_WARN, "PLC5 Read: failed to write response data");
        return buf_get_error(response);
    }

    pdlog(LOG_MODULE_PCCC_OBJECT, LOG_LEVEL_DETAIL, "PLC5 Read: success");
    return UTIL_OK;
}

/**
 * PLC5 Write Command (0x00)
 *
 * Request format:
 *   [0]     uint8   0x00 (command)
 *   [1-2]   uint16  Offset within element (LE)
 *   [3-4]   uint16  Transfer size in elements (LE)
 *   [5]     uint8   File prefix (0x06)
 *   [6]     uint8   File number
 *   [7]     uint8   Element number
 *   [8+]    uint8[] Data bytes
 *
 * Response format:
 *   (header only, 4 bytes)
 */
static util_err_t pccc_handle_plc5_write(buf_t *request, buf_t *response, plc_context_t *plc, uint16_t sequence_id) {
    uint16_t offset = 0;
    uint16_t transfer_size = 0;
    uint8_t file_prefix = 0;
    uint8_t file_num = 0;
    uint8_t element_num = 0;
    size_t start_byte_offset = 0;
    size_t end_byte_offset = 0;
    size_t tag_size = 0;
    bool ok = true;

    /* Parse request */
    ok &= buf_read_u16_le(request, "offset", &offset);
    ok &= buf_read_u16_le(request, "transfer_size", &transfer_size);
    ok &= buf_read_u8(request, "file_prefix", &file_prefix);
    ok &= buf_read_u8(request, "file_num", &file_num);
    ok &= buf_read_u8(request, "element_num", &element_num);

    if(!ok || file_prefix != 0x06) {
        pdlog(LOG_MODULE_PCCC_OBJECT, LOG_LEVEL_WARN, "PLC5 Write: invalid request format or file prefix");
        return pccc_build_error_response(response, sequence_id, PCCC_ERR_ADDR_NOT_USABLE);
    }

    /* Find tag */
    tag_def_t *tag = tag_find_by_file_num(plc->tags, plc->tag_count, file_num);
    if(!tag) {
        pdlog(LOG_MODULE_PCCC_OBJECT, LOG_LEVEL_WARN, "PLC5 Write: unable to find tag with data file %u", file_num);
        return pccc_build_error_response(response, sequence_id, PCCC_ERR_ADDR_NOT_USABLE);
    }

    /* Calculate byte offsets */
    tag_size = tag->elem_count * tag->elem_size;
    start_byte_offset = offset + (element_num * tag->elem_size);
    end_byte_offset = start_byte_offset + (transfer_size * tag->elem_size);

    pdlog(LOG_MODULE_PCCC_OBJECT, LOG_LEVEL_DETAIL, "PLC5 Write: file=%u elem=%u offset=%u size=%u bytes -> [%zu..%zu of %zu]",
          file_num, element_num, offset, (transfer_size * tag->elem_size), start_byte_offset, end_byte_offset, tag_size);

    /* Bounds check */
    if(start_byte_offset >= tag_size) {
        pdlog(LOG_MODULE_PCCC_OBJECT, LOG_LEVEL_WARN, "PLC5 Write: start offset %zu >= tag size %zu", start_byte_offset,
              tag_size);
        return pccc_build_error_response(response, sequence_id, PCCC_ERR_FILE_WRONG_SIZE);
    }

    if(end_byte_offset > tag_size) {
        pdlog(LOG_MODULE_PCCC_OBJECT, LOG_LEVEL_WARN, "PLC5 Write: end offset %zu > tag size %zu", end_byte_offset, tag_size);
        return pccc_build_error_response(response, sequence_id, PCCC_ERR_FILE_WRONG_SIZE);
    }

    if((end_byte_offset - start_byte_offset) > 240) {
        pdlog(LOG_MODULE_PCCC_OBJECT, LOG_LEVEL_WARN, "PLC5 Write: request too large (%zu bytes)",
              end_byte_offset - start_byte_offset);
        return pccc_build_error_response(response, sequence_id, PCCC_ERR_FILE_WRONG_SIZE);
    }

    /* Verify data length matches transfer size */
    size_t expected_data_len = transfer_size * tag->elem_size;
    size_t remaining = buf_read_size(request);

    if(remaining != expected_data_len) {
        pdlog(LOG_MODULE_PCCC_OBJECT, LOG_LEVEL_WARN, "PLC5 Write: data length mismatch (got %zu, expected %zu)", remaining,
              expected_data_len);
        return pccc_build_error_response(response, sequence_id, PCCC_ERR_FILE_WRONG_SIZE);
    }

    /* Copy data into tag */
    for(size_t i = 0; i < expected_data_len; i++) {
        uint8_t byte_val = 0;
        ok &= buf_read_u8(request, "data", &byte_val);
        if(ok) { tag->data[start_byte_offset + i] = byte_val; }
    }

    if(!ok) {
        pdlog(LOG_MODULE_PCCC_OBJECT, LOG_LEVEL_WARN, "PLC5 Write: failed to read request data");
        return buf_get_error(request);
    }

    /* Build response header (no data in response) */
    util_err_t err = pccc_build_response_header(response, sequence_id);
    if(err != UTIL_OK) { return err; }

    pdlog(LOG_MODULE_PCCC_OBJECT, LOG_LEVEL_DETAIL, "PLC5 Write: success");
    return UTIL_OK;
}

/**
 * PLC5 Read-Modify-Write Command (0x26)
 *
 * Request format:
 *   [0]     uint8   0x26 (command)
 *   [1]     uint8   File prefix (0x06)
 *   [2]     uint8   File number
 *   [3]     uint8   Element number
 *   [4+N]   uint8[] AND mask (N = element size)
 *   [4+2N+] uint8[] OR mask (N = element size)
 *
 * Operation: new_value = (old_value & AND_mask) | OR_mask (byte-by-byte)
 */
static util_err_t pccc_handle_plc5_rmw(buf_t *request, buf_t *response, plc_context_t *plc, uint16_t sequence_id) {
    uint8_t file_prefix = 0;
    uint8_t file_num = 0;
    uint8_t element_num = 0;
    size_t start_byte_offset = 0;
    size_t tag_size = 0;
    bool ok = true;

    /* Parse request */
    ok &= buf_read_u8(request, "file_prefix", &file_prefix);
    ok &= buf_read_u8(request, "file_num", &file_num);
    ok &= buf_read_u8(request, "element_num", &element_num);

    if(!ok || file_prefix != 0x06) {
        pdlog(LOG_MODULE_PCCC_OBJECT, LOG_LEVEL_WARN, "PLC5 RMW: invalid request format or file prefix");
        return pccc_build_error_response(response, sequence_id, PCCC_ERR_ADDR_NOT_USABLE);
    }

    /* Find tag */
    tag_def_t *tag = tag_find_by_file_num(plc->tags, plc->tag_count, file_num);
    if(!tag) {
        pdlog(LOG_MODULE_PCCC_OBJECT, LOG_LEVEL_WARN, "PLC5 RMW: unable to find tag with data file %u", file_num);
        return pccc_build_error_response(response, sequence_id, PCCC_ERR_ADDR_NOT_USABLE);
    }

    /* Calculate byte offset */
    tag_size = tag->elem_count * tag->elem_size;
    start_byte_offset = element_num * tag->elem_size;

    pdlog(LOG_MODULE_PCCC_OBJECT, LOG_LEVEL_DETAIL, "PLC5 RMW: file=%u elem=%u size=%zu bytes -> offset %zu of %zu", file_num,
          element_num, tag->elem_size, start_byte_offset, tag_size);

    /* Bounds check */
    if(start_byte_offset >= tag_size) {
        pdlog(LOG_MODULE_PCCC_OBJECT, LOG_LEVEL_WARN, "PLC5 RMW: start offset %zu >= tag size %zu", start_byte_offset, tag_size);
        return pccc_build_error_response(response, sequence_id, PCCC_ERR_FILE_WRONG_SIZE);
    }

    if(start_byte_offset + tag->elem_size > tag_size) {
        pdlog(LOG_MODULE_PCCC_OBJECT, LOG_LEVEL_WARN, "PLC5 RMW: end offset %zu > tag size %zu",
              start_byte_offset + tag->elem_size, tag_size);
        return pccc_build_error_response(response, sequence_id, PCCC_ERR_FILE_WRONG_SIZE);
    }

    /* Verify we have AND and OR masks */
    size_t expected_mask_len = tag->elem_size * 2; /* AND mask + OR mask */
    size_t remaining = buf_read_size(request);

    if(remaining != expected_mask_len) {
        pdlog(LOG_MODULE_PCCC_OBJECT, LOG_LEVEL_WARN, "PLC5 RMW: mask data length mismatch (got %zu, expected %zu)", remaining,
              expected_mask_len);
        return pccc_build_error_response(response, sequence_id, PCCC_ERR_FILE_WRONG_SIZE);
    }

    /* Apply AND/OR masks byte-by-byte */
    for(size_t i = 0; i < tag->elem_size; i++) {
        uint8_t and_mask = 0;

        ok &= buf_read_u8(request, "and_mask", &and_mask);
        if(!ok) {
            pdlog(LOG_MODULE_PCCC_OBJECT, LOG_LEVEL_WARN, "PLC5 RMW: failed to read AND mask");
            return buf_get_error(request);
        }

        /* Read all AND masks first, then OR masks */
        tag->data[start_byte_offset + i] = (tag->data[start_byte_offset + i] & and_mask);
    }

    /* Now apply OR masks */
    for(size_t i = 0; i < tag->elem_size; i++) {
        uint8_t or_mask = 0;
        ok &= buf_read_u8(request, "or_mask", &or_mask);
        if(!ok) {
            pdlog(LOG_MODULE_PCCC_OBJECT, LOG_LEVEL_WARN, "PLC5 RMW: failed to read OR mask");
            return buf_get_error(request);
        }

        tag->data[start_byte_offset + i] |= or_mask;
    }

    if(!ok) {
        pdlog(LOG_MODULE_PCCC_OBJECT, LOG_LEVEL_WARN, "PLC5 RMW: failed to process masks");
        return buf_get_error(request);
    }

    /* Build response header */
    util_err_t err = pccc_build_response_header(response, sequence_id);
    if(err != UTIL_OK) { return err; }

    pdlog(LOG_MODULE_PCCC_OBJECT, LOG_LEVEL_DETAIL, "PLC5 RMW: success");
    return UTIL_OK;
}

/* ============================================================================
 * PCCC SLC/MicroLogix Command Handlers
 * ============================================================================ */

/**
 * SLC Read Command (0xA2)
 *
 * Request format:
 *   [0]     uint8   0xA2 (command)
 *   [1]     uint8   Transfer size in bytes
 *   [2]     uint8   File number
 *   [3]     uint8   File type (0x85, 0x89, 0x8A, 0x8D, 0x91)
 *   [4]     uint8   Element number
 *   [5]     uint8   Sub-element (must be 0x00)
 *
 * Response format (appended to header):
 *   [0+]    uint8[] Data bytes
 */
static util_err_t pccc_handle_slc_read(buf_t *request, buf_t *response, plc_context_t *plc, uint16_t sequence_id) {
    uint8_t transfer_size = 0;
    uint8_t file_num = 0;
    uint8_t file_type = 0;
    uint8_t element_num = 0;
    uint8_t sub_element = 0;
    size_t start_byte_offset = 0;
    size_t end_byte_offset = 0;
    size_t tag_size = 0;
    bool ok = true;

    /* Parse request */
    ok &= buf_read_u8(request, "transfer_size", &transfer_size);
    ok &= buf_read_u8(request, "file_num", &file_num);
    ok &= buf_read_u8(request, "file_type", &file_type);
    ok &= buf_read_u8(request, "element_num", &element_num);
    ok &= buf_read_u8(request, "sub_element", &sub_element);

    if(!ok) {
        pdlog(LOG_MODULE_PCCC_OBJECT, LOG_LEVEL_WARN, "SLC Read: invalid request format");
        return pccc_build_error_response(response, sequence_id, PCCC_ERR_ADDR_NOT_USABLE);
    }

    if(sub_element != 0x00) {
        pdlog(LOG_MODULE_PCCC_OBJECT, LOG_LEVEL_WARN, "SLC Read: sub-element must be 0x00 (got 0x%02X)", sub_element);
        return pccc_build_error_response(response, sequence_id, PCCC_ERR_ADDR_NOT_USABLE);
    }

    /* Find tag */
    tag_def_t *tag = tag_find_by_file_num(plc->tags, plc->tag_count, file_num);
    if(!tag) {
        pdlog(LOG_MODULE_PCCC_OBJECT, LOG_LEVEL_WARN, "SLC Read: unable to find tag with data file %u", file_num);
        return pccc_build_error_response(response, sequence_id, PCCC_ERR_ADDR_NOT_USABLE);
    }

    /* Verify file type matches */
    if(tag->tag_type != file_type) {
        pdlog(LOG_MODULE_PCCC_OBJECT, LOG_LEVEL_WARN, "SLC Read: file type mismatch (requested 0x%02X, tag is 0x%02X)", file_type,
              tag->tag_type);
        return pccc_build_error_response(response, sequence_id, PCCC_ERR_ADDR_NOT_USABLE);
    }

    /* Calculate byte offsets */
    tag_size = tag->elem_count * tag->elem_size;
    start_byte_offset = element_num * tag->elem_size;
    end_byte_offset = start_byte_offset + transfer_size;

    pdlog(LOG_MODULE_PCCC_OBJECT, LOG_LEVEL_DETAIL, "SLC Read: file=%u elem=%u type=0x%02X size=%u bytes -> [%zu..%zu of %zu]",
          file_num, element_num, file_type, transfer_size, start_byte_offset, end_byte_offset, tag_size);

    /* Bounds check */
    if(start_byte_offset >= tag_size) {
        pdlog(LOG_MODULE_PCCC_OBJECT, LOG_LEVEL_WARN, "SLC Read: start offset %zu >= tag size %zu", start_byte_offset, tag_size);
        return pccc_build_error_response(response, sequence_id, PCCC_ERR_FILE_WRONG_SIZE);
    }

    if(end_byte_offset > tag_size) {
        pdlog(LOG_MODULE_PCCC_OBJECT, LOG_LEVEL_WARN, "SLC Read: end offset %zu > tag size %zu", end_byte_offset, tag_size);
        return pccc_build_error_response(response, sequence_id, PCCC_ERR_FILE_WRONG_SIZE);
    }

    if(transfer_size > 240) {
        pdlog(LOG_MODULE_PCCC_OBJECT, LOG_LEVEL_WARN, "SLC Read: request too large (%u bytes)", transfer_size);
        return pccc_build_error_response(response, sequence_id, PCCC_ERR_FILE_WRONG_SIZE);
    }

    /* Build response header */
    util_err_t err = pccc_build_response_header(response, sequence_id);
    if(err != UTIL_OK) { return err; }

    /* Copy data */
    for(size_t i = 0; i < transfer_size; i++) { ok &= buf_write_u8(response, "data", tag->data[start_byte_offset + i]); }

    if(!ok) {
        pdlog(LOG_MODULE_PCCC_OBJECT, LOG_LEVEL_WARN, "SLC Read: failed to write response data");
        return buf_get_error(response);
    }

    pdlog(LOG_MODULE_PCCC_OBJECT, LOG_LEVEL_DETAIL, "SLC Read: success");
    return UTIL_OK;
}

/**
 * SLC Write Command (0xAA)
 *
 * Request format:
 *   [0]     uint8   0xAA (command)
 *   [1]     uint8   Transfer size in bytes
 *   [2]     uint8   File number
 *   [3]     uint8   File type
 *   [4]     uint8   Element number
 *   [5]     uint8   Sub-element (must be 0x00)
 *   [6+]    uint8[] Data bytes
 *
 * Response format:
 *   (header only, 4 bytes)
 */
static util_err_t pccc_handle_slc_write(buf_t *request, buf_t *response, plc_context_t *plc, uint16_t sequence_id) {
    uint8_t transfer_size = 0;
    uint8_t file_num = 0;
    uint8_t file_type = 0;
    uint8_t element_num = 0;
    uint8_t sub_element = 0;
    size_t start_byte_offset = 0;
    size_t end_byte_offset = 0;
    size_t tag_size = 0;
    bool ok = true;

    /* Parse request */
    ok &= buf_read_u8(request, "transfer_size", &transfer_size);
    ok &= buf_read_u8(request, "file_num", &file_num);
    ok &= buf_read_u8(request, "file_type", &file_type);
    ok &= buf_read_u8(request, "element_num", &element_num);
    ok &= buf_read_u8(request, "sub_element", &sub_element);

    if(!ok) {
        pdlog(LOG_MODULE_PCCC_OBJECT, LOG_LEVEL_WARN, "SLC Write: invalid request format");
        return pccc_build_error_response(response, sequence_id, PCCC_ERR_ADDR_NOT_USABLE);
    }

    if(sub_element != 0x00) {
        pdlog(LOG_MODULE_PCCC_OBJECT, LOG_LEVEL_WARN, "SLC Write: sub-element must be 0x00 (got 0x%02X)", sub_element);
        return pccc_build_error_response(response, sequence_id, PCCC_ERR_ADDR_NOT_USABLE);
    }

    /* Find tag */
    tag_def_t *tag = tag_find_by_file_num(plc->tags, plc->tag_count, file_num);
    if(!tag) {
        pdlog(LOG_MODULE_PCCC_OBJECT, LOG_LEVEL_WARN, "SLC Write: unable to find tag with data file %u", file_num);
        return pccc_build_error_response(response, sequence_id, PCCC_ERR_ADDR_NOT_USABLE);
    }

    /* Verify file type matches */
    if(tag->tag_type != file_type) {
        pdlog(LOG_MODULE_PCCC_OBJECT, LOG_LEVEL_WARN, "SLC Write: file type mismatch (requested 0x%02X, tag is 0x%02X)",
              file_type, tag->tag_type);
        return pccc_build_error_response(response, sequence_id, PCCC_ERR_ADDR_NOT_USABLE);
    }

    /* Calculate byte offsets */
    tag_size = tag->elem_count * tag->elem_size;
    start_byte_offset = element_num * tag->elem_size;
    end_byte_offset = start_byte_offset + transfer_size;

    pdlog(LOG_MODULE_PCCC_OBJECT, LOG_LEVEL_DETAIL, "SLC Write: file=%u elem=%u type=0x%02X size=%u bytes -> [%zu..%zu of %zu]",
          file_num, element_num, file_type, transfer_size, start_byte_offset, end_byte_offset, tag_size);

    /* Bounds check */
    if(start_byte_offset >= tag_size) {
        pdlog(LOG_MODULE_PCCC_OBJECT, LOG_LEVEL_WARN, "SLC Write: start offset %zu >= tag size %zu", start_byte_offset, tag_size);
        return pccc_build_error_response(response, sequence_id, PCCC_ERR_FILE_WRONG_SIZE);
    }

    if(end_byte_offset > tag_size) {
        pdlog(LOG_MODULE_PCCC_OBJECT, LOG_LEVEL_WARN, "SLC Write: end offset %zu > tag size %zu", end_byte_offset, tag_size);
        return pccc_build_error_response(response, sequence_id, PCCC_ERR_FILE_WRONG_SIZE);
    }

    if(transfer_size > 240) {
        pdlog(LOG_MODULE_PCCC_OBJECT, LOG_LEVEL_WARN, "SLC Write: request too large (%u bytes)", transfer_size);
        return pccc_build_error_response(response, sequence_id, PCCC_ERR_FILE_WRONG_SIZE);
    }

    /* Verify data length */
    size_t remaining = buf_read_size(request);
    if(remaining != transfer_size) {
        pdlog(LOG_MODULE_PCCC_OBJECT, LOG_LEVEL_WARN, "SLC Write: data length mismatch (got %zu, expected %u)", remaining,
              transfer_size);
        return pccc_build_error_response(response, sequence_id, PCCC_ERR_FILE_WRONG_SIZE);
    }

    /* Copy data */
    for(size_t i = 0; i < transfer_size; i++) {
        uint8_t byte_val = 0;
        ok &= buf_read_u8(request, "data", &byte_val);
        if(ok) { tag->data[start_byte_offset + i] = byte_val; }
    }

    if(!ok) {
        pdlog(LOG_MODULE_PCCC_OBJECT, LOG_LEVEL_WARN, "SLC Write: failed to read request data");
        return buf_get_error(request);
    }

    /* Build response header */
    util_err_t err = pccc_build_response_header(response, sequence_id);
    if(err != UTIL_OK) { return err; }

    pdlog(LOG_MODULE_PCCC_OBJECT, LOG_LEVEL_DETAIL, "SLC Write: success");
    return UTIL_OK;
}

/**
 * SLC Read-Modify-Write Command (0xAB)
 *
 * Request format:
 *   [0]     uint8   0xAB (command)
 *   [1]     uint8   Transfer size (must be 0x02 for 2-byte elements)
 *   [2]     uint8   File number
 *   [3]     uint8   File type
 *   [4]     uint8   Element number
 *   [5]     uint8   Sub-element (must be 0x00)
 *   [6-7]   uint16  Mask (LE) - indicates which bits to modify
 *   [8-9]   uint16  Data (LE) - new values for masked bits
 *
 * Operation: new_value = (old_value & ~mask) | (data & mask)
 * Only supports 2-byte elements.
 */
static util_err_t pccc_handle_slc_rmw(buf_t *request, buf_t *response, plc_context_t *plc, uint16_t sequence_id) {
    uint8_t transfer_size = 0;
    uint8_t file_num = 0;
    uint8_t file_type = 0;
    uint8_t element_num = 0;
    uint8_t sub_element = 0;
    uint16_t mask = 0;
    uint16_t data = 0;
    size_t start_byte_offset = 0;
    size_t tag_size = 0;
    bool ok = true;

    /* Parse request */
    ok &= buf_read_u8(request, "transfer_size", &transfer_size);
    ok &= buf_read_u8(request, "file_num", &file_num);
    ok &= buf_read_u8(request, "file_type", &file_type);
    ok &= buf_read_u8(request, "element_num", &element_num);
    ok &= buf_read_u8(request, "sub_element", &sub_element);
    ok &= buf_read_u16_le(request, "mask", &mask);
    ok &= buf_read_u16_le(request, "data", &data);

    if(!ok) {
        pdlog(LOG_MODULE_PCCC_OBJECT, LOG_LEVEL_WARN, "SLC RMW: invalid request format");
        return pccc_build_error_response(response, sequence_id, PCCC_ERR_ADDR_NOT_USABLE);
    }

    if(sub_element != 0x00) {
        pdlog(LOG_MODULE_PCCC_OBJECT, LOG_LEVEL_WARN, "SLC RMW: sub-element must be 0x00 (got 0x%02X)", sub_element);
        return pccc_build_error_response(response, sequence_id, PCCC_ERR_ADDR_NOT_USABLE);
    }

    if(transfer_size != 2) {
        pdlog(LOG_MODULE_PCCC_OBJECT, LOG_LEVEL_WARN, "SLC RMW: transfer size must be 2 (got %u)", transfer_size);
        return pccc_build_error_response(response, sequence_id, PCCC_ERR_FILE_WRONG_SIZE);
    }

    /* Find tag */
    tag_def_t *tag = tag_find_by_file_num(plc->tags, plc->tag_count, file_num);
    if(!tag) {
        pdlog(LOG_MODULE_PCCC_OBJECT, LOG_LEVEL_WARN, "SLC RMW: unable to find tag with data file %u", file_num);
        return pccc_build_error_response(response, sequence_id, PCCC_ERR_ADDR_NOT_USABLE);
    }

    /* Verify file type matches */
    if(tag->tag_type != file_type) {
        pdlog(LOG_MODULE_PCCC_OBJECT, LOG_LEVEL_WARN, "SLC RMW: file type mismatch (requested 0x%02X, tag is 0x%02X)", file_type,
              tag->tag_type);
        return pccc_build_error_response(response, sequence_id, PCCC_ERR_ADDR_NOT_USABLE);
    }

    /* SLC RMW only works with 2-byte elements */
    if(tag->elem_size != 2) {
        pdlog(LOG_MODULE_PCCC_OBJECT, LOG_LEVEL_WARN, "SLC RMW: element size must be 2 bytes (got %zu)", tag->elem_size);
        return pccc_build_error_response(response, sequence_id, PCCC_ERR_FILE_WRONG_SIZE);
    }

    /* Calculate offset */
    tag_size = tag->elem_count * tag->elem_size;
    start_byte_offset = element_num * tag->elem_size;

    pdlog(LOG_MODULE_PCCC_OBJECT, LOG_LEVEL_DETAIL,
          "SLC RMW: file=%u elem=%u type=0x%02X mask=0x%04X data=0x%04X -> offset %zu of %zu", file_num, element_num, file_type,
          mask, data, start_byte_offset, tag_size);

    /* Bounds check */
    if(start_byte_offset >= tag_size) {
        pdlog(LOG_MODULE_PCCC_OBJECT, LOG_LEVEL_WARN, "SLC RMW: start offset %zu >= tag size %zu", start_byte_offset, tag_size);
        return pccc_build_error_response(response, sequence_id, PCCC_ERR_FILE_WRONG_SIZE);
    }

    if(start_byte_offset + 2 > tag_size) {
        pdlog(LOG_MODULE_PCCC_OBJECT, LOG_LEVEL_WARN, "SLC RMW: end offset %zu > tag size %zu", start_byte_offset + 2, tag_size);
        return pccc_build_error_response(response, sequence_id, PCCC_ERR_FILE_WRONG_SIZE);
    }

    /* Read old value (little-endian) */
    uint16_t old_value = (uint16_t)(tag->data[start_byte_offset] | (tag->data[start_byte_offset + 1] << 8));

    /* Apply mask: preserve bits NOT in mask, apply data bits that ARE in mask */
    uint16_t new_value = (old_value & ~mask) | (data & mask);

    /* Store new value (little-endian) */
    tag->data[start_byte_offset] = new_value & 0xFF;
    tag->data[start_byte_offset + 1] = (new_value >> 8) & 0xFF;

    /* Build response header */
    util_err_t err = pccc_build_response_header(response, sequence_id);
    if(err != UTIL_OK) { return err; }

    pdlog(LOG_MODULE_PCCC_OBJECT, LOG_LEVEL_DETAIL, "SLC RMW: success (0x%04X -> 0x%04X)", old_value, new_value);
    return UTIL_OK;
}

/* ============================================================================
 * PCCC Command Dispatcher
 * ============================================================================ */

/**
 * Dispatch PCCC command to appropriate handler
 *
 * PCCC packet format:
 *   [0-1]   uint16  0x0F 0x00 (PCCC prefix)
 *   [2-3]   uint16  Sequence ID (little-endian)
 *   [4]     uint8   Command byte
 *   [5+]    ...     Command-specific data
 */
static util_err_t pccc_dispatch_command(buf_t *input, buf_t *output, client_context_t *client) {
    plc_context_t *plc = client->plc;
    uint8_t prefix0 = 0;
    uint8_t prefix1 = 0;
    uint16_t sequence_id = 0;
    uint8_t command = 0;
    bool ok = true;

    /* Parse PCCC header */
    ok &= buf_read_u8(input, "prefix0", &prefix0);
    ok &= buf_read_u8(input, "prefix1", &prefix1);
    ok &= buf_read_u16_le(input, "sequence_id", &sequence_id);
    ok &= buf_read_u8(input, "command", &command);

    if(!ok) {
        pdlog(LOG_MODULE_PCCC_OBJECT, LOG_LEVEL_WARN, "PCCC dispatch: failed to parse PCCC header");
        return buf_get_error(input);
    }

    if(prefix0 != PCCC_PREFIX_BYTE0 || prefix1 != PCCC_PREFIX_BYTE1) {
        pdlog(LOG_MODULE_PCCC_OBJECT, LOG_LEVEL_WARN, "PCCC dispatch: invalid PCCC prefix (0x%02X 0x%02X)", prefix0, prefix1);
        return pccc_build_error_response(output, sequence_id, PCCC_ERR_UNSUPPORTED_CMD);
    }

    pdlog(LOG_MODULE_PCCC_OBJECT, LOG_LEVEL_DETAIL, "PCCC dispatch: cmd=0x%02X seq=0x%04X", command, sequence_id);

    /* Dispatch based on PLC type and command */
    switch(command) {
        case PCCC_CMD_PLC5_READ:
            if(plc->plc_type != PLC_TYPE_PLC5) {
                pdlog(LOG_MODULE_PCCC_OBJECT, LOG_LEVEL_WARN, "PCCC: PLC5 command 0x%02X on non-PLC5 device", command);
                return pccc_build_error_response(output, sequence_id, PCCC_ERR_UNSUPPORTED_CMD);
            }
            return pccc_handle_plc5_read(input, output, plc, sequence_id);

        case PCCC_CMD_PLC5_WRITE:
            if(plc->plc_type != PLC_TYPE_PLC5) {
                pdlog(LOG_MODULE_PCCC_OBJECT, LOG_LEVEL_WARN, "PCCC: PLC5 command 0x%02X on non-PLC5 device", command);
                return pccc_build_error_response(output, sequence_id, PCCC_ERR_UNSUPPORTED_CMD);
            }
            return pccc_handle_plc5_write(input, output, plc, sequence_id);

        case PCCC_CMD_PLC5_RMW:
            if(plc->plc_type != PLC_TYPE_PLC5) {
                pdlog(LOG_MODULE_PCCC_OBJECT, LOG_LEVEL_WARN, "PCCC: PLC5 command 0x%02X on non-PLC5 device", command);
                return pccc_build_error_response(output, sequence_id, PCCC_ERR_UNSUPPORTED_CMD);
            }
            return pccc_handle_plc5_rmw(input, output, plc, sequence_id);

        case PCCC_CMD_SLC_READ:
            if(plc->plc_type != PLC_TYPE_SLC && plc->plc_type != PLC_TYPE_MICROLOGIX) {
                pdlog(LOG_MODULE_PCCC_OBJECT, LOG_LEVEL_WARN, "PCCC: SLC command 0x%02X on non-SLC/ML device", command);
                return pccc_build_error_response(output, sequence_id, PCCC_ERR_UNSUPPORTED_CMD);
            }
            return pccc_handle_slc_read(input, output, plc, sequence_id);

        case PCCC_CMD_SLC_WRITE:
            if(plc->plc_type != PLC_TYPE_SLC && plc->plc_type != PLC_TYPE_MICROLOGIX) {
                pdlog(LOG_MODULE_PCCC_OBJECT, LOG_LEVEL_WARN, "PCCC: SLC command 0x%02X on non-SLC/ML device", command);
                return pccc_build_error_response(output, sequence_id, PCCC_ERR_UNSUPPORTED_CMD);
            }
            return pccc_handle_slc_write(input, output, plc, sequence_id);

        case PCCC_CMD_SLC_RMW:
            if(plc->plc_type != PLC_TYPE_SLC && plc->plc_type != PLC_TYPE_MICROLOGIX) {
                pdlog(LOG_MODULE_PCCC_OBJECT, LOG_LEVEL_WARN, "PCCC: SLC command 0x%02X on non-SLC/ML device", command);
                return pccc_build_error_response(output, sequence_id, PCCC_ERR_UNSUPPORTED_CMD);
            }
            return pccc_handle_slc_rmw(input, output, plc, sequence_id);

        default:
            pdlog(LOG_MODULE_PCCC_OBJECT, LOG_LEVEL_WARN, "PCCC dispatch: unsupported command 0x%02X", command);
            return pccc_build_error_response(output, sequence_id, PCCC_ERR_UNSUPPORTED_CMD);
    }
}

/* ============================================================================
 * Service 0x4B: PCCC Execute
 * ============================================================================ */

/**
 * Service 0x4B: PCCC Execute
 *
 * This is the main CIP service handler for PCCC requests.
 * The input buffer contains:
 *   [0]     uint8   Service code (0x4B)
 *   [1]     uint8   Path size
 *   [2...]  Path segments
 *   [...]   PCCC Prefix (0x07 0x3D 0xF3 0x45 0x43 0x50 0x21)
 *   [...]   PCCC Packet proper
 *
 * The CIP message router has already positioned us past the service and path.
 * We need to skip the PCCC prefix and process the PCCC packet.
 */
static util_err_t pccc_execute_service(uint8_t service, const cip_path_t *path, buf_t *request, buf_t *response,
                                       cip_object_instance_t *instance, client_context_t *client) {
    plc_context_t *plc = (plc_context_t *)client->plc;
    (void)service;
    (void)path;
    (void)instance;

    pdlog(LOG_MODULE_PCCC_OBJECT, LOG_LEVEL_DETAIL, "PCCC Execute (0x4B): processing request");

    /* Skip PCCC prefix (7 bytes: 0x07 0x3D 0xF3 0x45 0x43 0x50 0x21) */
    uint8_t prefix_byte = 0;
    for(int i = 0; i < 7; i++) {
        bool ok = buf_read_u8(request, "prefix", &prefix_byte);
        if(!ok) {
            pdlog(LOG_MODULE_PCCC_OBJECT, LOG_LEVEL_WARN, "PCCC Execute: failed to skip PCCC prefix");
            cip_build_response(response, 0x4B, CIP_STATUS_INVALID_PARAM);
            return buf_get_error(request);
        }
    }

    /* Build CIP response header with PCCC prefix */
    util_err_t err = cip_build_response(response, 0x4B, CIP_STATUS_OK);
    if(err != UTIL_OK) {
        pdlog(LOG_MODULE_PCCC_OBJECT, LOG_LEVEL_WARN, "PCCC Execute: failed to build CIP response header");
        return err;
    }

    /* Write PCCC response prefix */
    bool ok = true;
    ok &= buf_write_u8(response, "pccc_prefix_0", 0x07);
    ok &= buf_write_u8(response, "pccc_prefix_1", 0x3D);
    ok &= buf_write_u8(response, "pccc_prefix_2", 0xF3);
    ok &= buf_write_u8(response, "pccc_prefix_3", 0x45);
    ok &= buf_write_u8(response, "pccc_prefix_4", 0x43);
    ok &= buf_write_u8(response, "pccc_prefix_5", 0x50);
    ok &= buf_write_u8(response, "pccc_prefix_6", 0x21);

    if(!ok) {
        pdlog(LOG_MODULE_PCCC_OBJECT, LOG_LEVEL_WARN, "PCCC Execute: failed to write PCCC response prefix");
        return buf_get_error(response);
    }

    /* Dispatch PCCC command */
    return pccc_dispatch_command(request, response, plc);
}

/* ============================================================================
 * Object Registration
 * ============================================================================ */

void pccc_object_register(cip_object_registry_t *registry) {
    /* Create PCCC object class */
    cip_object_class_t *pccc_class = (cip_object_class_t *)malloc(sizeof(*pccc_class));
    if(!pccc_class) {
        pdlog(LOG_MODULE_PCCC_OBJECT, LOG_LEVEL_ERROR, "PCCC Object: failed to allocate class");
        return;
    }

    /* Initialize class */
    memset(pccc_class, 0, sizeof(*pccc_class));
    pccc_class->class_id = 0x67; /* PCCC Object class ID */
    pccc_class->class_name = "PCCC Object";

    /* Register service 0x4B handler */
    pccc_class->service_handlers[CIP_SRV_EXEC_PCCC_AB] = pccc_execute_service;

    /* No instance management needed for PCCC singleton object */
    pccc_class->get_instance = NULL;
    pccc_class->create_instance = NULL;
    pccc_class->destroy_instance = NULL;

    /* Register class with registry */
    util_err_t err = cip_registry_register_class(registry, pccc_class);
    if(err != UTIL_OK) {
        pdlog(LOG_MODULE_PCCC_OBJECT, LOG_LEVEL_ERROR, "PCCC Object: failed to register class: %s", util_err_str(err));
        free(pccc_class);
    } else {
        pdlog(LOG_MODULE_PCCC_OBJECT, LOG_LEVEL_INFO, "PCCC Object (class 0x67) registered");
    }
}
