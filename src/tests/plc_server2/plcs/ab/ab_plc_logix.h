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
#include <stdbool.h>
#include "../../../utils/err.h"
#include "../../../utils/args.h"
#include "../../../utils/coro_net.h"
#include "ab_context.h"

/* ============================================================================
 * Client Connection Context
 * ============================================================================
 *
 * Per-connection state including I/O buffers and EIP/CIP session information.
 * Each connected client has one client_context_t instance.
 */

typedef struct client_context_s {
    /* Reference to server-wide PLC context */
    ab_plc_context_t *plc;

    /* I/O Buffers */
    uint8_t recv_buffer[8192];
    uint8_t send_buffer[8192];

    /* Buffer size limits */
    size_t client_to_server_max_packet;
    size_t server_to_client_max_packet;

    /* EIP Session State */
    uint32_t eip_session_handle;
    uint32_t eip_sequence_number;

    /* CIP Connection Manager State */
    uint32_t client_connection_id;      /* O->T connection ID */
    uint32_t server_connection_id;      /* T->O connection ID */
    uint32_t connection_serial_number;  /* Connection serial number */
    uint16_t originator_vendor_id;      /* Originator vendor ID */
    uint32_t originator_serial_number;  /* Originator serial number */
    bool is_forward_open;               /* Connected mode flag */

    /* Fragmentation State (for large tag reads/writes) */
    uint8_t fragment_offset;            /* Current fragment offset */
    uint16_t fragment_size;             /* Size of current fragment */
    bool has_pending_fragments;         /* More fragments to come */
} client_context_t;

/* Main entry point for ControlLogix/Micro800 PLC setup
 * Called by main.c after argument parsing
 * Creates PLC context, listener socket, and registers with coro event loop
 */
extern util_err_t ab_plc_logix_main(const args_result_t *args, coro_net_t *coro);


#ifdef __cplusplus
}
#endif
