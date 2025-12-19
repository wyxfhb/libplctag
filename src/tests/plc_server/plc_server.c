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
#include "protocol/objects/symbol_object.h"
#include "protocol/objects/udt_object.h"
#include "protocol/objects/identity_object.h"
#include "protocol/objects/connection_manager.h"
#include "protocol/objects/tag_name_server_omron.h"
#include "protocol/objects/variable_object_omron.h"
#include "protocol/objects/variable_type_object_omron.h"
#include "omron_storage.h"
#include "tag_storage.h"
#include "udt_storage.h"

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
                                  {.name = "path",
                                   .type = ARGS_TYPE_STRING,
                                   .required = ARGS_OPTIONAL,
                                   .repeat = ARGS_ONCE,
                                   .description = "PLC path: 'backplane,slot' format (e.g., '1,4' for ControlLogix in slot 4)",
                                   .default_value = {.has_default = true, .value.string_val = "1,0"}},
                                  {.name = "udt",
                                   .type = ARGS_TYPE_STRING,
                                   .required = ARGS_OPTIONAL,
                                   .repeat = ARGS_MULTIPLE,
                                   .description = "UDT definition: UdtName:{field:type@offset,...}(totalSize)",
                                   .default_value = {.has_default = false}},
                                  {
                                      .name = "help",
                                      .type = ARGS_TYPE_BOOL,
                                      .required = ARGS_OPTIONAL,
                                      .repeat = ARGS_ONCE,
                                      .description = "Show this help message",
                                      .default_value = {.has_default = false},
                                  }};
static size_t num_flags = sizeof(flags) / sizeof(flags[0]);


/**
 * Type mapping for built-in CIP types
 */
static const struct {
    const char *name;
    uint16_t symbol_type;
    size_t element_size;
    size_t alignment; /* Alignment requirement in bytes */
} TYPE_MAP[] = {
    {"DINT", 0xC4, 4, 4}, {"REAL", 0xCA, 4, 4},  {"INT", 0xC3, 2, 2},  {"SINT", 0xC2, 1, 1},
    {"BOOL", 0xC1, 1, 1}, {"USINT", 0xC7, 1, 1}, {"UINT", 0xC8, 2, 2}, {NULL, 0, 0, 0} /* Sentinel */
};

/**
 * Look up a built-in type by name
 */
static uint16_t lookup_builtin_type(const char *type_name, size_t *elem_size, size_t *alignment) {
    for(int i = 0; TYPE_MAP[i].name; i++) {
        if(strcmp(type_name, TYPE_MAP[i].name) == 0) {
            if(elem_size) { *elem_size = TYPE_MAP[i].element_size; }
            if(alignment) { *alignment = TYPE_MAP[i].alignment; }
            return TYPE_MAP[i].symbol_type;
        }
    }
    return 0; /* Not found */
}

/**
 * Check if UDT A contains UDT B (directly or transitively)
 */
static bool udt_contains_udt(const udt_def_t *outer, const udt_def_t *inner, const udt_def_t *udt_head) {
    if(!outer || !inner) { return false; }

    for(size_t i = 0; i < outer->member_count; i++) {
        /* Check if this member is a UDT type */
        udt_def_t *member_udt = udt_find_by_id(udt_head, outer->members[i].symbol_type);

        if(member_udt == inner) { return true; /* Direct containment */ }

        /* Recursively check if member_udt contains inner */
        if(member_udt && udt_contains_udt(member_udt, inner, udt_head)) { return true; /* Transitive containment */ }
    }

    return false;
}

/**
 * Validate no circular dependencies in UDT registry
 */
static util_err_t validate_no_circular_dependencies(udt_def_t *udt_head) {
    for(udt_def_t *udt = udt_head; udt; udt = udt->next) {
        if(udt_contains_udt(udt, udt, udt_head)) {
            pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_ERROR, "Circular dependency detected: UDT '%s' contains itself", udt->name);
            return UTIL_EINVAL;
        }
    }
    return UTIL_OK;
}

/**
 * Parse a single UDT definition string and create UDT in registry (Pass 1 and 2)
 * Format: UdtName:{field:type@offset,...}(totalSize)
 *
 * Pass 1: Create udt_def_t with members using built-in types only
 * Pass 2: Resolve nested UDT references
 */
static util_err_t parse_udt_definition(const char *def_str, udt_def_t **udt_head, bool pass2) {
    if(!def_str || !udt_head) { return UTIL_EINVAL; }

    /* Create a mutable copy for parsing */
    char *def_copy = strdup(def_str);
    if(!def_copy) {
        pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_ERROR, "Failed to allocate memory for UDT parsing");
        return UTIL_ERESOURCE;
    }

    char *ptr = def_copy;
    char udt_name[256] = {0};
    size_t total_size = 0;

    /* Extract UDT name (before ':') */
    char *colon = strchr(ptr, ':');
    if(!colon) {
        pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_ERROR, "Invalid UDT format: missing ':' separator in '%s'", def_str);
        free(def_copy);
        return UTIL_EINVAL;
    }

    size_t name_len = (size_t)((ptrdiff_t)(colon - ptr));
    if(name_len >= sizeof(udt_name)) { name_len = sizeof(udt_name) - 1; }
    strncpy(udt_name, ptr, name_len);
    udt_name[name_len] = '\0';

    ptr = colon + 1;

    /* Find field list between '{' and '}' */
    char *open_brace = strchr(ptr, '{');
    char *close_brace = strrchr(ptr, '}');
    if(!open_brace || !close_brace || close_brace <= open_brace) {
        pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_ERROR, "Invalid UDT format: missing '{...}' in '%s'", def_str);
        free(def_copy);
        return UTIL_EINVAL;
    }

    /* Extract total size from end: find '(' and parse number before ')' */
    char *paren_open = strchr(close_brace, '(');
    char *paren_close = strchr(paren_open ? paren_open : ptr, ')');
    if(paren_open && paren_close && paren_close > paren_open) {
        char size_str[64] = {0};
        size_t size_len = paren_close - paren_open - 1;
        if(size_len >= sizeof(size_str)) { size_len = sizeof(size_str) - 1; }
        strncpy(size_str, paren_open + 1, size_len);
        size_str[size_len] = '\0';
        total_size = (size_t)atol(size_str);
    }

    if(total_size == 0) {
        pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_ERROR, "Invalid UDT format: missing or invalid (totalSize) in '%s'", def_str);
        free(def_copy);
        return UTIL_EINVAL;
    }

    /* Check if UDT already exists (only on Pass 1) */
    if(!pass2) {
        if(udt_find_by_name(*udt_head, udt_name)) {
            pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_ERROR, "UDT '%s' already defined", udt_name);
            free(def_copy);
            return UTIL_EINVAL;
        }
    }

    /* Create or find UDT */
    udt_def_t *udt;
    if(!pass2) {
        /* Pass 1: Create new UDT */
        udt = udt_create(udt_name, total_size);
        if(!udt) {
            pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_ERROR, "Failed to create UDT '%s'", udt_name);
            free(def_copy);
            return UTIL_ERESOURCE;
        }

        /* Add to linked list */
        if(!*udt_head) {
            *udt_head = udt;
        } else {
            udt_def_t *current = *udt_head;
            while(current->next) { current = current->next; }
            current->next = udt;
        }
    } else {
        /* Pass 2: Find existing UDT */
        udt = udt_find_by_name(*udt_head, udt_name);
        if(!udt) {
            pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_ERROR, "UDT '%s' not found during Pass 2", udt_name);
            free(def_copy);
            return UTIL_EINVAL;
        }
    }

    /* Parse field list */
    char *fields_start = open_brace + 1;
    size_t fields_len = close_brace - fields_start;
    if(fields_len > 0) {
        char fields_str[1024] = {0};
        if(fields_len >= sizeof(fields_str)) { fields_len = sizeof(fields_str) - 1; }
        strncpy(fields_str, fields_start, fields_len);
        fields_str[fields_len] = '\0';

        /* Split fields by comma and parse each */
        char *field_copy = strdup(fields_str);
        if(!field_copy) {
            free(def_copy);
            return UTIL_ERESOURCE;
        }

        char *field_ptr = field_copy;
        char *saveptr = NULL;
        char *field_str;

        while((field_str = strtok_r(field_ptr, ",", &saveptr)) != NULL) {
            field_ptr = NULL; /* For subsequent iterations */

            /* Parse field: fieldName:type@byteOffset[.bitOffset] or fieldName:type[count]@offset */
            char fname[256] = {0};
            char tname[256] = {0};
            size_t byte_off = 0, bit_off = 0, arr_count = 0;

            /* Extract field name (before ':') */
            char *type_sep = strchr(field_str, ':');
            if(!type_sep) { continue; }

            size_t fname_len = type_sep - field_str;
            if(fname_len >= sizeof(fname)) { fname_len = sizeof(fname) - 1; }
            strncpy(fname, field_str, fname_len);
            fname[fname_len] = '\0';

            /* Extract type and array count if present: type[count] */
            char *at_sign = strchr(type_sep + 1, '@');
            if(!at_sign) { continue; }

            size_t type_len = at_sign - (type_sep + 1);
            if(type_len >= sizeof(tname)) { type_len = sizeof(tname) - 1; }
            strncpy(tname, type_sep + 1, type_len);
            tname[type_len] = '\0';

            /* Check for array syntax: type[count] */
            char *bracket = strchr(tname, '[');
            if(bracket) {
                char *close_bracket = strchr(bracket, ']');
                if(close_bracket) {
                    char count_str[64] = {0};
                    size_t count_len = close_bracket - bracket - 1;
                    if(count_len >= sizeof(count_str)) { count_len = sizeof(count_str) - 1; }
                    strncpy(count_str, bracket + 1, count_len);
                    count_str[count_len] = '\0';
                    arr_count = (size_t)atol(count_str);
                    *bracket = '\0'; /* Terminate type name at bracket */
                }
            }

            /* Extract offsets: @byteOffset[.bitOffset] */
            char *dot = strchr(at_sign + 1, '.');
            if(dot) {
                /* Has bit offset */
                sscanf(at_sign + 1, "%zu.%zu", &byte_off, &bit_off);
            } else {
                sscanf(at_sign + 1, "%zu", &byte_off);
                bit_off = 0;
            }

            if(pass2) {
                /* Pass 2: Resolve field type (may be UDT) */
                size_t elem_size = 0, alignment = 0;
                uint16_t resolved_type = lookup_builtin_type(tname, &elem_size, &alignment);

                if(resolved_type == 0) {
                    /* Try to find as UDT */
                    udt_def_t *nested_udt = udt_find_by_name(*udt_head, tname);
                    if(nested_udt) {
                        resolved_type = nested_udt->udt_id;
                        elem_size = nested_udt->total_size;
                        alignment = 4; /* Default UDT alignment */
                    } else {
                        pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_ERROR, "UDT '%s': Unknown type '%s' for field '%s'", udt_name,
                              tname, fname);
                        free(field_copy);
                        free(def_copy);
                        return UTIL_EINVAL;
                    }
                }

                /* Find member and update symbol_type */
                for(size_t i = 0; i < udt->member_count; i++) {
                    if(strcmp(udt->members[i].name, fname) == 0) {
                        udt->members[i].symbol_type = resolved_type;
                        break;
                    }
                }
            } else {
                /* Pass 1: Add member with unresolved type (or resolved if built-in) */
                size_t elem_size = 0, alignment = 0;
                uint16_t type_code = lookup_builtin_type(tname, &elem_size, &alignment);

                if(type_code == 0) {
                    /* Not a built-in type - could be UDT, store as 0 for now */
                    type_code = 0xFFFF; /* Placeholder for unresolved UDT */
                    elem_size = 0;      /* Will be determined in Pass 2 */
                }

                uint32_t dims[3] = {0, 0, 0};
                if(arr_count > 0) {
                    dims[0] = (uint32_t)arr_count;
                    elem_size = elem_size * arr_count; /* Total size for array */
                }

                if(udt_add_member(udt, fname, type_code, byte_off, bit_off, elem_size, dims) != 0) {
                    pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_ERROR, "Failed to add member '%s' to UDT '%s'", fname, udt_name);
                    free(field_copy);
                    free(def_copy);
                    return UTIL_EINVAL;
                }
            }
        }

        free(field_copy);
    }

    free(def_copy);
    return UTIL_OK;
}

/**
 * Parse a path string like "1,4" into CIP format bytes.
 * Format: "backplane,slot" -> [backplane_byte, slot_byte]
 * Returns the number of bytes written, or 0 on error.
 */
static size_t parse_path_string(const char *path_str, uint8_t *path_bytes, size_t max_len) {
    if(!path_str || !path_bytes || max_len < 2) { return 0; }

    int backplane = 0;
    int slot = 0;

    /* Parse "backplane,slot" format */
    if(sscanf(path_str, "%d,%d", &backplane, &slot) != 2) {
        pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_ERROR, "Invalid path format: %s (expected 'backplane,slot')", path_str);
        return 0;
    }

    if(backplane < 0 || backplane > 255 || slot < 0 || slot > 255) {
        pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_ERROR, "Path out of range: %d,%d (must be 0-255)", backplane, slot);
        return 0;
    }

    /* Convert to CIP path format: just [backplane, slot] as individual bytes */
    path_bytes[0] = (uint8_t)backplane;
    path_bytes[1] = (uint8_t)slot;

    pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_DETAIL, "Parsed path '%s' to CIP bytes: 0x%02X 0x%02X", path_str, path_bytes[0],
          path_bytes[1]);

    return 2;
}


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
    if(!plc_type_str) { plc_type_str = "micro800"; /* fallback default */ }

    /* Parse and configure PLC path */
    char *path_str = args_get_string(&args_result, "path");
    if(!path_str) { path_str = "1,0"; /* fallback default */ }
    server.path_len = parse_path_string(path_str, server.path, sizeof(server.path));
    if(server.path_len == 0) {
        pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_ERROR, "Failed to parse PLC path: %s", path_str);
        args_free(&args_result);
        cip_registry_destroy(server.registry);
        socket_cleanup();
        return EXIT_FAILURE;
    }

    if(strcmp(plc_type_str, "controllogix") == 0) {
        pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_INFO, "Registering unified symbol object for ControlLogix");
        server.plc_type = PLC_TYPE_CONTROLLOGIX;
        symbol_object_register(server.registry);
        /* Note: UDT Object registration will happen after UDT parsing in Phase 5 */
    } else if(strcmp(plc_type_str, "micro800") == 0) {
        pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_INFO, "Registering unified symbol object for Micro800");
        server.plc_type = PLC_TYPE_MICRO800;
        symbol_object_register(server.registry);
    } else {
        pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_ERROR, "Unknown PLC type: %s (using micro800)", plc_type_str);
        server.plc_type = PLC_TYPE_MICRO800;
        symbol_object_register(server.registry);
    }

    identity_object_register(server.registry);
    connection_manager_object_register(server.registry);

    /* ===== PHASE 5: UDT (USER-DEFINED TYPE) PARSING (2-PASS SYSTEM) ===== */
    server.udts = NULL;

    /* Reset UDT ID counter */
    udt_reset_id_counter();

    /* Get count of --udt arguments */
    size_t udt_count = args_get_count(&args_result, "udt");

    if(udt_count > 0) {
        /* Pass 1: Create all UDT structures from command-line arguments */
        for(size_t i = 0; i < udt_count; i++) {
            args_value_t val = args_get_at(&args_result, "udt", i);
            if(!val.present) {
                pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_ERROR, "Failed to get UDT definition at index %zu", i);
                args_free(&args_result);
                cip_registry_destroy(server.registry);
                udt_destroy_all(server.udts);
                socket_cleanup();
                return EXIT_FAILURE;
            }
            err = parse_udt_definition(val.value.string_val, &server.udts, false);
            if(err != UTIL_OK) {
                pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_ERROR, "Pass 1: Failed to parse UDT definition: %s", val.value.string_val);
                args_free(&args_result);
                cip_registry_destroy(server.registry);
                udt_destroy_all(server.udts);
                socket_cleanup();
                return EXIT_FAILURE;
            }
        }

        /* Pass 2: Resolve nested UDT references */
        for(size_t i = 0; i < udt_count; i++) {
            args_value_t val = args_get_at(&args_result, "udt", i);
            if(!val.present) { continue; }
            err = parse_udt_definition(val.value.string_val, &server.udts, true);
            if(err != UTIL_OK) {
                pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_ERROR, "Pass 2: Failed to resolve UDT definition: %s",
                      val.value.string_val);
                args_free(&args_result);
                cip_registry_destroy(server.registry);
                udt_destroy_all(server.udts);
                socket_cleanup();
                return EXIT_FAILURE;
            }
        }

        /* Validate no circular dependencies */
        err = validate_no_circular_dependencies(server.udts);
        if(err != UTIL_OK) {
            pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_ERROR, "UDT validation failed: circular dependencies detected");
            args_free(&args_result);
            cip_registry_destroy(server.registry);
            udt_destroy_all(server.udts);
            socket_cleanup();
            return EXIT_FAILURE;
        }

        /* Log UDT summary */
        for(udt_def_t *udt = server.udts; udt; udt = udt->next) {
            pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_INFO, "Created UDT '%s' (ID: 0x%04X) with %zu members, %zu bytes total",
                  udt->name, udt->udt_id, udt->member_count, udt->total_size);
        }
    }

    /* Register UDT Definition Object (Class 0x6C) for ControlLogix and Micro800 (always, even if no UDTs defined) */
    if(server.plc_type == PLC_TYPE_CONTROLLOGIX || server.plc_type == PLC_TYPE_MICRO800) {
        if(udt_object_register(server.registry, &server) != 0) {
            pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_ERROR, "Failed to register UDT Definition Object");
            args_free(&args_result);
            cip_registry_destroy(server.registry);
            udt_destroy_all(server.udts);
            socket_cleanup();
            return EXIT_FAILURE;
        }
    }

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

    /* Initialize tag list (will be populated below with both UDT-based and regular tags) */
    server.tags = NULL;
    tag_def_t *last_tag = NULL;

    /* Create UDT-based tags FIRST (Motor type) if Motor UDT exists */
    if(server.udts) {
        for(size_t i = 1; i <= 5; i++) {
            char name[64];
            snprintf(name, sizeof(name), "Motor_%03zu", i);

            /* Create a Motor UDT tag (12 bytes) */
            tag_def_t *new_tag = tag_create(name, 0xC4, 12, 1); /* 0xC4 = DINT (placeholder; actual type determined by UDT) */
            if(new_tag) {
                new_tag->udt_id = 0x0001; /* Motor UDT ID */

                /* Initialize Motor data: speed (DINT@0), torque (REAL@4), status (INT@8) */
                int32_t *speed = (int32_t *)&new_tag->data[0];
                float *torque = (float *)&new_tag->data[4];
                int16_t *status = (int16_t *)&new_tag->data[8];

                *speed = (int32_t)(i * 500);  /* speed in RPM */
                *torque = (float)(i * 12.5f); /* torque in Nm */
                *status = (int16_t)(i % 2);   /* 0=off, 1=on */

                if(!server.tags) { server.tags = new_tag; }
                if(last_tag) { last_tag->next = new_tag; }
                last_tag = new_tag;

                pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_INFO, "Created Motor UDT tag '%s' (UDT ID: 0x%04X)", name,
                      new_tag->udt_id);
            }
        }
    }

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
    /* (server.tags and last_tag already initialized above for UDT-based tags) */

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
        if(!listen_addr) { listen_addr = "0.0.0.0:44818"; }

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
