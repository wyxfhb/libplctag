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

#include "cpf_protocol.h"
#include "../cip/cip_protocol.h"

/* ============================================================================
 * CPF Dispatcher - Stub
 * ============================================================================
 *
 * TODO: Port from plc_server/protocol/cpf_protocol.c
 * - Parse CPF item list
 * - Extract unconnected or connected data items
 * - Dispatch through CIP message router
 * - Build response with CPF item structure
 */

util_err_t cpf_dispatch(buf_t *input, buf_t *output, context_registry_t *context_reg) {
    if(!input || !output || !context_reg) {
        return UTIL_EINVAL;
    }

    /* Stub: Just return error for now */
    buf_set_error(output, UTIL_ENOTSUPPORTED, "cpf_dispatch");
    return UTIL_ENOTSUPPORTED;
}
