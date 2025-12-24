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
#include "log.h"


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
        size_t limit;                             /* Max size of this segment */
        size_t len;                               /* Actual bytes written */
    } limits[PB_MAX_SEGMENTS];                    /* FIXME - make this a defined value! */
    pb_segment_id_t segment_ids[PB_MAX_SEGMENTS]; /* Segment IDs */
    const char *err_field;
    int err;
    uint8_t num_segments;
    pb_segment_id_t default_write_seg_id;
    bool compacted;
};

/**
 * @brief returns an initialized packet builder
 *
 * The builder is initialized with the base pointing to the provided memory,
 * and the capacity set to the provided capacity.
 *
 * All segments are given the ID PB_INVALID_SEGMENT_ID.
 *
 * All segments are given a limit and length of 0.
 *
 * The default write segment ID is set to PB_INVALID_SEGMENT_ID.
 *
 * The compacted flag is set to false.
 *
 * @param mem
 * @param capacity
 * @return * packet_builder_t
 */
packet_builder_t pb_init(uint8_t *mem, size_t capacity);

/**
 * @brief Reset the builder to initial state
 *
 * All segments ar given the ID PB_INVALID_SEGMENT_ID.
 *
 * All segments are given a limit and length of 0.
 *
 * The number of segments is set to 0.
 *
 * The default write segment ID is set to PB_INVALID_SEGMENT_ID.
 *
 * The compacted flag is set to false.
 *
 * @param b
 * @return true
 * @return false
 */
bool pb_reset(packet_builder_t *b);

/* ==============================================================
 * API to write data to sockets
   ============================================================== */

/**
 * @brief Get base pointer to packet data
 *
 * This fails and returns NULL if b is NULL or if the packet builder has not
 * been compressed/compacted.
 *
 * @param b
 * @return * uint8_t*
 */
uint8_t *pb_get_base_ptr(packet_builder_t *b);

/**
 * @brief Get total length of packet data
 *
 * This will return the total length of all segments.
 *
 * If the b pointer is NULL, PB_INVALID_SEGMENT_SIZE is returned.
 *
 * @param b
 * @return size_t
 */
size_t pb_get_total_len(packet_builder_t *b);


/**
 * @brief Compact segments into a contiguous block.
 *
 * This packs all segments into a single contiguous segment, removing any unused space.
 * The compacted segment will have the ID passed as a parameter.
 *
 * After compaction, only the compacted segment is valid; all other segments are invalidated.
 * The segment count is set to 1.
 *
 * The compacted flag is set to true.
 *
 * Returns true on success, false on failure.
 *
 * If the packet builder has an error state, compaction will not be performed and false is returned.
 *
 * If the packet builder has already been compacted and the passed segment ID does not match the compacted segment ID,
 * false is returned and the packet builder error is set to UTIL_EDUPLICATE.
 *
 * @param b
 * @param packet_seg_id The segment ID to use for the compacted data.
 * @return bool - true on success, false on failure
 */
bool pb_compact(packet_builder_t *b, pb_segment_id_t packet_seg_id);


/**
 * @brief Check if the packet builder is compacted
 *
 * Returns false if b is NULL.
 *
 * @param b
 * @return true
 * @return false
 */
bool pb_is_compacted(packet_builder_t *b);

/**
 * @brief consume bytes from the compacted segment
 *
 * This fails if b is NULL with error UTIL_EINVAL.
 *
 * This fails if the the packet builder is not compacted with error UTIL_EINVAL.
 *
 * This fails if the app attemps to consume more bytes than are available
 * with error UTIL_EBOUNDS.
 *
 * @param b
 * @param size
 * @return true - everything was good.
 * @return false - some error occurred.
 */
bool pb_consume_compacted(packet_builder_t *b, size_t size);


/**
 * @brief Add a segment to the packet builder with the passed ID and maximum size.
 *
 * Fails if b is NULL, returning false.
 *
 * Fails if the maximum number of segments has already been added, returning false and
 * setting the packet builder error to UTIL_ERESOURCE.
 *
 * Fails if the segment ID is already in use, returning false and setting the packet
 * builder error to UTIL_EDUPLICATE.
 *
 * @param b
 * @param new_seg_id
 * @param max_size
 * @return bool - true on success, false on failure
 */
bool pb_add_segment(packet_builder_t *b, pb_segment_id_t new_seg_id, size_t max_size);


/* Get data length of a segment */
size_t pb_segment_len(packet_builder_t *b, pb_segment_id_t seg_id);

/* Sum length of data in segments starting from start_seg_id to end */
size_t pb_sum_segment_len(packet_builder_t *b, pb_segment_id_t start_seg_id);

/* Error handling */
util_err_t pb_get_err(packet_builder_t *b);
bool pb_set_err(packet_builder_t *b, int err);
const char *pb_get_err_field(packet_builder_t *b);
bool pb_set_err_field(packet_builder_t *b, const char *field_name);

/* Write functions
 *
 * The write functions write to the default segment ID. If the default segment ID does
 * not exist, the write fails and the packet builder error is set to UTIL_ENOTFOUND.
 *
 * If the packet builder is compacted, all write operations fail and the packet builder error
 * is set to UTIL_EUNSUPPORTED.
 */

/* sets the default segment ID to use when writing */
bool pb_set_default_seg_id(packet_builder_t *b, pb_segment_id_t seg_id);

bool pb_write_u8(packet_builder_t *b, const char *field_name, uint8_t val);

bool pb_write_u16_le(packet_builder_t *b, const char *field_name, uint16_t val);
bool pb_write_u32_le(packet_builder_t *b, const char *field_name, uint32_t val);
bool pb_write_u64_le(packet_builder_t *b, const char *field_name, uint64_t val);

bool pb_write_u16_be(packet_builder_t *b, const char *field_name, uint16_t val);
bool pb_write_u32_be(packet_builder_t *b, const char *field_name, uint32_t val);
bool pb_write_u64_be(packet_builder_t *b, const char *field_name, uint64_t val);

bool pb_write_bytes(packet_builder_t *b, const char *field_name, uint8_t *data, size_t len);


/* Logging */

/**
 * @brief Dump bytes from a packet builder segment to the log (internal).
 *
 * @param func name of the function in which the log is generated
 * @param line_num line number in the source file
 * @param lvl log level
 * @param modules bitmask of modules this log applies to
 * @param pb packet builder buffer to dump
 * @param seg_id segment ID to dump
 */
void log_pb_bytes_impl(const char *func, int line_num, log_level_t lvl, log_module_mask_t modules, packet_builder_t *pb,
                       pb_segment_id_t seg_id);

/**
 * @brief Log packet builder buffer contents if enabled for the given modules and level.
 *
 * @param modules Bitmask of modules this log applies to
 * @param level Log level
 * @param buf Buffer to dump
 */
#define pdlog_pb_bytes(modules, level, pb, seg_id)                                                                \
    do {                                                                                                          \
        if(log_is_enabled(modules, level)) log_pb_bytes_impl(__func__, __LINE__, level, modules, (pb), (seg_id)); \
    } while(0)
