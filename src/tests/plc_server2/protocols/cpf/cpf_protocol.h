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
#include "cpf_defs.h"

/* Forward declarations to avoid circular dependencies */
typedef struct ab_plc_context_s ab_plc_context_t;
typedef struct client_context_s client_context_t;

/* ============================================================================
 * CPF Protocol Functions
 * ============================================================================ */

/**
 * @brief Dispatch a CPF (Common Packet Format) packet
 *
 * Parses CPF packet structure and dispatches CIP message to appropriate handlers.
 * Handles both connected and unconnected data items.
 *
 * @param input Buffer positioned at start of CPF packet
 * @param output Buffer to write response to
 * @param plc PLC context with tags and CIP registry
 * @param client Client connection context with session and connection state
 * @return UTIL_OK on success, error code on failure
 */
util_err_t cpf_dispatch(buf_t *input, buf_t *output, ab_plc_context_t *plc, client_context_t *client);

#ifdef __cplusplus
}
#endif
