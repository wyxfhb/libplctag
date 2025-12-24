#pragma once

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


#include <stdint.h>
#include <stdbool.h>
#include "data_reader.h"
#include "err.h"
#include "packet_builder.h"
#include "register_storage.h"

/* Modbus TCP Application Protocol (MBAP) Header */
#define MBAP_HEADER_SIZE 7

/* Modbus Function Codes */
#define MODBUS_FC_READ_COILS 0x01
#define MODBUS_FC_READ_DISCRETE_INPUTS 0x02
#define MODBUS_FC_READ_HOLDING_REGISTERS 0x03
#define MODBUS_FC_READ_INPUT_REGISTERS 0x04
#define MODBUS_FC_WRITE_SINGLE_COIL 0x05
#define MODBUS_FC_WRITE_SINGLE_REGISTER 0x06
#define MODBUS_FC_WRITE_MULTIPLE_COILS 0x0F
#define MODBUS_FC_WRITE_MULTIPLE_REGISTERS 0x10

/* Modbus Exception Codes */
#define MODBUS_EXCEPTION_ILLEGAL_FUNCTION 0x01
#define MODBUS_EXCEPTION_ILLEGAL_ADDRESS 0x02
#define MODBUS_EXCEPTION_ILLEGAL_DATA_VALUE 0x03
#define MODBUS_EXCEPTION_DEVICE_FAILURE 0x04

/* Maximum Modbus PDU size (260 bytes) */
#define MODBUS_MAX_PDU_SIZE 260

/* Maximum ADU (MBAP + PDU) size */
#define MODBUS_MAX_ADU_SIZE (MBAP_HEADER_SIZE + MODBUS_MAX_PDU_SIZE)

#define MODBUS_MAX_READ_RESPONSE_BYTES 250

#define MODBUS_MAX_READ_COILS 2000
#define MODBUS_MAX_READ_DISCRETE_INPUTS 2000
#define MODBUS_MAX_READ_REGISTERS 125

#define MODBUS_MAX_WRITE_COILS 1968
#define MODBUS_MAX_WRITE_REGISTERS 123

/* MBAP Header structure */
typedef struct {
    uint16_t transaction_id;
    uint16_t protocol_id;
    uint16_t length;
    uint8_t unit_id;
} mbap_header_t;

/**
 * @brief Parse MBAP header from buffer
 * @param request Data reader to read from (will advance read cursor)
 * @param header OUT: Parsed MBAP header
 * @return UTIL_OK on success, error code on failure
 */
util_err_t modbus_parse_mbap_header(data_reader_t *request, mbap_header_t *header);

/**
 * @brief Build MBAP header and response PDU header
 * @param response Buffer to write to
 * @param req_header Request header (for transaction_id, etc.)
 * @param pdu_length Length of response PDU (not including MBAP)
 * @return UTIL_OK on success, error code on failure
 */
util_err_t modbus_build_response_header(packet_builder_t *response, const mbap_header_t *req_header,
                                        pb_segment_id_t payload_seg_id);

/**
 * @brief Build Modbus exception response
 * @param response Buffer to write to (must be reset)
 * @param req_header Request header
 * @param function_code Original function code
 * @param error Error code (will be mapped to Modbus exception)
 */
void modbus_build_exception_response(packet_builder_t *response, const mbap_header_t *req_header, uint8_t function_code,
                                     util_err_t error);

/**
 * @brief Process a Modbus request and generate response
 * @param function_code Function code from request
 * @param request Buffer with request PDU (positioned after FC)
 * @param response Buffer for response (will be written to)
 * @param req_header Request MBAP header
 * @param storage Register storage
 * @return UTIL_OK on success, error code otherwise
 */
util_err_t modbus_process_request(uint8_t function_code, data_reader_t *request, packet_builder_t *response,
                                  pb_segment_id_t payload_seg_id, const mbap_header_t *req_header, register_storage_t *storage);
