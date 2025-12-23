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

#define BYTE_MASK 0xFF

packet_builder_t pb_init(uint8_t *mem, size_t capacity) {
    packet_builder_t b = {.base = mem, .capacity = capacity, .num_segments = 0, .compacted = false, .err = 0};
    return b;
}


bool pb_consume_compacted(packet_builder_t *b, size_t size) {
    if(!b) { return false; }

    if(b->err) { return false; }

    /* we can only do this on compacted packet builders */
    if(!b->compacted) {
        b->err = UTIL_EINVAL;
        return false;
    }

    if(size > b->segments[0].limit - b->segments[0].len) {
        b->err = UTIL_EBOUNDS;
        return false;
    }

    b->segments[0].len += size;

    return true;

    uint8_t *pb_get_base_ptr(packet_builder_t * b) {
        if(!b) { return NULL; }

        /* return the base pointer depending on whether the packet is compacted */
        return (b->compacted ? b->base + b->segments[0].len : b->base);
    }

    size_t pb_get_total_len(packet_builder_t * b) {
        if(!b) { return PB_INVALID_SEGMENT_SIZE; }

        size_t total = 0;
        for(uint8_t i = 0; i < b->num_segments; i++) {
            total += (b->compacted ? b->segments[i].limit - b->segments[i].len : b->segments[i].len);
        }

        return total;
    }


    size_t pb_compact(packet_builder_t * b) {
        if(b->err) { return 0; }

        size_t dest_offset = 0;
        size_t src_offset = 0;

        if(b->compacted) {
            /* already compacted */
            return pb_get_total_len(b);
        }

        for(uint8_t i = 0; i < b->num_segments; i++) {
            size_t limit = b->segments[i].limit;
            size_t len = b->segments[i].len;

            /* Move data to the front (memmove handles overlapping regions safely) */
            if(len > 0 && src_offset != dest_offset) { memmove(b->base + dest_offset, b->base + src_offset, len); }
            dest_offset += len;
            src_offset += limit;

            /* zero out existing segments*/
            b->segments[i].limit = 0;
            b->segments[i].len = 0;
        }

        /* create a single segment with all the data. */
        if(b->num_segments > 0) {
            b->segments[0].limit = dest_offset;
            b->segments[0].len = dest_offset;
            b->num_segments = 1;
        }

        b->compacted = true;

        return dest_offset;
    }

    bool pb_consume_compacted(packet_builder_t * b, size_t size) {
        if(!b) { return false; }

        if(b->err) { return false; }

        /* we can only do this on compacted packet builders */
        if(!b->compacted) {
            b->err = UTIL_EINVAL;
            return false;
        }

        if(size > b->segments[0].limit - b->segments[0].len) {
            b->err = UTIL_EBOUNDS;
            return false;
        }

        b->segments[0].len += size;

        return true;
    }

    pb_segment_id_t pb_add_segment(packet_builder_t * b, size_t max_size) {
        if(!b) { return PB_INVALID_SEGMENT_ID; }

        if(b->err) { return PB_INVALID_SEGMENT_ID; }

        if(b->compacted) {
            b->err = UTIL_EINVAL;
            return PB_INVALID_SEGMENT_ID;
        }

        if(b->num_segments >= 8) {
            b->err = UTIL_EBOUNDS;
            return PB_INVALID_SEGMENT_ID;
        }

        /* Scan existing segments to check if we have room */
        size_t used = 0;
        for(uint8_t i = 0; i < b->num_segments; i++) { used += b->segments[i].limit; }

        if(used + max_size > b->capacity) {
            b->err = UTIL_EBOUNDS;
            return PB_INVALID_SEGMENT_ID;
        }

        pb_segment_id_t id = b->num_segments++;
        b->segments[id].limit = max_size;
        b->segments[id].len = 0;

        return id;
    }

    size_t pb_segment_len(packet_builder_t * b, pb_segment_id_t seg_id) {
        if(!b) { return PB_INVALID_SEGMENT_SIZE; }

        if(seg_id >= b->num_segments) {
            b->err = UTIL_EBOUNDS;
            return PB_INVALID_SEGMENT_SIZE;
        }
        return (b->compacted ? b->segments[seg_id].limit : b->segments[seg_id].len);
    }


    util_err_t pb_get_err(packet_builder_t * b) {
        if(!b) { return UTIL_EINVAL; }

        return b->err;
    }

    bool pb_set_err(packet_builder_t * b, int err) {
        if(!b) { return false; }

        b->err = err;

        return true;
    }

    /**
     * @brief sum the lengths of segments starting from start_seg_id up to the last segment
     *
     * @param b
     * @param start_seg_id
     * @return size_t
     */
    size_t pb_sum_segment_len(packet_builder_t * b, pb_segment_id_t start_seg_id) {
        if(!b) { return PB_INVALID_SEGMENT_SIZE; }

        if(start_seg_id >= b->num_segments) {
            b->err = UTIL_EBOUNDS;
            return PB_INVALID_SEGMENT_SIZE;
        }

        size_t total = 0;
        for(uint8_t i = start_seg_id; i < b->num_segments; i++) { total += b->segments[i].len; }

        return total;
    }

    /* Internal helper to check bounds */
    static bool check_bounds(packet_builder_t * b, pb_segment_id_t seg_id, size_t write_len) {
        if(!b) { return false; }

        if(b->err) { return false; }

        if(seg_id >= b->num_segments) {
            b->err = UTIL_EBOUNDS;
            return false;
        }

        if(b->segments[seg_id].len + write_len > b->segments[seg_id].limit) {
            b->err = UTIL_EBOUNDS;
            return false;
        }
        return true;
    }

    /* Internal helper to calculate start offset of a segment */
    static size_t calc_segment_offset(packet_builder_t * b, pb_segment_id_t seg_id) {
        size_t offset = 0;
        for(uint8_t i = 0; i < seg_id; i++) { offset += b->segments[i].limit; }
        return offset;
    }


    bool pb_write_u8(packet_builder_t * b, pb_segment_id_t seg_id, uint8_t val) {
        if(!check_bounds(b, seg_id, 1)) { return false; }

        size_t offset = calc_segment_offset(b, seg_id) + b->segments[seg_id].len;
        b->base[offset] = val;

        b->segments[seg_id].len += 1;

        return true;
    }

    bool pb_write_u16_le(packet_builder_t * b, pb_segment_id_t seg_id, uint16_t val) {
        size_t write_size = sizeof(uint16_t);

        if(!check_bounds(b, seg_id, write_size)) { return false; }

        size_t offset = calc_segment_offset(b, seg_id) + b->segments[seg_id].len;
        uint8_t *p = b->base + offset;
        for(int i = 0; i < write_size; i++) { p[i] = (uint8_t)((val >> (i * 8)) & BYTE_MASK); }

        b->segments[seg_id].len += write_size;

        return true;
    }

    bool pb_write_u32_le(packet_builder_t * b, pb_segment_id_t seg_id, uint32_t val) {
        size_t write_size = sizeof(uint32_t);

        if(!check_bounds(b, seg_id, write_size)) { return false; }

        size_t offset = calc_segment_offset(b, seg_id) + b->segments[seg_id].len;
        uint8_t *p = b->base + offset;
        for(int i = 0; i < write_size; i++) { p[i] = (uint8_t)((val >> (i * 8)) & BYTE_MASK); }

        b->segments[seg_id].len += write_size;

        return true;
    }

    bool pb_write_u64_le(packet_builder_t * b, pb_segment_id_t seg_id, uint64_t val) {
        size_t write_size = sizeof(uint64_t);

        if(!check_bounds(b, seg_id, write_size)) { return false; }

        size_t offset = calc_segment_offset(b, seg_id) + b->segments[seg_id].len;
        uint8_t *p = b->base + offset;
        for(int i = 0; i < write_size; i++) { p[i] = (uint8_t)((val >> (i * 8)) & BYTE_MASK); }

        b->segments[seg_id].len += write_size;

        return true;
    }

    bool pb_write_u16_be(packet_builder_t * b, pb_segment_id_t seg_id, uint16_t val) {
        size_t write_size = sizeof(uint16_t);

        if(!check_bounds(b, seg_id, write_size)) { return false; }

        size_t offset = calc_segment_offset(b, seg_id) + b->segments[seg_id].len;
        uint8_t *p = b->base + offset;
        for(int i = 0; i < write_size; i++) { p[(write_size - 1) - i] = (uint8_t)((val >> (i * 8)) & BYTE_MASK); }

        b->segments[seg_id].len += write_size;

        return true;
    }

    bool pb_write_u32_be(packet_builder_t * b, pb_segment_id_t seg_id, uint32_t val) {
        size_t write_size = sizeof(uint32_t);

        if(!check_bounds(b, seg_id, write_size)) { return false; }

        size_t offset = calc_segment_offset(b, seg_id) + b->segments[seg_id].len;
        uint8_t *p = b->base + offset;
        for(int i = 0; i < write_size; i++) { p[(write_size - 1) - i] = (uint8_t)((val >> (i * 8)) & BYTE_MASK); }

        b->segments[seg_id].len += write_size;

        return true;
    }

    bool pb_write_u64_be(packet_builder_t * b, pb_segment_id_t seg_id, uint64_t val) {
        size_t write_size = sizeof(uint64_t);

        if(!check_bounds(b, seg_id, write_size)) { return false; }

        size_t offset = calc_segment_offset(b, seg_id) + b->segments[seg_id].len;
        uint8_t *p = b->base + offset;
        for(int i = 0; i < write_size; i++) { p[(write_size - 1) - i] = (uint8_t)((val >> (i * 8)) & BYTE_MASK); }

        b->segments[seg_id].len += write_size;

        return true;
    }

    bool pb_write_bytes(packet_builder_t * b, pb_segment_id_t seg_id, uint8_t *data, size_t len) {
        if(!check_bounds(b, seg_id, len)) { return false; }

        size_t offset = calc_segment_offset(b, seg_id) + b->segments[seg_id].len;
        memcpy(b->base + offset, data, len);
        b->segments[seg_id].len += len;

        return true;
    }
