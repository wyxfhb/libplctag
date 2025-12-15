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
#include <signal.h>
#include <inttypes.h>

/* Utility headers from src/tests/utils/ */
#include "../utils/socket.h"   /* Socket operations */
#include "../utils/coro_net.h" /* Event loop + coroutines */
#include "../utils/buf.h"      /* Buffer management */
#include "../utils/log.h"      /* Logging */
#include "../utils/err.h"      /* Error handling */
#include "../utils/args.h"     /* Command-line parsing */
#include "../utils/utils.h"    /* Time, signal handlers */

/* Local headers */
#include "plc_context.h"
#include "protocol/eip_protocol.h"
#include "protocol/cip_object_registry.h"
#include "protocol/objects/symbol_object_micro800.h"
#include "protocol/objects/symbol_object_controllogix.h"
#include "protocol/objects/identity_object.h"
#include "protocol/objects/connection_manager.h"
#include "protocol/objects/tag_name_server_omron.h"
#include "protocol/objects/variable_object_omron.h"
#include "protocol/objects/variable_type_object_omron.h"
#include "omron_storage.h"
#include "tag_storage.h"

/* ============================================================================
 * Globals
 * ============================================================================ */

static plc_context_t *g_server = NULL;

/* ============================================================================
 * Signal Handler for Graceful Shutdown
 * ============================================================================ */

void signal_handler(void) {
    pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_INFO, "Shutdown signal received");
    if(g_server && g_server->coro_net) {
        g_server->running = false;
        pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_INFO, "Stopping event loop");
        coro_stop(g_server->coro_net);
    } else {
        pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_INFO, "No event loop to stop!");
    }
}

/* ============================================================================
 * Client Handler (Coroutine)
 *
 * Handles incoming EIP requests from a single client.
 * ============================================================================ */

static void client_handler(coro_task_handle_t handle, socket_t fd, void *context) {
    client_context_t *client = (client_context_t *)context;
    util_err_t err = UTIL_OK;

    (void)fd; /* fd is available via coro_get_fd(handle) if needed */

    CORO_START(handle);

    pdlog(LOG_MODULE_PLC_CLIENT, LOG_LEVEL_INFO, "Client handler started");

    while(1) {
        /* Compact receive buffer to maximize space for new data */
        buf_compact(&client->recv_buf);

        pdlog(LOG_MODULE_PLC_CLIENT, LOG_LEVEL_DETAIL, "Going to wait for data from the client.");

        /* Read EIP frame with automatic retry on incomplete frame */
        socket_read_yield(handle, &client->recv_buf, eip_frame_check, client, NULL, NULL, err);

        if(err != UTIL_OK) {
            pdlog(LOG_MODULE_PLC_CLIENT, LOG_LEVEL_WARN, "Client read failed: %s", util_err_str(err));
            break;
        }

        pdlog(LOG_MODULE_PLC_CLIENT, LOG_LEVEL_DETAIL, "Received data:");
        pdlog_bytes(LOG_MODULE_PLC_CLIENT, LOG_LEVEL_DETAIL, &client->recv_buf);

        /* Reset send buffer for response building */
        buf_reset(&client->send_buf);

        /* Dispatch EIP request */
        err = eip_dispatch(&client->recv_buf, &client->send_buf, &client->session, client->plc);

        if(err != UTIL_OK) {
            pdlog(LOG_MODULE_PLC_CLIENT, LOG_LEVEL_DETAIL, "EIP dispatch returned: %s", util_err_str(err));
            /* Continue - error response was already built */
        }

        pdlog(LOG_MODULE_PLC_CLIENT, LOG_LEVEL_DETAIL, "Response data:");
        pdlog_bytes(LOG_MODULE_PLC_CLIENT, LOG_LEVEL_DETAIL, &client->send_buf);

        /* Send response */
        socket_write_yield(handle, &client->send_buf, err);

        if(err != UTIL_OK) {
            pdlog(LOG_MODULE_PLC_CLIENT, LOG_LEVEL_WARN, "Client write failed: %s", util_err_str(err));
            break;
        }
    }

    pdlog(LOG_MODULE_PLC_CLIENT, LOG_LEVEL_INFO, "Client handler closing");

    /* Cleanup */
    coro_remove_task(client->handle);
    socket_close(coro_get_fd(client->handle));
    free(client);

    CORO_END(handle);
}

/* ============================================================================
 * Listener Handler (Coroutine)
 *
 * Accepts incoming connections and creates client_handler tasks.
 * ============================================================================ */

static void listener_handler(coro_task_handle_t handle, socket_t fd, void *context) {
    listener_info_t *listener = (listener_info_t *)context;
    socket_t client_fd = INVALID_SOCKET;
    socket_address_t client_addr;
    util_err_t err = UTIL_OK;

    (void)fd;

    CORO_START(handle);

    pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_INFO, "Listener started on %s:%u", listener->bind_address, listener->bind_port);

    while(listener->plc->running) {
        /* YIELD: Wait for incoming connection */
        socket_accept_yield(handle, &client_fd, &client_addr, err);

        if(err != UTIL_OK) {
            pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_ERROR, "Accept failed: %s", util_err_str(err));
            break;
        }

        /* Log client info */
        char client_ip[INET6_ADDRSTRLEN];
        socket_address_get_addr_str(&client_addr, client_ip, sizeof(client_ip));
        uint16_t client_port = socket_address_get_port(&client_addr);
        pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_INFO, "Accepted connection from %s:%u", client_ip, client_port);

        /* Create client context */
        client_context_t *client = (client_context_t *)calloc(1, sizeof(client_context_t));
        if(!client) {
            pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_ERROR, "Failed to allocate client context");
            socket_close(client_fd);
            continue;
        }

        /* Initialize client state */
        client->plc = listener->plc;
        client->recv_buf = buf_init(client->recv_buffer, sizeof(client->recv_buffer));
        client->send_buf = buf_init(client->send_buffer, sizeof(client->send_buffer));
        client->session.session_handle = 0;
        client->session.registered = false;

        /* Add client task to event loop */
        coro_task_handle_t client_handle;
        err = coro_add_task(&client_handle, listener->plc->coro_net, client_fd, client_handler, (void *)client);
        if(err != UTIL_OK) {
            pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_ERROR, "Failed to add client task: %s", util_err_str(err));
            socket_close(client_fd);
            free(client);
            continue;
        }

        client->handle = client_handle;
    }

    pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_INFO, "Listener stopping");

    /* Cleanup */
    coro_remove_task(listener->handle);
    socket_close(coro_get_fd(listener->handle));
    free(listener);

    CORO_END(handle);
}

/* ============================================================================
 * Main Entry Point
 * ============================================================================ */


static args_flag_def_t flags[] = {{.name = "listen",
                                   .type = ARGS_TYPE_STRING,
                                   .required = ARGS_OPTIONAL,
                                   .repeat = ARGS_MULTIPLE,
                                   .description = "Address:port to bind (default: 0.0.0.0:44818)",
                                   .default_value = {.has_default = true, .value.string_val = "0.0.0.0:44818"}},
                                  {.name = "debug",
                                   .type = ARGS_TYPE_STRING,
                                   .required = ARGS_OPTIONAL,
                                   .repeat = ARGS_ONCE,
                                   .description = "Log level: none|error|warn|info|detail|spew",
                                   .default_value = {.has_default = true, .value.string_val = "info"}},
                                  {.name = "plc-type",
                                   .type = ARGS_TYPE_STRING,
                                   .required = ARGS_OPTIONAL,
                                   .repeat = ARGS_ONCE,
                                   .description = "PLC type: micro800|controllogix|omron",
                                   .default_value = {.has_default = true, .value.string_val = "micro800"}},
                                  {
                                      .name = "help",
                                      .type = ARGS_TYPE_BOOL,
                                      .required = ARGS_OPTIONAL,
                                      .repeat = ARGS_ONCE,
                                      .description = "Show this help message",
                                      .default_value = {.has_default = false},
                                  }};
static size_t num_flags = sizeof(flags) / sizeof(flags[0]);


int main(int argc, char *argv[]) {
    plc_context_t server = {0};
    g_server = &server;

    args_result_t args_result = {0};
    util_err_t err = UTIL_OK;

    /* ===== PHASE 1: SOCKET SUBSYSTEM INITIALIZATION ===== */
    err = socket_init();
    if(err != UTIL_OK) {
        fprintf(stderr, "Failed to initialize socket subsystem: %s\n", util_err_str(err));
        return EXIT_FAILURE;
    }

    /* ===== PHASE 2: ARGUMENT PARSING ===== */

    util_err_t parse_rc = args_parse(argc, argv, flags, num_flags, &args_result);
    if(parse_rc != UTIL_OK || args_get_bool(&args_result, "help")) {
        args_print_help(argv[0], flags, num_flags);
        args_free(&args_result);
        return (parse_rc == UTIL_OK) ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    /* ===== PHASE 3: LOGGING CONFIGURATION ===== */
    char *debug_str = args_get_string(&args_result, "debug");
    log_level_t log_level = LOG_LEVEL_INFO; /* default */
    if(debug_str) {
        if(strcmp(debug_str, "none") == 0) {
            log_level = LOG_LEVEL_NONE;
        } else if(strcmp(debug_str, "error") == 0) {
            log_level = LOG_LEVEL_ERROR;
        } else if(strcmp(debug_str, "warn") == 0) {
            log_level = LOG_LEVEL_WARN;
        } else if(strcmp(debug_str, "info") == 0) {
            log_level = LOG_LEVEL_INFO;
        } else if(strcmp(debug_str, "detail") == 0) {
            log_level = LOG_LEVEL_DETAIL;
        } else if(strcmp(debug_str, "spew") == 0) {
            log_level = LOG_LEVEL_SPEW;
        }
    }
    log_set_all_modules(log_level);

    /* ===== PHASE 4: PLC CONTEXT INITIALIZATION ===== */
    server.running = 1;
    server.start_time_us = util_time_us();

    /* Initialize CIP object registry */
    server.registry = cip_registry_create();
    if(!server.registry) {
        pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_ERROR, "Failed to create CIP object registry");
        args_free(&args_result);
        socket_cleanup();
        return EXIT_FAILURE;
    }

    /* Register CIP objects - select symbol object based on PLC type */
    char *plc_type_str = args_get_string(&args_result, "plc-type");
    if(!plc_type_str) {
        plc_type_str = "micro800"; /* fallback default */
    }

    if(strcmp(plc_type_str, "controllogix") == 0) {
        pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_INFO, "Registering ControlLogix symbol object");
        symbol_object_controllogix_register(server.registry);
    } else if(strcmp(plc_type_str, "micro800") == 0) {
        pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_INFO, "Registering Micro800 symbol object");
        symbol_object_micro800_register(server.registry);
    } else {
        pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_ERROR, "Unknown PLC type: %s (using micro800)", plc_type_str);
        symbol_object_micro800_register(server.registry);
    }

    identity_object_register(server.registry);
    connection_manager_object_register(server.registry);

    /* Omron support disabled - this server simulates Micro800, not Omron
    server.omron_registry = omron_registry_create();
    if(!server.omron_registry) {
        pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_ERROR, "Failed to create Omron registry");
        args_free(&args_result);
        cip_registry_destroy(server.registry);
        socket_cleanup();
        return EXIT_FAILURE;
    }
    tag_name_server_omron_register(server.registry, server.omron_registry);
    variable_object_omron_register(server.registry, server.omron_registry);
    variable_type_object_omron_register(server.registry, server.omron_registry);
    */
    server.omron_registry = NULL;

    /* Omron test variables disabled
    omron_variable_create(server.omron_registry, "OmronDINT", 0xC4, 4);
    omron_variable_t *dint_var = omron_variable_find_by_name(server.omron_registry, "OmronDINT");
    if(dint_var) {
        int32_t *dint_data = (int32_t *)dint_var->data;
        *dint_data = 42;
    }

    omron_variable_create(server.omron_registry, "OmronREAL", 0xCA, 4);
    omron_variable_t *real_var = omron_variable_find_by_name(server.omron_registry, "OmronREAL");
    if(real_var) {
        float *real_data = (float *)real_var->data;
        *real_data = 3.14159f;
    }

    omron_variable_create_array(server.omron_registry, "myTag", 0xC4, 4, 10);
    omron_variable_t *array_var = omron_variable_find_by_name(server.omron_registry, "myTag");
    if(array_var) {
        int32_t *array_data = (int32_t *)array_var->data;
        for(int i = 0; i < 10; i++) { array_data[i] = i * 100; }
    }

    omron_variable_create_string(server.omron_registry, "OmronString", "Hello Omron");

    omron_type_def_t *point_type = omron_type_create(server.omron_registry, "Point", 4);
    if(point_type) {
        omron_member_add(server.omron_registry, point_type, "x", 0xC3, 0, 2);
        omron_member_add(server.omron_registry, point_type, "y", 0xC3, 2, 2);
        omron_variable_create_struct(server.omron_registry, "Point1", point_type->type_instance_id, point_type);
    }
    */

    /* Create 100+ test tags for comprehensive testing */
    server.tags = NULL;
    tag_def_t *last_tag = NULL;

    /* Create scalar DINT tags (tags 1-30) */
    for(size_t i = 1; i <= 30; i++) {
        char name[64];
        snprintf(name, sizeof(name), "Scalar_DINT_%03zu", i);
        tag_def_t *new_tag = tag_create_dint(name, (int32_t)(i * 100));
        if(new_tag) {
            if(!server.tags) { server.tags = new_tag; }
            if(last_tag) { last_tag->next = new_tag; }
            last_tag = new_tag;
        }
    }

    /* Create scalar REAL tags (tags 31-60) */
    for(size_t i = 1; i <= 30; i++) {
        char name[64];
        snprintf(name, sizeof(name), "Scalar_REAL_%03zu", i);
        tag_def_t *new_tag = tag_create_real(name, (float)i * 3.14159f);
        if(new_tag) {
            if(!server.tags) { server.tags = new_tag; }
            if(last_tag) { last_tag->next = new_tag; }
            last_tag = new_tag;
        }
    }

    /* Create 1D array DINT tags (tags 61-75) */
    for(size_t i = 1; i <= 15; i++) {
        char name[64];
        snprintf(name, sizeof(name), "Array_DINT_%03zu", i);
        tag_def_t *new_tag = tag_create_dint_array(name, 10 + i);
        if(new_tag) {
            int32_t *data = (int32_t *)new_tag->data;
            for(size_t j = 0; j < new_tag->elem_count; j++) { data[j] = (int32_t)(i * 1000 + j); }
            if(!server.tags) { server.tags = new_tag; }
            if(last_tag) { last_tag->next = new_tag; }
            last_tag = new_tag;
        }
    }

    /* Create 2D array DINT tags (tags 76-85) */
    for(size_t i = 1; i <= 10; i++) {
        char name[64];
        snprintf(name, sizeof(name), "Array2D_DINT_%03zu", i);
        size_t dims[2] = {3 + i, 4 + i};
        tag_def_t *new_tag = tag_create_dint_array_multi(name, 2, dims);
        if(new_tag) {
            int32_t *data = (int32_t *)new_tag->data;
            for(size_t j = 0; j < new_tag->elem_count; j++) { data[j] = (int32_t)(i * 10000 + j); }
            if(!server.tags) { server.tags = new_tag; }
            if(last_tag) { last_tag->next = new_tag; }
            last_tag = new_tag;
        }
    }

    /* Create 1D array REAL tags (tags 86-100) */
    for(size_t i = 1; i <= 15; i++) {
        char name[64];
        snprintf(name, sizeof(name), "Array_REAL_%03zu", i);
        tag_def_t *new_tag = tag_create_real_array_multi(name, 1, (size_t[]){5 + i});
        if(new_tag) {
            float *data = (float *)new_tag->data;
            for(size_t j = 0; j < new_tag->elem_count; j++) { data[j] = (float)(i * 100 + j) + 0.5f; }
            if(!server.tags) { server.tags = new_tag; }
            if(last_tag) { last_tag->next = new_tag; }
            last_tag = new_tag;
        }
    }

    pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_INFO, "Created 100+ test tags");

    pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_INFO, "PLC Server starting (type=%s, debug=%s)", plc_type_str, debug_str);

    /* ===== PHASE 5: COROUTINE SYSTEM INITIALIZATION ===== */
    err = coro_create(&server.coro_net, 256); /* max 256 tasks */
    if(err != UTIL_OK) {
        pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_ERROR, "Failed to create coro_net instance: %s", util_err_str(err));
        args_free(&args_result);
        socket_cleanup();
        return EXIT_FAILURE;
    }

    /* ===== PHASE 6: SIGNAL HANDLER SETUP ===== */
    util_set_interrupt_handler(signal_handler);

    /* ===== PHASE 7: LISTENER SETUP ===== */
    size_t listen_count = args_get_count(&args_result, "listen");
    if(listen_count == 0) { listen_count = 1; }

    for(size_t i = 0; i < listen_count; i++) {
        /* Get listen address, with fallback to default */
        char *listen_addr = NULL;
        if(listen_count > 0) {
            args_value_t listen_value = args_get_at(&args_result, "listen", i);
            listen_addr = (listen_value.present && listen_value.value.string_val) ? listen_value.value.string_val : NULL;
        }
        if(!listen_addr) {
            listen_addr = "0.0.0.0:44818";
        }

        /* Parse address:port */
        char addr_str[256];
        uint16_t port = 44818;
        char *colon_pos = strrchr((char *)listen_addr, ':');
        if(colon_pos) {
            ptrdiff_t addr_len_diff = (ptrdiff_t)colon_pos - (ptrdiff_t)listen_addr;
            if(addr_len_diff < 0) { addr_len_diff = 0; }
            size_t addr_len = (size_t)addr_len_diff;
            if(addr_len >= sizeof(addr_str)) { addr_len = sizeof(addr_str) - 1; }
            strncpy(addr_str, listen_addr, addr_len);
            addr_str[addr_len] = '\0';
            port = (uint16_t)strtol(colon_pos + 1, NULL, 10);
        } else {
            strncpy(addr_str, listen_addr, sizeof(addr_str) - 1);
            addr_str[sizeof(addr_str) - 1] = '\0';
        }

        /* Initialize socket address */
        socket_address_t listen_address;
        err = socket_address_init(&listen_address, addr_str, port);
        if(err != UTIL_OK) {
            pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_ERROR, "Failed to parse address %s: %s", listen_addr, util_err_str(err));
            continue;
        }

        /* Create TCP server socket */
        socket_t listen_fd = socket_create_tcp_server(&listen_address, 128);
        if(listen_fd == INVALID_SOCKET) {
            pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_ERROR, "Failed to create server socket for %s:%u", addr_str, port);
            continue;
        }

        /* Set socket options */
        socket_set_nonblocking(listen_fd, true);
        socket_set_reuseaddr(listen_fd, true);

        /* Allocate listener context */
        listener_info_t *listener_info = (listener_info_t *)malloc(sizeof(listener_info_t));
        if(!listener_info) {
            pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_ERROR, "Failed to allocate listener context");
            socket_close(listen_fd);
            continue;
        }

        memset(listener_info, 0, sizeof(*listener_info));
        listener_info->plc = &server;
        strncpy(listener_info->bind_address, addr_str, sizeof(listener_info->bind_address) - 1);
        listener_info->bind_port = port;

        /* Add listener task to event loop */
        coro_task_handle_t listener_handle;
        err = coro_add_task(&listener_handle, server.coro_net, listen_fd, listener_handler, (void *)listener_info);
        if(err != UTIL_OK) {
            pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_ERROR, "Failed to add listener task: %s", util_err_str(err));
            socket_close(listen_fd);
            free(listener_info);
            continue;
        }

        listener_info->handle = listener_handle;

        pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_INFO, "Listening on %s:%u", addr_str, port);
    }

    /* ===== PHASE 8: RUN EVENT LOOP (BLOCKING) ===== */
    pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_INFO, "Starting coroutine event loop");
    coro_run(&server.coro_net, 50); /* 50ms tick interval */

    /* ===== PHASE 9: SHUTDOWN AND CLEANUP ===== */
    pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_INFO, "PLC server shutting down");

    coro_destroy(&server.coro_net);
    server.coro_net = NULL;

    args_free(&args_result);
    socket_cleanup();

    pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_INFO, "PLC server stopped");

    return EXIT_SUCCESS;
}
