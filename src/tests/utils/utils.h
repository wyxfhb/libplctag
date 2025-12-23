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

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/**
 * @brief Event type identifier
 *
 * Individual modules define their own event type constants.
 * Events are passed with a separate context pointer rather than
 * being wrapped in a struct.
 */
typedef uint32_t event_type_t;

#define EVENT_TYPE_MAX 64 /* Maximum number of event types supported */


/**
 * @brief Get current time in milliseconds.
 *
 * Used for deferred event timing. Uses platform-specific functions.
 */
extern int64_t util_time_ms(void);

/**
 * @brief Get current time in microseconds.
 *
 * Used for performance analysis and latency measurements.
 */
extern int64_t util_time_us(void);

/**
 * @brief Set the interrupt handler function
 *
 * @param handler Function pointer to the handler called when ^C or signals occur.
 * @return int
 */
extern int util_set_interrupt_handler(void (*handler)(void));


/**
 * @brief  Sleep for the specified number of milliseconds.
 *
 * @param ms The number of milliseconds to sleep.
 */
extern void util_sleep_ms(int ms);

#ifdef __cplusplus
}
#endif
