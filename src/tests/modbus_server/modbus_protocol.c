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

#include "modbus_protocol.h"
#include "log.h"
#include "data_reader.h"
#include "packet_builder.h"
#include <stdlib.h>
#include <string.h>

/* Map util_err_t to Modbus exception codes */
static inline uint8_t util_err_to_modbus_exception(util_err_t err) {
    switch(err) {
        case UTIL_OK: return 0; /* No error */
        case UTIL_EBOUNDS: return MODBUS_EXCEPTION_ILLEGAL_ADDRESS;
        case UTIL_EINVAL: return MODBUS_EXCEPTION_ILLEGAL_DATA_VALUE;
        case UTIL_ERESOURCE: return MODBUS_EXCEPTION_DEVICE_FAILURE;
        default: return MODBUS_EXCEPTION_ILLEGAL_FUNCTION;
    }
}

/* Parse MBAP header from buffer using buf.h API */
util_err_t modbus_parse_mbap_header(data_reader_t *request, mbap_header_t *header) {
    if(!request || !header) { return UTIL_EINVAL; }

    /* Save checkpoint for potential rollback */
    data_reader_t checkpoint = *request;

    /* Read MBAP header fields */
    if(!data_reader_read_u16_be(request, "transaction_id", &header->transaction_id)
       || !data_reader_read_u16_be(request, "protocol_id", &header->protocol_id)
       || !data_reader_read_u16_be(request, "length", &header->length)
       || !data_reader_read_u8(request, "unit_id", &header->unit_id)) {
        *request = checkpoint;
        return data_reader_get_err(request);
    }

    /* Validate protocol ID (must be 0 for Modbus TCP) */
    if(header->protocol_id != 0) {
        *request = checkpoint;
        return UTIL_EINVAL;
    }

    /* Validate length field (PDU length: unit id + function_code + data, min 2, max 260 + 1) */
    if(header->length < 2 || header->length > (MODBUS_MAX_PDU_SIZE + 1)) {
        *request = checkpoint;
        return UTIL_EINVAL;
    }

    return UTIL_OK;
}

/* Build MBAP response header in buffer */
util_err_t modbus_build_response_header(packet_builder_t *response, const mbap_header_t *req_header,
                                        pb_segment_id_t payload_seg_id) {
    if(!response || !req_header) { return UTIL_EINVAL; }

    /* Set default segment to MBAP segment for writing header */
    if(!pb_set_default_seg_id(response, MODBUS_MBAP_SEG_ID)) {
        return pb_get_err(response);
    }

    /* Get payload length from payload segment */
    size_t pdu_length = pb_segment_len(response, payload_seg_id);

    /* Write MBAP header fields to MBAP segment */
    bool ok = true;
    ok = ok && pb_write_u16_be(response, "transaction_id", req_header->transaction_id);
    ok = ok && pb_write_u16_be(response, "protocol_id", 0);         /* protocol_id = 0 */
    ok = ok && pb_write_u16_be(response, "length", pdu_length + 1); /* length = unit_id + PDU */
    ok = ok && pb_write_u8(response, "unit_id", req_header->unit_id);

    return pb_get_err(response);
}

/* Build exception response */
void modbus_build_exception_response(packet_builder_t *response, const mbap_header_t *req_header, uint8_t function_code,
                                     util_err_t error) {
    if(!response || !req_header) {
        pdlog(LOG_MODULE_MODBUS_PROTOCOL, LOG_LEVEL_WARN, "NULL pointer(s) to build exception response");
        return;
    }

    /* Reset response buffer to clear any previous data */
    if(!pb_reset(response)) {
        pdlog(LOG_MODULE_MODBUS_PROTOCOL, LOG_LEVEL_WARN, "Failed to reset response buffer: %s", util_err_str(pb_get_err(response)));
        return;
    }

    /* Add segments: MBAP (7 bytes) + payload (2 bytes: FC + exception code) */
    if(!pb_add_segment(response, MODBUS_MBAP_SEG_ID, MBAP_HEADER_SIZE)) {
        pdlog(LOG_MODULE_MODBUS_PROTOCOL, LOG_LEVEL_WARN, "Failed to add MBAP segment %s", util_err_str(pb_get_err(response)));
        return;
    }

    if(!pb_add_segment(response, MODBUS_PAYLOAD_SEG_ID, 2)) {  /* FC + exception code */
        pdlog(LOG_MODULE_MODBUS_PROTOCOL, LOG_LEVEL_WARN, "Failed to add payload segment %s", util_err_str(pb_get_err(response)));
        return;
    }

    /* Write exception function code (FC with high bit set) and exception code to payload segment */
    if(!pb_set_default_seg_id(response, MODBUS_PAYLOAD_SEG_ID)) {
        pdlog(LOG_MODULE_MODBUS_PROTOCOL, LOG_LEVEL_WARN, "Failed to set default segment %s", util_err_str(pb_get_err(response)));
        return;
    }

    if(!pb_write_u8(response, "function_code", function_code | 0x80) ||
       !pb_write_u8(response, "exception_code", util_err_to_modbus_exception(error))) {
        pdlog(LOG_MODULE_MODBUS_PROTOCOL, LOG_LEVEL_WARN, "Failed to write exception response %s", util_err_str(pb_get_err(response)));
        return;
    }

    /* Build response header */
    modbus_build_response_header(response, req_header, MODBUS_PAYLOAD_SEG_ID);
}


/* Handle FC 0x01: Read Coils */
static util_err_t handle_read_coils(data_reader_t *request, packet_builder_t *response, pb_segment_id_t payload_seg_id,
                                    const mbap_header_t *req_header, register_storage_t *storage) {
    uint16_t start_address, count;

    /* Read request parameters */
    if(!data_reader_read_u16_be(request, "start_address", &start_address) || !data_reader_read_u16_be(request, "count", &count)) {
        return UTIL_EINVAL;
    }

    /* Validate count (1-2000 coils) */
    if(count == 0 || count > MODBUS_MAX_READ_COILS) { return UTIL_EINVAL; }

    /* Allocate response buffer for coils */
    uint16_t tmp_byte_count = (uint16_t)(((uint16_t)count + (uint16_t)7) / (uint16_t)8);

    if(tmp_byte_count > MODBUS_MAX_READ_RESPONSE_BYTES) {
        pdlog(LOG_MODULE_MODBUS_PROTOCOL, LOG_LEVEL_WARN, "Requested coil count %u results in byte count %u exceeding %d bytes",
              count, tmp_byte_count, MODBUS_MAX_READ_RESPONSE_BYTES);
        return UTIL_EINVAL;
    }

    uint8_t byte_count = (uint8_t)((count + 7) / 8);
    uint8_t *coil_data = malloc(byte_count);

    if(!coil_data) { return UTIL_ERESOURCE; }

    /* Read coils from storage */
    bool success = register_storage_read_coils(storage, start_address, count, coil_data);
    if(!success) {
        free(coil_data);
        return UTIL_EBOUNDS;
    }

    /* Set default segment to payload before writing */
    if(!pb_set_default_seg_id(response, payload_seg_id)) {
        free(coil_data);
        return pb_get_err(response);
    }

    /* Write response: FC, byte_count, coil data */
    if(!pb_write_u8(response, "function_code", MODBUS_FC_READ_COILS) || !pb_write_u8(response, "byte_count", byte_count)
       || !pb_write_bytes(response, "coil_data", coil_data, byte_count)) {
        free(coil_data);
        return pb_get_err(response);
    }

    /* Build response header */
    modbus_build_response_header(response, req_header, payload_seg_id);

    free(coil_data);
    pdlog(LOG_MODULE_MODBUS_PROTOCOL, LOG_LEVEL_DETAIL, "Read %u coils from address %u", count, start_address);
    return UTIL_OK;
}

/* Handle FC 0x02: Read Discrete Inputs */
static util_err_t handle_read_discrete_inputs(data_reader_t *request, packet_builder_t *response, pb_segment_id_t payload_seg_id,
                                              const mbap_header_t *req_header, register_storage_t *storage) {
    uint16_t start_address, count;

    /* Read request parameters */
    if(!data_reader_read_u16_be(request, "start_address", &start_address) || !data_reader_read_u16_be(request, "count", &count)) {
        return UTIL_EINVAL;
    }

    /* Validate count (1-2000 inputs) */
    if(count == 0 || count > MODBUS_MAX_READ_DISCRETE_INPUTS) { return UTIL_EINVAL; }

    /* Allocate response buffer for inputs */
    uint16_t tmp_byte_count = (uint16_t)(((uint16_t)count + (uint16_t)7) / (uint16_t)8);

    if(tmp_byte_count > MODBUS_MAX_READ_RESPONSE_BYTES) {
        pdlog(LOG_MODULE_MODBUS_PROTOCOL, LOG_LEVEL_WARN,
              "Requested discrete input count %u results in byte count %u exceeding %d bytes", count, tmp_byte_count,
              MODBUS_MAX_READ_RESPONSE_BYTES);
        return UTIL_EINVAL;
    }

    uint8_t byte_count = (uint8_t)((count + 7) / 8);
    uint8_t *input_data = malloc(byte_count);
    if(!input_data) { return UTIL_ERESOURCE; }

    /* Read discrete inputs from storage */
    bool success = register_storage_read_discrete_inputs(storage, start_address, count, input_data);
    if(!success) {
        free(input_data);
        return UTIL_EBOUNDS;
    }

    /* Set default segment to payload before writing */
    if(!pb_set_default_seg_id(response, payload_seg_id)) {
        free(input_data);
        return pb_get_err(response);
    }

    /* Write response: FC, byte_count, input data */
    if(!pb_write_u8(response, "function_code", MODBUS_FC_READ_DISCRETE_INPUTS)
       || !pb_write_u8(response, "byte_count", byte_count) || !pb_write_bytes(response, "input_data", input_data, byte_count)) {
        free(input_data);
        return pb_get_err(response);
    }

    /* Build response header */
    modbus_build_response_header(response, req_header, payload_seg_id);

    free(input_data);
    pdlog(LOG_MODULE_MODBUS_PROTOCOL, LOG_LEVEL_DETAIL, "Read %u discrete inputs from address %u", count, start_address);
    return UTIL_OK;
}

/* Handle FC 0x03: Read Holding Registers */
static util_err_t handle_read_holding_registers(data_reader_t *request, packet_builder_t *response, pb_segment_id_t payload_seg_id,
                                                const mbap_header_t *req_header, register_storage_t *storage) {
    uint16_t start_address, count;

    pdlog(LOG_MODULE_MODBUS_PROTOCOL, LOG_LEVEL_DETAIL, "Handling Read Holding Registers request");

    /* Read request parameters */
    if(!data_reader_read_u16_be(request, "start_address", &start_address) || !data_reader_read_u16_be(request, "count", &count)) {
        return UTIL_EINVAL;
    }

    /* Validate count (1-125 registers) */
    if(count == 0 || count > MODBUS_MAX_READ_REGISTERS) { return UTIL_EINVAL; }

    /* Allocate response buffer for registers */
    uint16_t *register_data = malloc(count * sizeof(uint16_t));
    if(!register_data) { return UTIL_ERESOURCE; }

    /* Read holding registers from storage */
    bool success = register_storage_read_holding_registers(storage, start_address, count, register_data);
    if(!success) {
        free(register_data);
        return UTIL_EBOUNDS;
    }

    /* Set default segment to payload before writing */
    if(!pb_set_default_seg_id(response, payload_seg_id)) {
        free(register_data);
        return pb_get_err(response);
    }

    /* Write response: FC, byte_count, register values (big-endian) */
    uint8_t byte_count = (uint8_t)(count * 2);
    if(!pb_write_u8(response, "function_code", MODBUS_FC_READ_HOLDING_REGISTERS)
       || !pb_write_u8(response, "byte_count", byte_count)) {
        free(register_data);
        return pb_get_err(response);
    }

    for(uint16_t i = 0; i < count; i++) {
        if(!pb_write_u16_be(response, "register", register_data[i])) {
            free(register_data);
            return pb_get_err(response);
        }
    }

    /* Build response header */
    modbus_build_response_header(response, req_header, payload_seg_id);

    free(register_data);
    pdlog(LOG_MODULE_MODBUS_PROTOCOL, LOG_LEVEL_DETAIL, "Read %u holding registers from address %u", count, start_address);
    return UTIL_OK;
}

/* Handle FC 0x04: Read Input Registers */
static util_err_t handle_read_input_registers(data_reader_t *request, packet_builder_t *response, pb_segment_id_t payload_seg_id,
                                              const mbap_header_t *req_header, register_storage_t *storage) {
    uint16_t start_address, count;

    /* Read request parameters */
    if(!data_reader_read_u16_be(request, "start_address", &start_address) || !data_reader_read_u16_be(request, "count", &count)) {
        return UTIL_EINVAL;
    }

    /* Validate count (1-125 registers) */
    if(count == 0 || count > MODBUS_MAX_READ_REGISTERS) { return UTIL_EINVAL; }

    /* Allocate response buffer for registers */
    uint16_t *register_data = malloc(count * sizeof(uint16_t));
    if(!register_data) { return UTIL_ERESOURCE; }

    /* Read input registers from storage */
    bool success = register_storage_read_input_registers(storage, start_address, count, register_data);
    if(!success) {
        free(register_data);
        return UTIL_EBOUNDS;
    }

    /* Set default segment to payload before writing */
    if(!pb_set_default_seg_id(response, payload_seg_id)) {
        free(register_data);
        return pb_get_err(response);
    }

    /* Write response: FC, byte_count, register values (big-endian) */
    uint8_t byte_count = (uint8_t)(count * 2);
    if(!pb_write_u8(response, "function_code", MODBUS_FC_READ_INPUT_REGISTERS)
       || !pb_write_u8(response, "byte_count", byte_count)) {
        free(register_data);
        return pb_get_err(response);
    }

    for(uint16_t i = 0; i < count; i++) {
        if(!pb_write_u16_be(response, "register", register_data[i])) {
            free(register_data);
            return pb_get_err(response);
        }
    }

    /* Build response header */
    modbus_build_response_header(response, req_header, payload_seg_id);

    free(register_data);
    pdlog(LOG_MODULE_MODBUS_PROTOCOL, LOG_LEVEL_DETAIL, "Read %u input registers from address %u", count, start_address);
    return UTIL_OK;
}

/* Handle FC 0x05: Write Single Coil */
static util_err_t handle_write_single_coil(data_reader_t *request, packet_builder_t *response, pb_segment_id_t payload_seg_id,
                                           const mbap_header_t *req_header, register_storage_t *storage) {
    uint16_t address, value;

    /* Read request parameters */
    if(!data_reader_read_u16_be(request, "address", &address) || !data_reader_read_u16_be(request, "value", &value)) {
        return UTIL_EINVAL;
    }

    /* Validate coil value (0x0000 = OFF, 0xFF00 = ON) */
    if(value != 0x0000 && value != 0xFF00) { return UTIL_EINVAL; }

    /* Write coil to storage */
    bool coil_value = (value == 0xFF00);
    uint8_t coil_bytes = coil_value ? 0x01 : 0x00;
    if(!register_storage_write_coils(storage, address, 1, &coil_bytes)) { return UTIL_EBOUNDS; }

    /* Set default segment to payload before writing */
    if(!pb_set_default_seg_id(response, payload_seg_id)) {
        return pb_get_err(response);
    }

    /* Write response: FC, address, value */
    if(!pb_write_u8(response, "function_code", MODBUS_FC_WRITE_SINGLE_COIL) || !pb_write_u16_be(response, "address", address)
       || !pb_write_u16_be(response, "value", value)) {
        return pb_get_err(response);
    }

    /* Build response header (echo request as response) */
    modbus_build_response_header(response, req_header, payload_seg_id);

    pdlog(LOG_MODULE_MODBUS_PROTOCOL, LOG_LEVEL_DETAIL, "Wrote single coil at address %u = %s", address,
          coil_value ? "ON" : "OFF");
    return UTIL_OK;
}

/* Handle FC 0x06: Write Single Register */
static util_err_t handle_write_single_register(data_reader_t *request, packet_builder_t *response, pb_segment_id_t payload_seg_id,
                                               const mbap_header_t *req_header, register_storage_t *storage) {
    uint16_t address, value;

    /* Read request parameters */
    if(!data_reader_read_u16_be(request, "address", &address) || !data_reader_read_u16_be(request, "value", &value)) {
        return UTIL_EINVAL;
    }

    /* Write register to storage */
    if(!register_storage_write_holding_registers(storage, address, 1, &value)) { return UTIL_EBOUNDS; }

    /* Set default segment to payload before writing */
    if(!pb_set_default_seg_id(response, payload_seg_id)) {
        return pb_get_err(response);
    }

    /* Write response: FC, address, value */
    if(!pb_write_u8(response, "function_code", MODBUS_FC_WRITE_SINGLE_REGISTER)
       || !pb_write_u16_be(response, "address", address) || !pb_write_u16_be(response, "value", value)) {
        return pb_get_err(response);
    }

    /* Build response header (echo request as response) */
    modbus_build_response_header(response, req_header, payload_seg_id);

    pdlog(LOG_MODULE_MODBUS_PROTOCOL, LOG_LEVEL_DETAIL, "Wrote single register at address %u = 0x%04X", address, value);
    return UTIL_OK;
}

/* Handle FC 0x0F: Write Multiple Coils */
static util_err_t handle_write_multiple_coils(data_reader_t *request, packet_builder_t *response, pb_segment_id_t payload_seg_id,
                                              const mbap_header_t *req_header, register_storage_t *storage) {
    uint16_t start_address, count;
    uint8_t byte_count;

    /* Read request parameters */
    if(!data_reader_read_u16_be(request, "start_address", &start_address) || !data_reader_read_u16_be(request, "count", &count)
       || !data_reader_read_u8(request, "byte_count", &byte_count)) {
        return UTIL_EINVAL;
    }

    /* Validate count and byte count */
    if(count == 0 || count > MODBUS_MAX_WRITE_COILS || byte_count != ((count + 7) / 8)) { return UTIL_EINVAL; }

    /* Read coil data from request */
    uint8_t coil_data[MODBUS_MAX_WRITE_COILS / 8];
    if(!data_reader_read_bytes(request, "coil_data", coil_data, byte_count)) {
        return UTIL_EINVAL;
    }

    /* Write coils to storage */
    if(!register_storage_write_coils(storage, start_address, count, coil_data)) { return UTIL_EBOUNDS; }

    /* Set default segment to payload before writing */
    if(!pb_set_default_seg_id(response, payload_seg_id)) {
        return pb_get_err(response);
    }

    /* Write response: FC, start_address, count */
    if(!pb_write_u8(response, "function_code", MODBUS_FC_WRITE_MULTIPLE_COILS)
       || !pb_write_u16_be(response, "start_address", start_address) || !pb_write_u16_be(response, "count", count)) {
        return pb_get_err(response);
    }

    /* Build response header */
    modbus_build_response_header(response, req_header, payload_seg_id);

    pdlog(LOG_MODULE_MODBUS_PROTOCOL, LOG_LEVEL_DETAIL, "Wrote %u coils starting at address %u", count, start_address);
    return UTIL_OK;
}

/* Handle FC 0x10: Write Multiple Registers */
static util_err_t handle_write_multiple_registers(data_reader_t *request, packet_builder_t *response, pb_segment_id_t payload_seg_id,
                                                  const mbap_header_t *req_header, register_storage_t *storage) {
    uint16_t start_address, count;
    uint8_t byte_count;

    /* Read request parameters */
    if(!data_reader_read_u16_be(request, "start_address", &start_address) || !data_reader_read_u16_be(request, "count", &count)
       || !data_reader_read_u8(request, "byte_count", &byte_count)) {
        return UTIL_EINVAL;
    }

    /* Validate count and byte count */
    if(count == 0 || count > MODBUS_MAX_WRITE_REGISTERS || byte_count != (count * 2)) { return UTIL_EINVAL; }

    /* Allocate temporary buffer for register values */
    uint16_t *register_data = malloc(count * sizeof(uint16_t));
    if(!register_data) { return UTIL_ERESOURCE; }

    /* Read register values from request (big-endian) */
    for(uint16_t i = 0; i < count; i++) {
        if(!data_reader_read_u16_be(request, "register", &register_data[i])) {
            free(register_data);
            return UTIL_EINVAL;
        }
    }

    /* Write registers to storage */
    bool success = register_storage_write_holding_registers(storage, start_address, count, register_data);
    free(register_data);

    if(!success) { return UTIL_EBOUNDS; }

    /* Set default segment to payload before writing */
    if(!pb_set_default_seg_id(response, payload_seg_id)) {
        return pb_get_err(response);
    }

    /* Write response: FC, start_address, count */
    if(!pb_write_u8(response, "function_code", MODBUS_FC_WRITE_MULTIPLE_REGISTERS)
       || !pb_write_u16_be(response, "start_address", start_address) || !pb_write_u16_be(response, "count", count)) {
        return pb_get_err(response);
    }

    /* Build response header */
    modbus_build_response_header(response, req_header, payload_seg_id);

    pdlog(LOG_MODULE_MODBUS_PROTOCOL, LOG_LEVEL_DETAIL, "Wrote %u registers starting at address %u", count, start_address);
    return UTIL_OK;
}

/* Main request dispatcher */
util_err_t modbus_process_request(uint8_t function_code, data_reader_t *request, packet_builder_t *response,
                                  pb_segment_id_t payload_seg_id, const mbap_header_t *req_header, register_storage_t *storage) {
    pdlog(LOG_MODULE_MODBUS_PROTOCOL, LOG_LEVEL_DETAIL, "Processing Modbus request with function code: 0x%02X", function_code);
    pdlog_dr_bytes(LOG_MODULE_MODBUS_PROTOCOL, LOG_LEVEL_DETAIL, request);

    if(!request || !response || !req_header || !storage) {
        pdlog(LOG_MODULE_MODBUS_PROTOCOL, LOG_LEVEL_WARN, "NULL pointer(s) to process Modbus request");
        return UTIL_ENULL;
    }

    util_err_t err = UTIL_OK;

    switch(function_code) {
        case MODBUS_FC_READ_COILS:
            pdlog(LOG_MODULE_MODBUS_PROTOCOL, LOG_LEVEL_DETAIL, "Handling Read Coils request");
            err = handle_read_coils(request, response, payload_seg_id, req_header, storage);
            break;

        case MODBUS_FC_READ_DISCRETE_INPUTS:
            pdlog(LOG_MODULE_MODBUS_PROTOCOL, LOG_LEVEL_DETAIL, "Handling Read Discrete Inputs request");
            err = handle_read_discrete_inputs(request, response, payload_seg_id, req_header, storage);
            break;

        case MODBUS_FC_READ_HOLDING_REGISTERS:
            pdlog(LOG_MODULE_MODBUS_PROTOCOL, LOG_LEVEL_DETAIL, "Handling Read Holding Registers request");
            err = handle_read_holding_registers(request, response, payload_seg_id, req_header, storage);
            break;

        case MODBUS_FC_READ_INPUT_REGISTERS:
            pdlog(LOG_MODULE_MODBUS_PROTOCOL, LOG_LEVEL_DETAIL, "Handling Read Input Registers request");
            err = handle_read_input_registers(request, response, payload_seg_id, req_header, storage);
            break;

        case MODBUS_FC_WRITE_SINGLE_COIL:
            pdlog(LOG_MODULE_MODBUS_PROTOCOL, LOG_LEVEL_DETAIL, "Handling Write Single Coil request");
            err = handle_write_single_coil(request, response, payload_seg_id, req_header, storage);
            break;

        case MODBUS_FC_WRITE_SINGLE_REGISTER:
            pdlog(LOG_MODULE_MODBUS_PROTOCOL, LOG_LEVEL_DETAIL, "Handling Write Single Register request");
            err = handle_write_single_register(request, response, payload_seg_id, req_header, storage);
            break;

        case MODBUS_FC_WRITE_MULTIPLE_COILS:
            pdlog(LOG_MODULE_MODBUS_PROTOCOL, LOG_LEVEL_DETAIL, "Handling Write Multiple Coils request");
            err = handle_write_multiple_coils(request, response, payload_seg_id, req_header, storage);
            break;

        case MODBUS_FC_WRITE_MULTIPLE_REGISTERS:
            pdlog(LOG_MODULE_MODBUS_PROTOCOL, LOG_LEVEL_DETAIL, "Handling Write Multiple Registers request");
            err = handle_write_multiple_registers(request, response, payload_seg_id, req_header, storage);
            break;

        default:
            pdlog(LOG_MODULE_MODBUS_PROTOCOL, LOG_LEVEL_WARN, "Unsupported function code: 0x%02X", function_code);
            return UTIL_ENOTSUPPORTED;
    }

    /* Build exception response if handler failed */
    if(err != UTIL_OK) { modbus_build_exception_response(response, req_header, function_code, err); }

    pdlog(LOG_MODULE_MODBUS_PROTOCOL, LOG_LEVEL_DETAIL, "Built Modbus response:");
    pdlog_pb_bytes(LOG_MODULE_MODBUS_PROTOCOL, LOG_LEVEL_DETAIL, response, pb_get_compacted_segment_id(response));

    return err;
}
