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
#include <stdbool.h>
#include "../../utils/buf.h"

/* ============================================================================
 * Client Connection Context
 * ============================================================================
 *
 * Per-client connection context stored in the context registry.
 * Contains all state for a single client connection to the EtherNet/IP server.
 */

typedef struct client_context_s {
    /* I/O Buffers - Fixed size for this connection */
    uint8_t recv_buffer[8192];
    uint8_t send_buffer[8192];
    buf_t recv_buf;
    buf_t send_buf;

    /* EIP Session Information */
    uint32_t session_handle;           /* EIP session handle from RegisterSession */
    uint32_t client_serial_number;     /* Client serial number for tracking */

    /* Connection Manager State */
    uint32_t client_connection_id;     /* Client-assigned connection ID */
    uint32_t server_connection_id;     /* Server-assigned connection ID */
    uint16_t server_to_client_max_packet;  /* Max packet size for S2C flow */
    bool is_forward_open;              /* Whether ForwardOpen is active */

    /* Note: Reference to PLC context is obtained via:
     * ab_plc_context_t *plc = context_registry_get(context_reg, CONTEXT_ID_PLC)
     * Do NOT store direct pointer here to avoid lifecycle issues */

} client_context_t;

/* ============================================================================
 * Client Context Lifecycle
 * ============================================================================ */

/**
 * @brief Create a new client context
 *
 * @return Pointer to new client_context_t, or NULL on allocation failure
 */
client_context_t *client_context_create(void);

/**
 * @brief Destroy a client context and free all resources
 *
 * @param context Client context to destroy
 */
void client_context_destroy(client_context_t *context);

#ifdef __cplusplus
}
#endif
