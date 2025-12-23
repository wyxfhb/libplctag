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

#include "register_storage.h"
#include "log.h"
#include <stdlib.h>
#include <string.h>

register_storage_t *register_storage_create(size_t num_coils, size_t num_discrete_inputs, size_t num_holding_registers,
                                            size_t num_input_registers) {
    register_storage_t *storage = calloc(1, sizeof(*storage));
    if(!storage) {
        pdlog(LOG_MODULE_REGISTER_STORAGE, LOG_LEVEL_ERROR, "Failed to allocate register storage");
        return NULL;
    }

    // Create coil array
    if(num_coils > 0) {
        storage->coils = modbus_bitarray_create(num_coils);
        if(!storage->coils) {
            pdlog(LOG_MODULE_REGISTER_STORAGE, LOG_LEVEL_ERROR, "Failed to allocate coils (num_coils=%zu)", num_coils);
            free(storage);
            return NULL;
        }
    }

    // Create discrete inputs array
    if(num_discrete_inputs > 0) {
        storage->discrete_inputs = modbus_bitarray_create(num_discrete_inputs);
        if(!storage->discrete_inputs) {
            pdlog(LOG_MODULE_REGISTER_STORAGE, LOG_LEVEL_ERROR, "Failed to allocate discrete inputs (num_discrete_inputs=%zu)",
                  num_discrete_inputs);
            modbus_bitarray_destroy(storage->coils);
            free(storage);
            return NULL;
        }
    }

    // Create holding registers array
    if(num_holding_registers > 0) {
        storage->holding_registers = calloc(num_holding_registers, sizeof(uint16_t));
        if(!storage->holding_registers) {
            pdlog(LOG_MODULE_REGISTER_STORAGE, LOG_LEVEL_ERROR,
                  "Failed to allocate holding registers (num_holding_registers=%zu)", num_holding_registers);
            modbus_bitarray_destroy(storage->coils);
            modbus_bitarray_destroy(storage->discrete_inputs);
            free(storage);
            return NULL;
        }
        storage->num_holding_registers = num_holding_registers;
    }

    // Create input registers array
    if(num_input_registers > 0) {
        storage->input_registers = calloc(num_input_registers, sizeof(uint16_t));
        if(!storage->input_registers) {
            pdlog(LOG_MODULE_REGISTER_STORAGE, LOG_LEVEL_ERROR, "Failed to allocate input registers (num_input_registers=%zu)",
                  num_input_registers);
            modbus_bitarray_destroy(storage->coils);
            modbus_bitarray_destroy(storage->discrete_inputs);
            free(storage->holding_registers);
            free(storage);
            return NULL;
        }
        storage->num_input_registers = num_input_registers;
    }

    pdlog(LOG_MODULE_REGISTER_STORAGE, LOG_LEVEL_DETAIL, "Register storage created: coils=%zu, di=%zu, hr=%zu, ir=%zu", num_coils,
          num_discrete_inputs, num_holding_registers, num_input_registers);

    return storage;
}

void register_storage_destroy(register_storage_t *storage) {
    if(storage) {
        modbus_bitarray_destroy(storage->coils);
        modbus_bitarray_destroy(storage->discrete_inputs);
        free(storage->holding_registers);
        free(storage->input_registers);
        free(storage);
    }
}

bool register_storage_read_coils(register_storage_t *storage, uint16_t address, uint16_t count, uint8_t *out_bytes) {
    if(!storage || !storage->coils || !out_bytes) { return false; }

    return modbus_bitarray_read_bits(storage->coils, address, count, out_bytes);
}

bool register_storage_read_discrete_inputs(register_storage_t *storage, uint16_t address, uint16_t count, uint8_t *out_bytes) {
    if(!storage || !storage->discrete_inputs || !out_bytes) { return false; }

    return modbus_bitarray_read_bits(storage->discrete_inputs, address, count, out_bytes);
}

bool register_storage_write_coils(register_storage_t *storage, uint16_t address, uint16_t count, const uint8_t *in_bytes) {
    if(!storage || !storage->coils || !in_bytes) { return false; }

    return modbus_bitarray_write_bits(storage->coils, address, count, in_bytes);
}

bool register_storage_read_holding_registers(register_storage_t *storage, uint16_t address, uint16_t count,
                                             uint16_t *out_values) {
    if(!storage || !storage->holding_registers || !out_values) { return false; }

    if(address + count > storage->num_holding_registers) { return false; }

    memcpy(out_values, &storage->holding_registers[address], count * sizeof(uint16_t));
    return true;
}

bool register_storage_write_holding_registers(register_storage_t *storage, uint16_t address, uint16_t count,
                                              const uint16_t *in_values) {
    if(!storage || !storage->holding_registers || !in_values) { return false; }

    if(address + count > storage->num_holding_registers) { return false; }

    memcpy(&storage->holding_registers[address], in_values, count * sizeof(uint16_t));
    return true;
}

bool register_storage_read_input_registers(register_storage_t *storage, uint16_t address, uint16_t count, uint16_t *out_values) {
    if(!storage || !storage->input_registers || !out_values) { return false; }

    if(address + count > storage->num_input_registers) { return false; }

    memcpy(out_values, &storage->input_registers[address], count * sizeof(uint16_t));
    return true;
}
