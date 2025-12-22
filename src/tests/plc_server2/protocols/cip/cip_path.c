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

#include <string.h>
#include "cip_path.h"
#include "../../../utils/log.h"

/* ============================================================================
 * CIP Path Parsing
 * ============================================================================ */

util_err_t cip_parse_path(buf_t *input, uint8_t path_size_words, cip_path_t *path) {
    memset(path, 0, sizeof(*path));

    size_t path_bytes = path_size_words * 2; /* Convert words to bytes */
    size_t start_pos = buf_read_pos(input);
    size_t end_pos = start_pos + path_bytes;

    pdlog(LOG_MODULE_CIP_ROUTER, LOG_LEVEL_DETAIL, "CIP path: parsing %u words (%zu bytes)", path_size_words, path_bytes);
    pdlog_bytes(LOG_MODULE_CIP_ROUTER, LOG_LEVEL_DETAIL, input);

    /* Parse segments until we reach end */
    while(buf_read_pos(input) < end_pos && path->segment_count < 16) {
        uint8_t segment_format;

        if(!buf_read_u8(input, "segment_format", &segment_format)) {
            pdlog(LOG_MODULE_CIP_ROUTER, LOG_LEVEL_WARN, "CIP path: failed to read segment format");
            return buf_get_error(input);
        }

        pdlog(LOG_MODULE_CIP_ROUTER, LOG_LEVEL_DETAIL, "CIP path: segment_format=0x%02X at pos %zu/%zu",
              segment_format, buf_read_pos(input) - 1, end_pos);

        cip_segment_t *seg = &path->segments[path->segment_count];
        seg->type = segment_format;

        /* Dispatch by segment type */
        switch(segment_format) {
            /* Logical: Class 8-bit */
            case CIP_SEGMENT_LOGICAL_CLASS_8BIT: {
                uint8_t class_id;
                if(!buf_read_u8(input, "class_id", &class_id)) { return buf_get_error(input); }
                seg->logical.id = class_id;
                pdlog(LOG_MODULE_CIP_ROUTER, LOG_LEVEL_DETAIL, "  segment %zu: class 0x%02X", path->segment_count, class_id);
                break;
            }

            /* Logical: Class 16-bit */
            case CIP_SEGMENT_LOGICAL_CLASS_16BIT: {
                uint8_t padding;
                if(!buf_read_u8(input, "padding", &padding)) { return buf_get_error(input); }
                uint16_t class_id;
                if(!buf_read_u16_le(input, "class_id", &class_id)) { return buf_get_error(input); }
                seg->logical.id = class_id;
                pdlog(LOG_MODULE_CIP_ROUTER, LOG_LEVEL_DETAIL, "  segment %zu: class 0x%04X", path->segment_count, class_id);
                break;
            }

            /* Logical: Instance 8-bit */
            case CIP_SEGMENT_LOGICAL_INSTANCE_8BIT: {
                uint8_t instance_id;
                if(!buf_read_u8(input, "instance_id", &instance_id)) { return buf_get_error(input); }
                seg->logical.id = instance_id;
                pdlog(LOG_MODULE_CIP_ROUTER, LOG_LEVEL_DETAIL, "  segment %zu: instance 0x%02X", path->segment_count,
                      instance_id);
                break;
            }

            /* Logical: Instance 16-bit */
            case CIP_SEGMENT_LOGICAL_INSTANCE_16BIT: {
                uint8_t padding;
                if(!buf_read_u8(input, "padding", &padding)) { return buf_get_error(input); }
                uint16_t instance_id;
                if(!buf_read_u16_le(input, "instance_id", &instance_id)) { return buf_get_error(input); }
                seg->logical.id = instance_id;
                pdlog(LOG_MODULE_CIP_ROUTER, LOG_LEVEL_DETAIL, "  segment %zu: instance 0x%04X", path->segment_count,
                      instance_id);
                break;
            }

            /* Symbolic: Tag name */
            case CIP_SEGMENT_SYMBOLIC: {
                uint8_t name_length;
                if(!buf_read_u8(input, "name_length", &name_length)) { return buf_get_error(input); }

                /* Get pointer to name data in buffer */
                seg->symbolic.name = (const char *)buf_read_ptr(input);
                seg->symbolic.length = name_length;

                pdlog(LOG_MODULE_CIP_ROUTER, LOG_LEVEL_DETAIL, "  segment %zu: symbolic tag '%.*s'", path->segment_count,
                      (int)name_length, seg->symbolic.name);

                /* Advance past name */
                buf_read_advance(input, name_length);

                /* Pad to even boundary */
                if(name_length & 1) { buf_read_advance(input, 1); }
                break;
            }

            /* Numeric: 8-bit */
            case CIP_SEGMENT_NUMERIC_8BIT: {
                uint8_t value;
                if(!buf_read_u8(input, "numeric_value", &value)) { return buf_get_error(input); }
                seg->logical.id = value;
                pdlog(LOG_MODULE_CIP_ROUTER, LOG_LEVEL_DETAIL, "  segment %zu: numeric 8-bit 0x%02X", path->segment_count, value);
                break;
            }

            /* Numeric: 16-bit */
            case CIP_SEGMENT_NUMERIC_16BIT: {
                uint8_t padding;
                if(!buf_read_u8(input, "padding", &padding)) { return buf_get_error(input); }
                uint16_t value;
                if(!buf_read_u16_le(input, "numeric_value", &value)) { return buf_get_error(input); }
                seg->logical.id = value;
                pdlog(LOG_MODULE_CIP_ROUTER, LOG_LEVEL_DETAIL, "  segment %zu: numeric 16-bit 0x%04X", path->segment_count, value);
                break;
            }

            /* Numeric: 32-bit */
            case CIP_SEGMENT_NUMERIC_32BIT: {
                uint8_t padding;
                if(!buf_read_u8(input, "padding", &padding)) { return buf_get_error(input); }
                uint32_t value;
                if(!buf_read_u32_le(input, "numeric_value", &value)) { return buf_get_error(input); }
                seg->logical.id = value;
                pdlog(LOG_MODULE_CIP_ROUTER, LOG_LEVEL_DETAIL, "  segment %zu: numeric 32-bit 0x%08X", path->segment_count, value);
                break;
            }

            /* Data segment (for Omron offsets) */
            case CIP_SEGMENT_DATA: {
                uint32_t offset;
                uint16_t count;
                if(!buf_read_u32_le(input, "offset", &offset)) { return buf_get_error(input); }
                if(!buf_read_u16_le(input, "count", &count)) { return buf_get_error(input); }
                seg->data.offset_bytes = offset;
                seg->data.element_count = count;
                pdlog(LOG_MODULE_CIP_ROUTER, LOG_LEVEL_DETAIL, "  segment %zu: data offset=%u count=%u", path->segment_count,
                      offset, count);
                break;
            }

            default:
                pdlog(LOG_MODULE_CIP_ROUTER, LOG_LEVEL_WARN, "CIP path: unknown segment type 0x%02X", segment_format);
                return UTIL_ENOTSUPPORTED;
        }

        path->segment_count++;
    }

    /* Verify we consumed exactly the right number of bytes */
    if(buf_read_pos(input) != end_pos) {
        pdlog(LOG_MODULE_CIP_ROUTER, LOG_LEVEL_WARN, "CIP path: size mismatch (consumed %zu, expected %zu)",
              buf_read_pos(input) - start_pos, path_bytes);
        return UTIL_EBOUNDS;
    }

    return UTIL_OK;
}
