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


#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "err.h"
#include "log.h"

/*
 * Data Reader API (Linear)
 * Simple sequential reader.
 */

#define DATA_READER_INVALID_SIZE SIZE_MAX

typedef struct data_reader_s {
    uint8_t *base;
    size_t capacity;
    size_t read_pos;
    size_t write_pos;
    const char *err_field;
    util_err_t err;
} data_reader_t;

/* initialization */
data_reader_t data_reader_init(uint8_t *base, size_t capacity);
bool data_reader_reset(data_reader_t *r);

/* state and errors */
util_err_t data_reader_get_err(data_reader_t *r);
bool data_reader_set_err(data_reader_t *r, util_err_t err);

const char *data_reader_get_err_field(data_reader_t *r);
bool data_reader_set_err_field(data_reader_t *r, const char *field_name);

/* write operations for filling the buffer */
size_t data_reader_write_space(data_reader_t *r);
uint8_t *data_reader_write_ptr(data_reader_t *r);
bool data_reader_write_advance(data_reader_t *r, size_t len);

/* buffer management */
size_t data_reader_read_size(data_reader_t *r);
bool data_reader_compact(data_reader_t *r);

/* data accessors*/
bool data_reader_read_u8(data_reader_t *r, const char *field_name, uint8_t *val);

bool data_reader_read_u16_le(data_reader_t *r, const char *field_name, uint16_t *val);
bool data_reader_read_u32_le(data_reader_t *r, const char *field_name, uint32_t *val);
bool data_reader_read_u64_le(data_reader_t *r, const char *field_name, uint64_t *val);

bool data_reader_read_u16_be(data_reader_t *r, const char *field_name, uint16_t *val);
bool data_reader_read_u32_be(data_reader_t *r, const char *field_name, uint32_t *val);
bool data_reader_read_u64_be(data_reader_t *r, const char *field_name, uint64_t *val);

bool data_reader_read_bytes(data_reader_t *r, const char *field_name, uint8_t *out, size_t len);


/* Logging */

/**
 * @brief Dump bytes from a data reader to the log (internal).
 *
 * @param func
 * @param line_num
 * @param lvl
 * @param modules
 * @param dr
 */
void log_dr_bytes_impl(const char *func, int line_num, log_level_t lvl, log_module_mask_t modules, data_reader_t *dr);
/* Logging macros */

/**
 * @brief Log data reader buffer contents if enabled for the given modules and level.
 *
 * @param modules Bitmask of modules this log applies to
 * @param level Log level
 * @param buf Buffer to dump
 */
#define pdlog_dr_bytes(modules, level, buf)                                                              \
    do {                                                                                                 \
        if(log_is_enabled(modules, level)) log_dr_bytes_impl(__func__, __LINE__, level, modules, (buf)); \
    } while(0)
