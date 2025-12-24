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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../../utils/log.h"
#include "../../../utils/socket.h"
#include "../../../utils/coro_net.h"
#include "../../../utils/args.h"
#include "../../../utils/utils.h"
#include "../../protocols/eip/eip_protocol.h"
#include "../../protocols/cip/cip_registry.h"
#include "ab_context.h"
#include "ab_plc_logix.h"
#include "symbol_object.h"
#include "connection_manager.h"

/* Constants */
#define MAX_DIMS 4   /* Maximum array dimensions for tags */
#define MAX_TAGS 100 /* Maximum number of tags */

/* ============================================================================
 * Tag Parsing Helper
 * ============================================================================ */

/**
 * Parse a tag specification like "TestBigArray:DINT[100]" or "MyTag:INT[10,20]"
 */
static util_err_t parse_tag_spec(const char *spec, size_t spec_len, char *name_out, size_t name_size, char *type_out,
                                 size_t type_size, size_t *dims_out, size_t *dim_count_out) {
    /* Find the colon separating name and type */
    const char *colon = memchr(spec, ':', spec_len);
    if(!colon) { return UTIL_EINVAL; }

    size_t name_len = colon - spec;
    if(name_len >= name_size) { return UTIL_EBOUNDS; }

    memcpy(name_out, spec, name_len);
    name_out[name_len] = '\0';

    /* Find the type and dimensions */
    const char *bracket = memchr(colon + 1, '[', spec_len - name_len - 1);
    if(!bracket) {
        /* No brackets means scalar type */
        size_t type_len = spec_len - name_len - 1;
        if(type_len >= type_size) { return UTIL_EBOUNDS; }
        memcpy(type_out, colon + 1, type_len);
        type_out[type_len] = '\0';
        *dim_count_out = 0;
        return UTIL_OK;
    }

    /* Extract type between colon and bracket */
    size_t type_len = bracket - colon - 1;
    if(type_len >= type_size) { return UTIL_EBOUNDS; }
    memcpy(type_out, colon + 1, type_len);
    type_out[type_len] = '\0';

    /* Parse dimensions inside brackets */
    size_t dim_idx = 0;
    const char *dim_start = bracket + 1;
    while(dim_idx < MAX_DIMS && dim_start < spec + spec_len) {
        const char *dim_end = memchr(dim_start, ',', spec + spec_len - dim_start);
        const char *bracket_end = memchr(dim_start, ']', spec + spec_len - dim_start);

        if(!bracket_end) { return UTIL_EINVAL; }

        if(!dim_end || dim_end > bracket_end) { dim_end = bracket_end; }

        char dim_str[32] = {0};
        size_t dim_str_len = dim_end - dim_start;
        if(dim_str_len >= sizeof(dim_str)) { return UTIL_EBOUNDS; }

        memcpy(dim_str, dim_start, dim_str_len);
        dims_out[dim_idx] = (size_t)atol(dim_str);
        dim_idx++;

        if(dim_end == bracket_end) { break; }

        dim_start = dim_end + 1; /* Skip comma */
    }

    *dim_count_out = dim_idx;
    return UTIL_OK;
}


typedef struct {
    ab_plc_context_t *plc;
    coro_net_t *coro; /* Event loop for adding new tasks */
    const char *bind_address;
    int bind_port;
} listener_info_t;

typedef struct {
    ab_plc_context_t *plc;
    client_context_t *client;
    uint8_t recv_buffer[8192];
    uint8_t send_buffer[8192];
} client_connection_t;


/* ============================================================================
 * Client Handler (Coroutine)
 * ============================================================================ */

static void client_handler(coro_task_handle_t handle, socket_t fd, void *context) {
    client_connection_t *conn = (client_connection_t *)context;
    util_err_t err = UTIL_OK;

    if(!conn || !conn->plc || !conn->client) {
        pdlog(LOG_MODULE_PLC_CLIENT, LOG_LEVEL_ERROR, "Missing client connection context");
        if(conn) { free(conn); }
        coro_remove_task(handle);
        socket_close(fd);
        return;
    }

    buf_t recv_buf = buf_init(conn->recv_buffer, sizeof(conn->recv_buffer));
    buf_t send_buf = buf_init(conn->send_buffer, sizeof(conn->send_buffer));

    CORO_START(handle);

    pdlog(LOG_MODULE_PLC_CLIENT, LOG_LEVEL_INFO, "Client handler started");

    while(1) {
        /* Compact receive buffer */
        buf_compact(&recv_buf);

        /* rebuild the send buffer, later layers will clamp the output size. */
        send_buf = buf_init(conn->send_buffer, sizeof(conn->send_buffer));

        pdlog(LOG_MODULE_PLC_CLIENT, LOG_LEVEL_DETAIL, "Waiting for EIP packet...");

        /* Read EIP frame from socket (will yield until data arrives) */
        stream_read_yield(handle, &recv_buf, eip_frame_check, NULL, NULL, NULL, err);

        if(err != UTIL_OK) {
            pdlog(LOG_MODULE_PLC_CLIENT, LOG_LEVEL_WARN, "Client read failed: %s", util_err_str(err));
            break;
        }

        pdlog(LOG_MODULE_PLC_CLIENT, LOG_LEVEL_DETAIL, "Received %zu bytes", buf_read_size(&recv_buf));

        /* Dispatch EIP request */
        err = eip_dispatch(&recv_buf, &send_buf, conn->plc, conn->client);

        if(err != UTIL_OK) { pdlog(LOG_MODULE_PLC_CLIENT, LOG_LEVEL_DETAIL, "EIP dispatch returned: %s", util_err_str(err)); }

        /* Send response */
        stream_write_yield(handle, &send_buf, err);

        if(err != UTIL_OK) {
            pdlog(LOG_MODULE_PLC_CLIENT, LOG_LEVEL_WARN, "Client write failed: %s", util_err_str(err));
            break;
        }
    }

    pdlog(LOG_MODULE_PLC_CLIENT, LOG_LEVEL_INFO, "Client handler closing");

    /* Cleanup client connection (PLC context is shared and cleaned up by main) */
    free(conn->client);
    free(conn);

    CORO_END(handle);
}

/* ============================================================================
 * Listener Handler (Coroutine)
 * ============================================================================ */

static void listener_handler(coro_task_handle_t handle, socket_t fd, void *context) {
    listener_info_t *listener = (listener_info_t *)context;
    socket_t client_fd = INVALID_SOCKET;
    socket_address_t client_addr;
    util_err_t err = UTIL_OK;

    (void)fd;

    CORO_START(handle);

    pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_INFO, "Listener started on %s:%d", listener->bind_address, listener->bind_port);

    while(1) {
        /* Accept incoming connection */
        stream_listener_accept_yield(handle, &client_fd, &client_addr, err);

        if(err != UTIL_OK) {
            pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_INFO, "Accept stopped: %s", util_err_str(err));
            break;
        }

        pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_INFO, "Accepted client connection");

        /* Create client context */
        client_context_t *client = (client_context_t *)calloc(1, sizeof(*client));
        if(!client) {
            pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_ERROR, "Failed to allocate client context");
            socket_close(client_fd);
            continue;
        }

        client->plc = listener->plc;

        /* Create client connection wrapper */
        client_connection_t *conn = (client_connection_t *)calloc(1, sizeof(*conn));
        if(!conn) {
            pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_ERROR, "Failed to allocate client connection");
            free(client);
            socket_close(client_fd);
            continue;
        }

        conn->plc = listener->plc;
        conn->client = client;

        /* Add client task to event loop */
        coro_task_handle_t client_handle;
        err = coro_add_task(&client_handle, listener->coro, client_fd, client_handler, (void *)conn);
        if(err != UTIL_OK) {
            pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_ERROR, "Failed to add client task: %s", util_err_str(err));
            free(client);
            socket_close(client_fd);
            free(conn);
            continue;
        }
    }

    pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_INFO, "Listener stopping");

    /* Cleanup */
    socket_close(fd);
    free(listener);

    CORO_END(handle);
}


/* ============================================================================
 * Main Entry Point for ControlLogix/Micro800 PLC Setup
 * ============================================================================ */

util_err_t ab_plc_logix_main(const args_result_t *args, coro_net_t *coro) {
    util_err_t rc = UTIL_OK;

    /* Extract port from arguments (default: 44818 for ControlLogix) */
    int port = 44818;
    char *port_str = (char *)args_get_string(args, "port");
    if(port_str && port_str[0] != '\0') { port = atoi(port_str); }

    pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_INFO, "Initializing ControlLogix PLC on port %d", port);

    /* Create AB ControlLogix PLC context using helper function */
    ab_plc_context_t *plc = ab_plc_context_create(PLC_TYPE_CONTROLLOGIX);
    if(!plc) {
        pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_ERROR, "Failed to create PLC context");
        return UTIL_ERESOURCE;
    }

    /* Process path from args if provided */
    const char *path_str = args_get_string((args_result_t *)args, "path");
    if(path_str && path_str[0] != '\0') {
        /* Parse comma-separated path bytes */
        const char *pos = path_str;
        size_t path_idx = 0;
        while(path_idx < sizeof(plc->path) && *pos) {
            char *endptr = NULL;
            long val = strtol(pos, &endptr, 0);
            if(endptr == pos) { break; }
            plc->path[path_idx++] = (uint8_t)val;
            pos = endptr;
            if(*pos == ',') { pos++; }
        }
        plc->path_len = path_idx;
        pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_INFO, "Using path: %zu bytes", plc->path_len);
    } else {
        /* Default path for ControlLogix: backplane (0x20), slot (0x01) */
        plc->path[0] = 0x20;
        plc->path[1] = 0x01;
        plc->path_len = 2;
        pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_INFO, "Using default ControlLogix path: 0x20 0x01");
    }

    /* Process tags from args if provided */
    size_t tag_count = args_get_count((args_result_t *)args, "tag");
    if(tag_count > 0) {
        plc->tags = (tag_def_t **)calloc(tag_count, sizeof(tag_def_t *));
        if(!plc->tags) {
            pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_ERROR, "Failed to allocate tags array");
            ab_plc_context_destroy(plc);
            return UTIL_ERESOURCE;
        }

        for(size_t i = 0; i < tag_count; i++) {
            args_value_t tag_val = args_get_at((args_result_t *)args, "tag", i);
            if(!tag_val.present) {
                pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_WARN, "Tag at index %zu has no value", i);
                continue;
            }

            const char *tag_spec = tag_val.value.string_val;
            size_t tag_spec_len = strlen(tag_spec);

            /* Parse tag specification */
            char tag_name[256] = {0};
            char tag_type[64] = {0};
            size_t tag_dims[MAX_DIMS] = {0};
            size_t tag_dim_count = 0;

            rc = parse_tag_spec(tag_spec, tag_spec_len, tag_name, sizeof(tag_name), tag_type, sizeof(tag_type), tag_dims,
                                &tag_dim_count);
            if(rc != UTIL_OK) {
                pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_WARN, "Failed to parse tag spec '%s': %s", tag_spec, util_err_str(rc));
                continue;
            }

            pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_INFO, "Creating tag '%s' type=%s dims=%zu", tag_name, tag_type, tag_dim_count);

            /* Create tag based on type */
            tag_def_t *tag = NULL;
            if(strcasecmp(tag_type, "DINT") == 0) {
                if(tag_dim_count == 0) {
                    tag = tag_create_dint(tag_name, 0);
                } else if(tag_dim_count == 1) {
                    tag = tag_create_dint_array(tag_name, tag_dims[0]);
                } else {
                    tag = tag_create_dint_array_multi(tag_name, tag_dim_count, tag_dims);
                }
            } else if(strcasecmp(tag_type, "INT") == 0) {
                /* For now, just create DINT (32-bit) for INT too */
                if(tag_dim_count == 0) {
                    tag = tag_create_dint(tag_name, 0);
                } else if(tag_dim_count == 1) {
                    tag = tag_create_dint_array(tag_name, tag_dims[0]);
                } else {
                    tag = tag_create_dint_array_multi(tag_name, tag_dim_count, tag_dims);
                }
            } else if(strcasecmp(tag_type, "REAL") == 0) {
                if(tag_dim_count == 0) {
                    tag = tag_create_real(tag_name, 0.0f);
                } else {
                    tag = tag_create_real_array_multi(tag_name, tag_dim_count, tag_dims);
                }
            } else {
                pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_WARN, "Unsupported tag type '%s' for tag '%s'", tag_type, tag_name);
                continue;
            }

            if(!tag) {
                pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_ERROR, "Failed to create tag '%s'", tag_name);
                continue;
            }

            plc->tags[plc->tag_count++] = tag;
        }

        pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_INFO, "Loaded %zu tags", plc->tag_count);
    }

    /* Register CIP objects for ControlLogix */
    /* Identity Object (Class 0x01) - would be registered here */

    /* Connection Manager (Class 0x06) for ForwardOpen/ForwardClose */
    rc = connection_manager_register(plc->cip_registry);
    if(rc != UTIL_OK) {
        pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_ERROR, "Failed to register Connection Manager: %s", util_err_str(rc));
        ab_plc_context_destroy(plc);
        return rc;
    }

    /* Symbol Object (Class 0x6B) for tag access */
    rc = symbol_object_register(plc->cip_registry);
    if(rc != UTIL_OK) {
        pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_ERROR, "Failed to register Symbol Object: %s", util_err_str(rc));
        ab_plc_context_destroy(plc);
        return rc;
    }

    /* Set Symbol Object as default handler for symbolic segments */
    rc = cip_class_registry_set_symbolic_handler_class(plc->cip_registry, 0x6B);
    if(rc != UTIL_OK) {
        pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_ERROR, "Failed to set symbolic handler class: %s", util_err_str(rc));
        ab_plc_context_destroy(plc);
        return rc;
    }

    /* Create listener socket */
    socket_address_t listen_addr;
    rc = socket_address_init(&listen_addr, "127.0.0.1", (uint16_t)port);
    if(rc != UTIL_OK) {
        pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_ERROR, "Failed to initialize socket address: %s", util_err_str(rc));
        ab_plc_context_destroy(plc);
        return rc;
    }

    socket_t listen_fd = stream_listener_socket_create(&listen_addr, 10);
    if(listen_fd == INVALID_SOCKET) {
        pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_ERROR, "Failed to create listener socket");
        ab_plc_context_destroy(plc);
        return UTIL_ESOCKET;
    }

    pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_INFO, "Listening on 127.0.0.1:%d", port);

    /* Create listener info structure */
    listener_info_t *listener = (listener_info_t *)malloc(sizeof(*listener));
    if(!listener) {
        pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_ERROR, "Failed to allocate listener info");
        socket_close(listen_fd);
        ab_plc_context_destroy(plc);
        return UTIL_ERESOURCE;
    }

    listener->plc = plc;
    listener->coro = coro;
    listener->bind_address = "127.0.0.1";
    listener->bind_port = port;

    /* Add listener task to event loop */
    coro_task_handle_t listener_handle;
    rc = coro_add_task(&listener_handle, coro, listen_fd, listener_handler, (void *)listener);
    if(rc != UTIL_OK) {
        pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_ERROR, "Failed to add listener task: %s", util_err_str(rc));
        free(listener);
        socket_close(listen_fd);
        ab_plc_context_destroy(plc);
        return rc;
    }

    pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_INFO, "ControlLogix PLC initialized successfully");

    return UTIL_OK;
}
