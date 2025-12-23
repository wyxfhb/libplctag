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


#include "buf_view.h"
#include <string.h>

buf_view_t buf_view_init(uint8_t *data, size_t capacity) {
    buf_view_t v;
    v.data = data;
    v.capacity = (data) ? capacity : 0;
    v.cursor = 0;
    v.error = (data) ? UTIL_OK : UTIL_ENULL;
    return v;
}

buf_view_t buf_view_slice(buf_view_t *v, size_t len) {
    buf_view_t slice = {0};

    if(v->error != UTIL_OK) {
        slice.error = v->error;
        return slice;
    }

    if(v->cursor + len > v->capacity) {
        v->error = UTIL_EBOUNDS;
        slice.error = UTIL_EBOUNDS;
        return slice;
    }

    slice.data = v->data + v->cursor;
    slice.capacity = len;
    slice.cursor = 0;
    slice.error = UTIL_OK;

    v->cursor += len;
    return slice;
}

buf_view_t buf_view_slice_remaining(buf_view_t *v) { return buf_view_slice(v, buf_view_remaining(v)); }

bool buf_view_ok(const buf_view_t *v) { return v && v->data && v->error == UTIL_OK; }

size_t buf_view_remaining(const buf_view_t *v) {
    if(!buf_view_ok(v) || v->cursor >= v->capacity) { return 0; }
    return v->capacity - v->cursor;
}

size_t buf_view_pos(const buf_view_t *v) { return v ? v->cursor : 0; }

util_err_t buf_view_get_error(const buf_view_t *v) { return v ? v->error : UTIL_ENULL; }

bool buf_view_seek(buf_view_t *v, size_t pos) {
    if(!buf_view_ok(v)) { return false; }
    if(pos > v->capacity) {
        v->error = UTIL_EBOUNDS;
        return false;
    }
    v->cursor = pos;
    return true;
}

bool buf_view_advance(buf_view_t *v, size_t n) {
    if(!buf_view_ok(v)) { return false; }
    if(v->cursor + n > v->capacity) {
        v->error = UTIL_EBOUNDS;
        return false;
    }
    v->cursor += n;
    return true;
}

/* --- Read Implementations --- */

bool buf_view_read_u8(buf_view_t *v, uint8_t *out) {
    if(!buf_view_ok(v) || v->cursor + 1 > v->capacity) {
        if(v) { v->error = UTIL_EBOUNDS; }
        return false;
    }
    *out = v->data[v->cursor++];
    return true;
}

bool buf_view_read_u16_le(buf_view_t *v, uint16_t *out) {
    if(!buf_view_ok(v) || v->cursor + 2 > v->capacity) {
        if(v) { v->error = UTIL_EBOUNDS; }
        return false;
    }
    uint8_t *p = v->data + v->cursor;
    *out = (uint16_t)p[0] | ((uint16_t)p[1] << 8);
    v->cursor += 2;
    return true;
}

bool buf_view_read_u16_be(buf_view_t *v, uint16_t *out) {
    if(!buf_view_ok(v) || v->cursor + 2 > v->capacity) {
        if(v) { v->error = UTIL_EBOUNDS; }
        return false;
    }
    uint8_t *p = v->data + v->cursor;
    *out = ((uint16_t)p[0] << 8) | (uint16_t)p[1];
    v->cursor += 2;
    return true;
}

bool buf_view_read_u32_le(buf_view_t *v, uint32_t *out) {
    if(!buf_view_ok(v) || v->cursor + 4 > v->capacity) {
        if(v) { v->error = UTIL_EBOUNDS; }
        return false;
    }
    uint8_t *p = v->data + v->cursor;
    *out = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    v->cursor += 4;
    return true;
}

bool buf_view_read_u32_be(buf_view_t *v, uint32_t *out) {
    if(!buf_view_ok(v) || v->cursor + 4 > v->capacity) {
        if(v) { v->error = UTIL_EBOUNDS; }
        return false;
    }
    uint8_t *p = v->data + v->cursor;
    *out = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
    v->cursor += 4;
    return true;
}

bool buf_view_read_bytes(buf_view_t *v, uint8_t *out, size_t len) {
    if(!buf_view_ok(v) || v->cursor + len > v->capacity) {
        if(v) { v->error = UTIL_EBOUNDS; }
        return false;
    }
    memcpy(out, v->data + v->cursor, len);
    v->cursor += len;
    return true;
}

/* --- Write Implementations --- */

bool buf_view_write_u8(buf_view_t *v, uint8_t val) {
    if(!buf_view_ok(v) || v->cursor + 1 > v->capacity) {
        if(v) { v->error = UTIL_EBOUNDS; }
        return false;
    }
    v->data[v->cursor++] = val;
    return true;
}

bool buf_view_write_u16_le(buf_view_t *v, uint16_t val) {
    if(!buf_view_ok(v) || v->cursor + 2 > v->capacity) {
        if(v) { v->error = UTIL_EBOUNDS; }
        return false;
    }
    uint8_t *p = v->data + v->cursor;
    p[0] = (uint8_t)(val & 0xFF);
    p[1] = (uint8_t)((val >> 8) & 0xFF);
    v->cursor += 2;
    return true;
}

bool buf_view_write_u16_be(buf_view_t *v, uint16_t val) {
    if(!buf_view_ok(v) || v->cursor + 2 > v->capacity) {
        if(v) { v->error = UTIL_EBOUNDS; }
        return false;
    }
    uint8_t *p = v->data + v->cursor;
    p[0] = (uint8_t)((val >> 8) & 0xFF);
    p[1] = (uint8_t)(val & 0xFF);
    v->cursor += 2;
    return true;
}

bool buf_view_write_u32_le(buf_view_t *v, uint32_t val) {
    if(!buf_view_ok(v) || v->cursor + 4 > v->capacity) {
        if(v) { v->error = UTIL_EBOUNDS; }
        return false;
    }
    uint8_t *p = v->data + v->cursor;
    p[0] = (uint8_t)(val & 0xFF);
    p[1] = (uint8_t)((val >> 8) & 0xFF);
    p[2] = (uint8_t)((val >> 16) & 0xFF);
    p[3] = (uint8_t)((val >> 24) & 0xFF);
    v->cursor += 4;
    return true;
}

bool buf_view_write_u32_be(buf_view_t *v, uint32_t val) {
    if(!buf_view_ok(v) || v->cursor + 4 > v->capacity) {
        if(v) { v->error = UTIL_EBOUNDS; }
        return false;
    }
    uint8_t *p = v->data + v->cursor;
    p[0] = (uint8_t)((val >> 24) & 0xFF);
    p[1] = (uint8_t)((val >> 16) & 0xFF);
    p[2] = (uint8_t)((val >> 8) & 0xFF);
    p[3] = (uint8_t)(val & 0xFF);
    v->cursor += 4;
    return true;
}

bool buf_view_write_bytes(buf_view_t *v, const uint8_t *data, size_t len) {
    if(!buf_view_ok(v) || v->cursor + len > v->capacity) {
        if(v) { v->error = UTIL_EBOUNDS; }
        return false;
    }
    if(len > 0 && data) {
        memcpy(v->data + v->cursor, data, len);
        v->cursor += len;
    }
    return true;
}