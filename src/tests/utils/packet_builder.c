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


#include "packet_builder.h"
#include "err.h"
#include <string.h>
#include <stdio.h>

#define BYTE_MASK 0xFF

/* Forward declaration */
static pb_segment_id_t find_segment_index(packet_builder_t *b, pb_segment_id_t seg_id);

packet_builder_t pb_init(uint8_t *mem, size_t capacity) {
    packet_builder_t b = {.base = mem,
                          .capacity = capacity,
                          .num_segments = 0,
                          .compacted = false,
                          .err = 0,
                          .default_write_seg_id = PB_INVALID_SEGMENT_ID,
                          .err_field = NULL};
    /* Initialize all segment IDs to invalid */
    for(int i = 0; i < PB_MAX_SEGMENTS; i++) {
        b.segment_ids[i] = PB_INVALID_SEGMENT_ID;
        b.limits[i].limit = 0;
        b.limits[i].len = 0;
    }
    return b;
}


bool pb_reset(packet_builder_t *b) {
    if(!b) { return false; }

    b->num_segments = 0;
    b->compacted = false;
    b->err = 0;
    b->err_field = NULL;
    b->default_write_seg_id = PB_INVALID_SEGMENT_ID;

    /* Initialize all segment IDs and limits to invalid/zero */
    for(int i = 0; i < PB_MAX_SEGMENTS; i++) {
        b->segment_ids[i] = PB_INVALID_SEGMENT_ID;
        b->limits[i].limit = 0;
        b->limits[i].len = 0;
    }

    return true;
}

uint8_t *pb_get_base_ptr(packet_builder_t *b) {
    if(!b) { return NULL; }

    if(!b->compacted) { return NULL; }

    return b->base;
}

uint8_t *pb_unconsumed_ptr(packet_builder_t *b) {
    if(!b) { return NULL; }

    if(!b->compacted) { return NULL; }

    return b->base + b->limits[0].len;
}

size_t pb_get_total_len(packet_builder_t *b) {
    if(!b) { return PB_INVALID_SEGMENT_SIZE; }

    size_t total = 0;
    for(uint8_t i = 0; i < b->num_segments; i++) { total += b->limits[i].len; }

    return total;
}

size_t pb_unconsumed_size(packet_builder_t *b) {
    if(!b) { return PB_INVALID_SEGMENT_SIZE; }

    if(!b->compacted) { return PB_INVALID_SEGMENT_SIZE; }

    return b->limits[0].limit - b->limits[0].len;
}


bool pb_compact(packet_builder_t *b, pb_segment_id_t packet_seg_id) {
    if(!b) { return false; }

    if(b->err) { return false; }

    if(b->compacted) {
        /* already compacted - check if trying to use different segment ID */
        if(b->segment_ids[0] != packet_seg_id) {
            b->err = UTIL_EDUPLICATE;
            return false;
        }
        return true;
    }

    size_t dest_offset = 0;
    size_t src_offset = 0;

    for(uint8_t i = 0; i < b->num_segments; i++) {
        size_t limit = b->limits[i].limit;
        size_t len = b->limits[i].len;

        /* Move data to the front (memmove handles overlapping regions safely) */
        if(len > 0 && src_offset != dest_offset) { memmove(b->base + dest_offset, b->base + src_offset, len); }
        dest_offset += len;
        src_offset += limit;

        /* zero out existing segments*/
        b->limits[i].limit = 0;
        b->limits[i].len = 0;
    }

    /* create a single segment with all the data. */
    if(b->num_segments > 0) {
        b->limits[0].limit = dest_offset;
        b->limits[0].len = dest_offset;
        b->segment_ids[0] = packet_seg_id;
        b->num_segments = 1;
    }

    b->compacted = true;

    return true;
}

bool pb_consume_compacted(packet_builder_t *b, size_t size) {
    if(!b) { return false; }

    if(b->err) { return false; }

    /* we can only do this on compacted packet builders */
    if(!b->compacted) {
        b->err = UTIL_EINVAL;
        return false;
    }

    if(size > b->limits[0].limit - b->limits[0].len) {
        b->err = UTIL_EBOUNDS;
        return false;
    }

    b->limits[0].len += size;

    return true;
}

bool pb_is_compacted(packet_builder_t *b) {
    if(!b) { return false; }

    return b->compacted;
}

pb_segment_id_t pb_get_compacted_segment_id(packet_builder_t *b) {
    if(!b) { return PB_INVALID_SEGMENT_ID; }

    if(!b->compacted) { return PB_INVALID_SEGMENT_ID; }

    return b->segment_ids[0];
}

bool pb_set_default_seg_id(packet_builder_t *b, pb_segment_id_t seg_id) {
    if(!b) { return false; }

    if(b->err) { return false; }

    pb_segment_id_t seg_index = find_segment_index(b, seg_id);
    if(seg_index == PB_INVALID_SEGMENT_ID) {
        b->err = UTIL_ENOTFOUND;
        return false;
    }

    b->default_write_seg_id = seg_id;

    return true;
}

bool pb_add_segment(packet_builder_t *b, pb_segment_id_t new_seg_id, size_t max_size) {
    if(!b) { return false; }

    if(b->err) { return false; }

    if(b->compacted) {
        b->err = UTIL_EINVAL;
        return false;
    }

    if(b->num_segments >= PB_MAX_SEGMENTS) {
        b->err = UTIL_ERESOURCE;
        return false;
    }

    /* Check if segment ID is already in use */
    for(uint8_t i = 0; i < b->num_segments; i++) {
        if(b->segment_ids[i] == new_seg_id) {
            b->err = UTIL_EDUPLICATE;
            return false;
        }
    }

    /* Scan existing segments to check if we have room */
    size_t used = 0;
    for(uint8_t i = 0; i < b->num_segments; i++) { used += b->limits[i].limit; }

    if(used + max_size > b->capacity) {
        b->err = UTIL_EBOUNDS;
        return false;
    }

    uint8_t id = b->num_segments++;
    b->segment_ids[id] = new_seg_id;
    b->limits[id].limit = max_size;
    b->limits[id].len = 0;

    return true;
}

size_t pb_segment_len(packet_builder_t *b, pb_segment_id_t seg_id) {
    if(!b) { return PB_INVALID_SEGMENT_SIZE; }

    pb_segment_id_t seg_index = find_segment_index(b, seg_id);
    if(seg_index == PB_INVALID_SEGMENT_ID) {
        b->err = UTIL_ENOTFOUND;
        return PB_INVALID_SEGMENT_SIZE;
    }

    /* For compacted: return remaining data (limit - len) */
    /* For non-compacted: return data written (len) */
    if(b->compacted) {
        return b->limits[seg_index].limit - b->limits[seg_index].len;
    } else {
        return b->limits[seg_index].len;
    }
}


util_err_t pb_get_err(packet_builder_t *b) {
    if(!b) { return UTIL_EINVAL; }

    return b->err;
}

bool pb_set_err(packet_builder_t *b, util_err_t err) {
    if(!b) { return false; }

    b->err = err;

    return true;
}


const char *pb_get_err_field(packet_builder_t *b) {
    if(!b) { return "--NO FIELD--"; }
    return b->err_field;
}

bool pb_set_err_field(packet_builder_t *b, const char *field_name) {
    if(!b) { return false; }
    b->err_field = field_name;
    return true;
}

/* Internal helper to find segment index by ID (linear search through segment_ids array) */
static pb_segment_id_t find_segment_index(packet_builder_t *b, pb_segment_id_t seg_id) {
    if(!b) { return PB_INVALID_SEGMENT_ID; }

    for(uint8_t i = 0; i < b->num_segments; i++) {
        if(b->segment_ids[i] == seg_id) { return i; }
    }

    return PB_INVALID_SEGMENT_ID;
}

/**
 * @brief sum the lengths of segments starting from start_seg_id up to the last segment
 *
 * @param b
 * @param start_seg_id
 * @return size_t
 */
size_t pb_sum_segment_len(packet_builder_t *b, pb_segment_id_t start_seg_id) {
    if(!b) { return PB_INVALID_SEGMENT_SIZE; }

    pb_segment_id_t start_index = find_segment_index(b, start_seg_id);
    if(start_index == PB_INVALID_SEGMENT_ID) {
        b->err = UTIL_ENOTFOUND;
        return PB_INVALID_SEGMENT_SIZE;
    }

    size_t total = 0;
    for(uint8_t i = start_index; i < b->num_segments; i++) { total += b->limits[i].len; }

    return total;
}

/* Internal helper to check bounds and find segment index */
static pb_segment_id_t check_bounds(packet_builder_t *b, const char *field_name, size_t write_len) {
    if(!b) { return PB_INVALID_SEGMENT_ID; }

    if(b->err) { return PB_INVALID_SEGMENT_ID; }

    if(b->compacted) {
        b->err = UTIL_ENOTSUPPORTED;
        pb_set_err_field(b, field_name);
        return PB_INVALID_SEGMENT_ID;
    }

    if(b->default_write_seg_id == PB_INVALID_SEGMENT_ID) {
        b->err = UTIL_ENOTFOUND;
        pb_set_err_field(b, field_name);
        return PB_INVALID_SEGMENT_ID;
    }

    pb_segment_id_t seg_index = find_segment_index(b, b->default_write_seg_id);
    if(seg_index == PB_INVALID_SEGMENT_ID) {
        b->err = UTIL_ENOTFOUND;
        pb_set_err_field(b, field_name);
        return PB_INVALID_SEGMENT_ID;
    }

    if(b->limits[seg_index].len + write_len > b->limits[seg_index].limit) {
        b->err = UTIL_EBOUNDS;
        pb_set_err_field(b, field_name);
        return PB_INVALID_SEGMENT_ID;
    }

    return seg_index;
}

/* Internal helper to calculate start offset of a segment by index */
static size_t calc_segment_offset_by_index(packet_builder_t *b, pb_segment_id_t seg_index) {
    size_t offset = 0;
    for(uint8_t i = 0; i < seg_index; i++) { offset += b->limits[i].limit; }
    return offset;
}


bool pb_write_u8(packet_builder_t *b, const char *field_name, uint8_t val) {
    pb_segment_id_t seg_index = check_bounds(b, field_name, 1);
    if(seg_index == PB_INVALID_SEGMENT_ID) { return false; }

    size_t offset = calc_segment_offset_by_index(b, seg_index) + b->limits[seg_index].len;
    b->base[offset] = val;

    b->limits[seg_index].len += 1;

    return true;
}

bool pb_write_u16_le(packet_builder_t *b, const char *field_name, uint16_t val) {
    size_t write_size = sizeof(uint16_t);

    pb_segment_id_t seg_index = check_bounds(b, field_name, write_size);
    if(seg_index == PB_INVALID_SEGMENT_ID) { return false; }

    size_t offset = calc_segment_offset_by_index(b, seg_index) + b->limits[seg_index].len;
    uint8_t *p = b->base + offset;
    for(size_t i = 0; i < write_size; i++) { p[i] = (uint8_t)((val >> (i * 8)) & BYTE_MASK); }

    b->limits[seg_index].len += write_size;

    return true;
}

bool pb_write_u32_le(packet_builder_t *b, const char *field_name, uint32_t val) {
    size_t write_size = sizeof(uint32_t);

    pb_segment_id_t seg_index = check_bounds(b, field_name, write_size);
    if(seg_index == PB_INVALID_SEGMENT_ID) { return false; }

    size_t offset = calc_segment_offset_by_index(b, seg_index) + b->limits[seg_index].len;
    uint8_t *p = b->base + offset;
    for(size_t i = 0; i < write_size; i++) { p[i] = (uint8_t)((val >> (i * 8)) & BYTE_MASK); }

    b->limits[seg_index].len += write_size;

    return true;
}

bool pb_write_u64_le(packet_builder_t *b, const char *field_name, uint64_t val) {
    size_t write_size = sizeof(uint64_t);

    pb_segment_id_t seg_index = check_bounds(b, field_name, write_size);
    if(seg_index == PB_INVALID_SEGMENT_ID) { return false; }

    size_t offset = calc_segment_offset_by_index(b, seg_index) + b->limits[seg_index].len;
    uint8_t *p = b->base + offset;
    for(size_t i = 0; i < write_size; i++) { p[i] = (uint8_t)((val >> (i * 8)) & BYTE_MASK); }

    b->limits[seg_index].len += write_size;

    return true;
}

bool pb_write_u16_be(packet_builder_t *b, const char *field_name, uint16_t val) {
    size_t write_size = sizeof(uint16_t);

    pb_segment_id_t seg_index = check_bounds(b, field_name, write_size);
    if(seg_index == PB_INVALID_SEGMENT_ID) { return false; }

    size_t offset = calc_segment_offset_by_index(b, seg_index) + b->limits[seg_index].len;
    uint8_t *p = b->base + offset;
    for(size_t i = 0; i < write_size; i++) { p[(write_size - 1) - i] = (uint8_t)((val >> (i * 8)) & BYTE_MASK); }

    b->limits[seg_index].len += write_size;

    return true;
}

bool pb_write_u32_be(packet_builder_t *b, const char *field_name, uint32_t val) {
    size_t write_size = sizeof(uint32_t);

    pb_segment_id_t seg_index = check_bounds(b, field_name, write_size);
    if(seg_index == PB_INVALID_SEGMENT_ID) { return false; }

    size_t offset = calc_segment_offset_by_index(b, seg_index) + b->limits[seg_index].len;
    uint8_t *p = b->base + offset;
    for(size_t i = 0; i < write_size; i++) { p[(write_size - 1) - i] = (uint8_t)((val >> (i * 8)) & BYTE_MASK); }

    b->limits[seg_index].len += write_size;

    return true;
}

bool pb_write_u64_be(packet_builder_t *b, const char *field_name, uint64_t val) {
    size_t write_size = sizeof(uint64_t);

    pb_segment_id_t seg_index = check_bounds(b, field_name, write_size);
    if(seg_index == PB_INVALID_SEGMENT_ID) { return false; }

    size_t offset = calc_segment_offset_by_index(b, seg_index) + b->limits[seg_index].len;
    uint8_t *p = b->base + offset;
    for(size_t i = 0; i < write_size; i++) { p[(write_size - 1) - i] = (uint8_t)((val >> (i * 8)) & BYTE_MASK); }

    b->limits[seg_index].len += write_size;

    return true;
}

bool pb_write_bytes(packet_builder_t *b, const char *field_name, uint8_t *data, size_t len) {
    pb_segment_id_t seg_index = check_bounds(b, field_name, len);
    if(seg_index == PB_INVALID_SEGMENT_ID) { return false; }

    size_t offset = calc_segment_offset_by_index(b, seg_index) + b->limits[seg_index].len;
    memcpy(b->base + offset, data, len);
    b->limits[seg_index].len += len;

    return true;
}


#define COLUMNS (16)

/**
 * @brief Log a buffer's contents as hex.
 *
 * @param func name of the function in which the log is generated
 * @param line_num line number in the source file
 * @param lvl log level
 * @param modules bitmask of modules this log applies to
 * @param buf buffer containing the bytes to log
 * @param seg_id segment ID to dump
 */
void log_pb_bytes_impl(const char *func, int line_num, log_level_t lvl, log_module_mask_t modules, packet_builder_t *pb,
                       pb_segment_id_t seg_id) {
    if(!pb) {
        log_impl(func, line_num, lvl, modules, "<null>");
        return;
    }

    if(!pb->base) {
        log_impl(func, line_num, lvl, modules, "<null base>");
        return;
    }

    /* Find the segment with this ID */
    pb_segment_id_t seg_index = find_segment_index(pb, seg_id);
    if(seg_index == PB_INVALID_SEGMENT_ID) {
        log_impl(func, line_num, lvl, modules, "<invalid segment ID>");
        return;
    }

    /* get the offset to the segment */
    size_t offset = calc_segment_offset_by_index(pb, seg_index);

    uint8_t *data = pb->base + offset;

    size_t data_len = pb->limits[seg_index].len;
    if(data_len == 0) {
        log_impl(func, line_num, lvl, modules, "<empty>");
        return;
    }

    size_t total_rows = (data_len + (COLUMNS - 1)) / COLUMNS;
    for(size_t row = 0; row < total_rows; ++row) {
        char row_buf[(COLUMNS * 3) + 6] = {0};
        char *p = row_buf;

        p += sprintf(p, "%04zx:", row * COLUMNS);

        size_t start = row * COLUMNS;
        size_t end = start + COLUMNS;

        if(end > data_len) { end = data_len; }

        for(size_t i = start; i < end; ++i) { p += sprintf(p, " %02x", (unsigned)data[i]); }

        log_impl(func, line_num, lvl, modules, "%s", row_buf);
    }
}
