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

#include "utils.h"
#include "compat.h"
#include "plc.h"
#include "../../utils/log.h"
#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>


#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || (__linux__)
#    define USE_ARC4RANDOM
#    define PLATFORM_POSIX

#    include <arpa/inet.h>
#    include <netdb.h>
#    include <netinet/in.h>
#    include <stdlib.h>
#    include <sys/socket.h>
#    include <sys/time.h>
#    include <sys/types.h>
#    include <unistd.h>

#elif defined(_WIN32) || defined(_WIN64)
#    define USE_BCRYPTGENRANDOM
#    define PLATFORM_WINDOWS

/* Windows include file order is important! */
#    include <winsock2.h>
#    include <windows.h>
#    include <ws2tcpip.h>
#    include <io.h>
#    include <tchar.h>
#    include <strsafe.h>
#    include <wincrypt.h>

#else
#    error "Platform does not support good random function!"
#endif


/*
 * This contains the utilities used by the test harness.
 */


#ifdef PLATFORM_WINDOWS

int util_sleep_ms(int ms) {
    if(ms <= 0) { return 1; }

    Sleep((DWORD)(unsigned int)ms);
    return 1;
}

#else

int util_sleep_ms(int ms) {
    struct timespec req;
    struct timespec rem = {0};

    req.tv_sec = ms / 1000;
    req.tv_nsec = (ms % 1000) * 1000000;

    return nanosleep(&req, &rem);
}

#endif


/*
 * time_ms
 *
 * Return the current epoch time in milliseconds.
 */

#ifdef PLATFORM_WINDOWS
int64_t util_time_ms(void) {
    FILETIME ft;
    int64_t res;

    GetSystemTimeAsFileTime(&ft);

    /* calculate time as 100ns increments since Jan 1, 1601. */
    res = (int64_t)(ft.dwLowDateTime) + ((int64_t)(ft.dwHighDateTime) << 32);

    /* get time in ms */

    res = res / 10000;

    return res;
}

int64_t util_time_us(void) {
    FILETIME ft;
    int64_t res;

    GetSystemTimeAsFileTime(&ft);

    /* calculate time as 100ns increments since Jan 1, 1601. */
    res = (int64_t)(ft.dwLowDateTime) + ((int64_t)(ft.dwHighDateTime) << 32);

    /* get time in microseconds */
    res = res / 10;

    return res;
}

#else


int64_t util_time_ms(void) {
    struct timeval tv;

    gettimeofday(&tv, NULL);

    return ((int64_t)tv.tv_sec * 1000) + ((int64_t)tv.tv_usec / 1000);
}

int64_t util_time_us(void) {
    struct timeval tv;

    gettimeofday(&tv, NULL);

    return ((int64_t)tv.tv_sec * 1000000) + (int64_t)tv.tv_usec;
}

#endif

#ifdef PLATFORM_WINDOWS
void system_yield(void) { SwitchToThread(); }
#else
#    include <sched.h>
void system_yield(void) { sched_yield(); }
#endif

/*
 * string helpers
 */

int match_chars(const char *source, int start_index, const char *chars) {
    return (int)(unsigned int)strspn(source + start_index, chars);
}


/*
 * Logging routines.
 */

static bool debug_is_on = false;


void debug_on(void) { debug_is_on = true; }

void debug_off(void) { debug_is_on = false; }


void error_impl(const char *func, int line, const char *templ, ...) {
    va_list va;

    /* print it out. */
    // NOLINTNEXTLINE
    fprintf(stderr, "ERROR %s:%d ", func, line);
    va_start(va, templ);
    vfprintf(stderr, templ, va);
    va_end(va);
    // NOLINTNEXTLINE
    fprintf(stderr, "\n");

    exit(1);
}


void info_impl(const char *func, int line, const char *templ, ...) {
    va_list va;

    if(!debug_is_on) { return; }

    /* print it out. */
    // NOLINTNEXTLINE
    fprintf(stderr, "INFO %s:%d ", func, line);
    va_start(va, templ);
    vfprintf(stderr, templ, va);
    va_end(va);
    // NOLINTNEXTLINE
    fprintf(stderr, "\n");
}


#define COLUMNS (size_t)(10)

void slice_dump(slice_s s) {
    size_t max_row, row, column;
    char row_buf[300]; /* MAGIC */

    if(!debug_is_on) { return; }

    /* determine the number of rows we will need to print. */
    max_row = (slice_len(s) + (COLUMNS - 1)) / COLUMNS;

    for(row = 0; row < max_row; row++) {
        size_t offset = (row * COLUMNS);
        size_t row_offset;

        /* print the prefix and address */
        // NOLINTNEXTLINE
        row_offset = (size_t)snprintf(&row_buf[0], sizeof(row_buf), "%03zu", offset);

        for(column = 0; column < COLUMNS && ((row * COLUMNS) + column) < slice_len(s) && row_offset < (int)sizeof(row_buf);
            column++) {
            offset = (row * COLUMNS) + column;
            row_offset +=
                // NOLINTNEXTLINE
                (size_t)snprintf(&row_buf[row_offset], sizeof(row_buf) - row_offset, " %02x", slice_get_uint8(s, offset));
        }

        /* zero terminate */
        if(row_offset < sizeof(row_buf)) {
            row_buf[row_offset] = (char)0;
        } else {
            /* this might truncate the string, but it is safe. */
            row_buf[sizeof(row_buf) - 1] = (char)0;
        }

        /* output it, finally */
        // NOLINTNEXTLINE
        fprintf(stderr, "%s\n", row_buf);
    }
}


/* FIXME - move this all over into a compatibility/platform check header */
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
#    include <stdlib.h>

uint64_t random_u64(uint64_t upper_bound) {
    uint64_t random_number = 0;

    arc4random_buf(&random_number, sizeof(random_number));
    random_number %= upper_bound;

    return random_number;
}

#elif defined(__linux__)
#    include <stdlib.h>
#    include <sys/random.h>


uint64_t random_u64(uint64_t upper_bound) {
    uint64_t random_number = 0;

    if(upper_bound == 0) { return 0; }

    if(getrandom(&random_number, sizeof(random_number), GRND_NONBLOCK) < (ssize_t)sizeof(random_number)) {
        /* not enough entropy, do it the hard way. */
        srand((unsigned int)((uint64_t)time(NULL) ^ random_number));
        for(size_t i = 0; i < sizeof(random_number); ++i) { ((uint8_t *)&random_number)[i] ^= (uint8_t)(rand() % 256); }
    }

    random_number %= upper_bound;

    return random_number;
}

#elif defined(USE_BCRYPTGENRANDOM)

uint64_t random_u64(uint64_t upper_bound) {
    uint64_t random_number = 0;

    if(BCryptGenRandom(NULL, (PUCHAR)&random_number, sizeof(random_number), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
        return RANDOM_U64_ERROR;
    }
    if(upper_bound == 0) { return 0; }
    random_number %= upper_bound;

    return random_number;
}

#else
#    error "Platform not supported!"
#endif


/* Logging implementation for slice and CIP error responses */

void log_slice_impl_func(const char *func, int line, log_level_t lvl, slice_s s) {
    if (!log_is_enabled(LOG_MODULE_AB_SERVER, lvl)) {
        return;
    }

    if (!s.data || s.len == 0) {
        pdlog(LOG_MODULE_AB_SERVER, lvl, "%s:%d - Empty slice", func, line);
        return;
    }

    pdlog(LOG_MODULE_AB_SERVER, lvl, "%s:%d - Slice data (len=%zu):", func, line, s.len);

    /* Print the slice data as hex and ASCII */
    const size_t bytes_per_line = 16;
    for (size_t offset = 0; offset < s.len; offset += bytes_per_line) {
        size_t remaining = s.len - offset;
        size_t line_len = remaining < bytes_per_line ? remaining : bytes_per_line;

        /* Print hex */
        char hex_buf[256];
        char *hex_ptr = hex_buf;
        for (size_t i = 0; i < line_len; i++) {
            hex_ptr += snprintf(hex_ptr, hex_buf + sizeof(hex_buf) - hex_ptr, "%02x ", s.data[offset + i]);
        }

        /* Print ASCII */
        char ascii_buf[256];
        char *ascii_ptr = ascii_buf;
        for (size_t i = 0; i < line_len; i++) {
            uint8_t c = s.data[offset + i];
            ascii_ptr += snprintf(ascii_ptr, ascii_buf + sizeof(ascii_buf) - ascii_ptr, "%c", (c >= 32 && c < 127) ? c : '.');
        }

        pdlog(LOG_MODULE_AB_SERVER, lvl, "  %04zx: %-48s %s", offset, hex_buf, ascii_buf);
    }
}


slice_s make_cip_pdlog(log_module_t module, log_level_t level, slice_s output, uint8_t cip_service, uint16_t error_code, bool is_extended, uint16_t extended_code) {
    /* Log the error */
    if (is_extended) {
        pdlog(module, level, "CIP Error: service=0x%02x error=0x%04x (extended) extended_error=0x%04x", cip_service, error_code, extended_code);
    } else {
        pdlog(module, level, "CIP Error: service=0x%02x error=0x%02x", cip_service, (uint8_t)error_code);
    }

    /* Build CIP error response header */
    #define CIP_RESPONSE_HEADER_SIZE 4
    #define CIP_DONE 0x80

    if (slice_len(output) < CIP_RESPONSE_HEADER_SIZE) {
        /* Not enough space for error response */
        return slice_make_err(-1);
    }

    slice_s response_header = slice_from_slice(output, 0, CIP_RESPONSE_HEADER_SIZE);

    /* Build header: service | done, reserved, status, extended_error_size */
    slice_set_uint8(response_header, 0, cip_service | CIP_DONE);
    slice_set_uint8(response_header, 1, 0);  /* reserved */
    slice_set_uint8(response_header, 2, (uint8_t)error_code);  /* status */

    if (is_extended) {
        slice_set_uint8(response_header, 3, 2);  /* extended error size (in words) */

        /* Add extended error code if there's space */
        if (slice_len(output) >= CIP_RESPONSE_HEADER_SIZE + 2) {
            slice_set_uint16_le(output, CIP_RESPONSE_HEADER_SIZE, extended_code);
            return slice_from_slice(output, 0, CIP_RESPONSE_HEADER_SIZE + 2);
        }
    } else {
        slice_set_uint8(response_header, 3, 0);  /* no extended error */
    }

    return slice_from_slice(output, 0, CIP_RESPONSE_HEADER_SIZE);
}
