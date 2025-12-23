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


#define PB_MAX_SEGMENTS 8
#define PB_INVALID_SEGMENT_ID 0xFF
#define PB_INVALID_SEGMENT_SIZE SIZE_MAX

/*
 * Packet Builder API
 *
 * Solves the "Layered Protocol" problem by pre-allocating maximum space
 * for each layer (segment), allowing random-access writing to layers,
 * and then compacting the result into a contiguous packet.
 */

typedef struct packet_builder_s packet_builder_t;
typedef uint8_t pb_segment_id_t;

struct packet_builder_s {
    uint8_t *base;
    size_t capacity;
    struct {
        size_t limit; /* Max size of this segment */
        size_t len;   /* Actual bytes written */
    } segments[8];
    uint8_t num_segments;
    bool compacted;
    int err;
};

/* Initialize the builder */
packet_builder_t pb_init(uint8_t *mem, size_t capacity);

/* Get base pointer to the packet data */
uint8_t *pb_get_base_ptr(packet_builder_t *b);

/* get total length of all segments */
size_t pb_get_total_len(packet_builder_t *b);

/* Compress segments into contiguous block. Returns total size. */
size_t pb_compact(packet_builder_t *b);

/* consume data from a compacted packet builder */
bool pb_consume_compacted(packet_builder_t *b, size_t size);

/* Define a new segment with a maximum size. Must be called in order (Outer -> Inner). */
pb_segment_id_t pb_add_segment(packet_builder_t *b, size_t max_size);

/* Get data length of a segment */
size_t pb_segment_len(packet_builder_t *b, pb_segment_id_t seg_id);

/* Sum length of data in segments starting from start_seg_id to end */
size_t pb_sum_segment_len(packet_builder_t *b, pb_segment_id_t start_seg_id);

util_err_t pb_get_err(packet_builder_t *b);
bool pb_set_err(packet_builder_t *b, int err);

/* Write functions */
bool pb_write_u8(packet_builder_t *b, pb_segment_id_t seg_id, uint8_t val);

bool pb_write_u16_le(packet_builder_t *b, pb_segment_id_t seg_id, uint16_t val);
bool pb_write_u32_le(packet_builder_t *b, pb_segment_id_t seg_id, uint32_t val);
bool pb_write_u64_le(packet_builder_t *b, pb_segment_id_t seg_id, uint64_t val);

bool pb_write_u16_be(packet_builder_t *b, pb_segment_id_t seg_id, uint16_t val);
bool pb_write_u32_be(packet_builder_t *b, pb_segment_id_t seg_id, uint32_t val);
bool pb_write_u64_be(packet_builder_t *b, pb_segment_id_t seg_id, uint64_t val);

bool pb_write_bytes(packet_builder_t *b, pb_segment_id_t seg_id, uint8_t *data, size_t len);
