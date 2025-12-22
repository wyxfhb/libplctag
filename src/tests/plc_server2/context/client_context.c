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

#include <stdlib.h>
#include <string.h>
#include "client_context.h"

/* ============================================================================
 * Client Context Lifecycle
 * ============================================================================ */

client_context_t *client_context_create(void) {
    client_context_t *context = calloc(1, sizeof(*context));
    if(!context) { return NULL; }

    /* Initialize buffers */
    context->recv_buf = buf_init(context->recv_buffer, sizeof(context->recv_buffer));
    context->send_buf = buf_init(context->send_buffer, sizeof(context->send_buffer));

    /* Initialize EIP session handle to invalid */
    context->session_handle = 0;

    /* Initialize connection state */
    context->is_forward_open = false;
    context->client_connection_id = 0;
    context->server_connection_id = 0;
    context->server_to_client_max_packet = 2048;

    return context;
}

void client_context_destroy(client_context_t *context) {
    if(!context) { return; }
    free(context);
}
