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

#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "../../../utils/buf.h"
#include "../../../utils/err.h"
#include "../../context/context_registry.h"
#include "eip_defs.h"

/* ============================================================================
 * EIP Protocol Functions
 * ============================================================================ */

/**
 * @brief Frame check callback for socket_read_yield
 *
 * Checks if a complete EIP packet is available in the buffer.
 *
 * @param buf Buffer to check
 * @param context Unused (NULL)
 * @return UTIL_OK if complete frame available, UTIL_EAGAIN if need more data,
 *         error code on invalid frame
 */
util_err_t eip_frame_check(buf_t *buf, void *context);

/**
 * @brief Main EIP dispatcher
 *
 * Routes EIP commands to appropriate handlers.
 * Parses request header, dispatches by command code, builds response.
 *
 * Extracts client context from registry via CONTEXT_ID_CLIENT.
 * Extracts PLC context from registry via CONTEXT_ID_PLC.
 *
 * @param input Buffer positioned at start of EIP packet (will advance read cursor)
 * @param output Buffer to write response to (cleared before use)
 * @param context_reg Context registry (contains client and PLC contexts)
 * @return UTIL_OK on success, error code on failure
 */
util_err_t eip_dispatch(buf_t *input, buf_t *output, context_registry_t *context_reg);

#ifdef __cplusplus
}
#endif
