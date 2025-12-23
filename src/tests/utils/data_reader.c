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

#include "data_reader.h"
#include "err.h"
#include <string.h>

data_reader_t data_reader_init(uint8_t *base, size_t capacity) {
    data_reader_t r = {.base = base, .capacity = capacity, .read_pos = 0, .write_pos = 0, .err = UTIL_OK};
    return r;
}

bool data_reader_reset(data_reader_t *r) {
    if(!r) { return false; }

    r->read_pos = 0;
    r->write_pos = 0;
    r->err = UTIL_OK;

    return true;
}


int data_reader_get_err(const data_reader_t *r) { return r ? r->err : UTIL_ENULL; }

bool data_reader_set_err(data_reader_t *r, int err) {
    if(!r) { return false; }

    r->err = err;

    return true;
}

size_t data_reader_write_space(const data_reader_t *r) {
    if(!r) { return DATA_READER_INVALID_SIZE; }
    if(r->write_pos > r->capacity) { return 0; }
    return r->capacity - r->write_pos;
}

uint8_t *data_reader_write_ptr(data_reader_t *r) {
    if(!r || !r->base) { return NULL; }
    return r->base + r->write_pos;
}

bool data_reader_write_advance(data_reader_t *r, size_t len) {
    if(!r) { return false; }
    if(r->err) { return false; }

    if(r->write_pos + len > r->capacity) {
        data_reader_set_err(r, UTIL_EBOUNDS);
        return false;
    }

    r->write_pos += len;

    return true;
}

size_t data_reader_read_size(const data_reader_t *r) {
    if(!r) { return DATA_READER_INVALID_SIZE; }

    if(r->read_pos > r->write_pos) { return 0; }

    return r->write_pos - r->read_pos;
}

bool data_reader_compact(data_reader_t *r) {
    if(!r) { return false; }
    if(r->read_pos == 0) { return true; }

    size_t remaining = data_reader_read_size(r);
    if(remaining > 0) { memmove(r->base, r->base + r->read_pos, remaining); }
    r->read_pos = 0;
    r->write_pos = remaining;

    return true;
}

bool data_reader_read_u8(data_reader_t *r, uint8_t *val) {
    if(!r) { return false; }
    if(!val) {
        data_reader_set_err(r, UTIL_ENULL);
        return false;
    }

    /* we can set the error but we do not want to overwrite the existing one. */
    if(r->err) { return false; }

    if(r->read_pos + 1 > r->write_pos) {
        data_reader_set_err(r, UTIL_EBOUNDS);
        return false;
    }

    *val = r->base[r->read_pos++];

    return true;
}

bool data_reader_read_u16_le(data_reader_t *r, uint16_t *val) {
    size_t read_size = sizeof(uint16_t);

    if(!r) { return false; }

    if(r->err) { return false; }

    if(r->read_pos + read_size > r->write_pos) {
        data_reader_set_err(r, UTIL_EBOUNDS);
        return false;
    }

    const uint8_t *p = r->base + r->read_pos;
    *val = (uint16_t)(p[0] | (p[1] << 8));

    r->read_pos += read_size;

    return true;
}

bool data_reader_read_u32_le(data_reader_t *r, uint32_t *val) {
    size_t read_size = sizeof(uint32_t);

    if(!r) { return false; }

    if(r->err) { return false; }

    if(r->read_pos + read_size > r->write_pos) {
        data_reader_set_err(r, UTIL_EBOUNDS);
        return false;
    }

    const uint8_t *p = r->base + r->read_pos;
    *val = (uint32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));

    r->read_pos += read_size;

    return true;
}

bool data_reader_read_u64_le(data_reader_t *r, uint64_t *val) {
    size_t read_size = sizeof(uint64_t);

    if(!r) { return false; }

    if(r->err) { return false; }

    if(r->read_pos + read_size > r->write_pos) {
        data_reader_set_err(r, UTIL_EBOUNDS);
        return false;
    }

    const uint8_t *p = r->base + r->read_pos;
    *val = (uint64_t)p[0] | ((uint64_t)p[1] << 8) | ((uint64_t)p[2] << 16) | ((uint64_t)p[3] << 24) | ((uint64_t)p[4] << 32)
           | ((uint64_t)p[5] << 40) | ((uint64_t)p[6] << 48) | ((uint64_t)p[7] << 56);

    r->read_pos += read_size;

    return true;
}

bool data_reader_read_u16_be(data_reader_t *r, uint16_t *val) {
    size_t read_size = sizeof(uint16_t);

    if(!r) { return false; }

    if(r->err) { return false; }

    if(r->read_pos + read_size > r->write_pos) {
        data_reader_set_err(r, UTIL_EBOUNDS);
        return false;
    }

    const uint8_t *p = r->base + r->read_pos;
    *val = (uint16_t)((p[0] << 8) | p[1]);

    r->read_pos += read_size;

    return true;
}

bool data_reader_read_u32_be(data_reader_t *r, uint32_t *val) {
    size_t read_size = sizeof(uint32_t);

    if(!r) { return false; }

    if(r->err) { return false; }

    if(r->read_pos + read_size > r->write_pos) {
        data_reader_set_err(r, UTIL_EBOUNDS);
        return false;
    }

    const uint8_t *p = r->base + r->read_pos;
    *val = (uint32_t)((p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]);

    r->read_pos += read_size;

    return true;
}

bool data_reader_read_u64_be(data_reader_t *r, uint64_t *val) {
    size_t read_size = sizeof(uint64_t);

    if(!r) { return false; }

    if(r->err) { return false; }

    if(r->read_pos + read_size > r->write_pos) {
        data_reader_set_err(r, UTIL_EBOUNDS);
        return false;
    }

    const uint8_t *p = r->base + r->read_pos;
    *val = ((uint64_t)p[0] << 56) | ((uint64_t)p[1] << 48) | ((uint64_t)p[2] << 40) | ((uint64_t)p[3] << 32)
           | ((uint64_t)p[4] << 24) | ((uint64_t)p[5] << 16) | ((uint64_t)p[6] << 8) | (uint64_t)p[7];

    r->read_pos += read_size;

    return true;
}

bool data_reader_read_bytes(data_reader_t *r, uint8_t *out, size_t len) {
    if(!r) { return false; }
    if(r->err) { return false; }
    if(r->read_pos + len > r->write_pos) {
        data_reader_set_err(r, UTIL_EBOUNDS);
        return false;
    }
    memcpy(out, r->base + r->read_pos, len);
    r->read_pos += len;
    return true;
}
