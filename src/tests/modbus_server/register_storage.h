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
#include <stddef.h>
#include "modbus_bitarray.h"

/**
 * @brief Storage for Modbus registers and coils
 */
typedef struct {
    modbus_bitarray_t *coils;            // Read/write bits
    modbus_bitarray_t *discrete_inputs;  // Read-only bits
    uint16_t *holding_registers;         // Read/write 16-bit values
    size_t num_holding_registers;
    uint16_t *input_registers;  // Read-only 16-bit values
    size_t num_input_registers;
} register_storage_t;

/**
 * @brief Create register storage
 */
register_storage_t *register_storage_create(size_t num_coils, size_t num_discrete_inputs, size_t num_holding_registers,
                                            size_t num_input_registers);

/**
 * @brief Destroy register storage
 */
void register_storage_destroy(register_storage_t *storage);

/**
 * @brief Read coils into a byte array (LSB-first per Modbus spec)
 */
bool register_storage_read_coils(register_storage_t *storage, uint16_t address, uint16_t count, uint8_t *out_bytes);

/**
 * @brief Read discrete inputs into a byte array
 */
bool register_storage_read_discrete_inputs(register_storage_t *storage, uint16_t address, uint16_t count, uint8_t *out_bytes);

/**
 * @brief Write coils from a byte array
 */
bool register_storage_write_coils(register_storage_t *storage, uint16_t address, uint16_t count, const uint8_t *in_bytes);

/**
 * @brief Read holding registers
 */
bool register_storage_read_holding_registers(register_storage_t *storage, uint16_t address, uint16_t count, uint16_t *out_values);

/**
 * @brief Write holding registers
 */
bool register_storage_write_holding_registers(register_storage_t *storage, uint16_t address, uint16_t count,
                                              const uint16_t *in_values);

/**
 * @brief Read input registers
 */
bool register_storage_read_input_registers(register_storage_t *storage, uint16_t address, uint16_t count, uint16_t *out_values);
