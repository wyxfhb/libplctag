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
#include "protocol/objects/logix_symbol_object.h"
#include "protocol/objects/udt_object.h"
#include "protocol/objects/identity_object.h"
#include "protocol/objects/connection_manager.h"
#include "protocol/objects/pccc_object.h"
#include "protocol/objects/tag_name_server_omron.h"
#include "protocol/objects/variable_object_omron.h"
#include "protocol/objects/variable_type_object_omron.h"
#include "tag_storage.h"
#include "udt_storage.h"

/* ============================================================================
 * UDT Type Resolution Constants and Structures
 * ============================================================================ */

/* High bit set in symbol_type indicates it's a temporary lookup table index, not a real ID */
#define SYSTEM_TYPE_BIT 0x8000
#define MAX_TEMP_LOOKUP_ENTRIES 4096
#define MAX_RECURSION_DEPTH 8

/**
 * Temporary lookup table entry for resolving UDT names to their final IDs.
 * Used during Pass 1 to handle forward references.
 */
typedef struct {
    char name[256];   /* UDT name */
    uint16_t real_id; /* Real instance ID, 0 if not yet defined */
} udt_lookup_entry_t;

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
    size_t receive_capacity = (client->client_to_server_max_packet <= sizeof(client->recv_buffer)) ?
                                  client->client_to_server_max_packet :
                                  sizeof(client->recv_buffer);
    size_t send_capacity = (client->server_to_client_max_packet <= sizeof(client->send_buffer)) ?
                               client->server_to_client_max_packet :
                               sizeof(client->send_buffer);

    (void)fd; /* fd is available via coro_get_fd(handle) if needed */

    CORO_START(handle);

    pdlog(LOG_MODULE_PLC_CLIENT, LOG_LEVEL_INFO, "Client handler started");

    while(1) {
        pdlog(LOG_MODULE_PLC_CLIENT, LOG_LEVEL_DETAIL, "Receive buffer capacity: %zu bytes", buf_capacity(&client->recv_buf));
        pdlog(LOG_MODULE_PLC_CLIENT, LOG_LEVEL_DETAIL, "Send buffer capacity: %zu bytes", buf_capacity(&client->send_buf));

        /* Compact receive buffer to maximize space for new data */
        buf_compact(&client->recv_buf);

        size_t available_data = buf_read_size(&client->recv_buf);
        pdlog(LOG_MODULE_PLC_CLIENT, LOG_LEVEL_SPEW, "Receive buffer: %zu/%zu bytes remaining", available_data, receive_capacity);

        /* clamp the receive capacity to the negotiated receive capacity */
        if(receive_capacity != buf_capacity(&client->recv_buf)) {
            pdlog(LOG_MODULE_PLC_CLIENT, LOG_LEVEL_DETAIL, "Resizing receive buffer to %zu bytes", receive_capacity);

            client->recv_buf = buf_init(client->recv_buffer, receive_capacity);

            /* now make sure that the remaining data after the compaction above is retained. */
            buf_t tmp_buf = {0};
            if(available_data > 0 && !buf_reserve_write(&client->recv_buf, available_data, &tmp_buf)) {
                pdlog(LOG_MODULE_PLC_CLIENT, LOG_LEVEL_WARN, "Failed to resize receive buffer");
                break;
            }
        }

        /* clamp the send capacity to the negotiated send capacity */
        if(send_capacity != buf_capacity(&client->send_buf)) {
            pdlog(LOG_MODULE_PLC_CLIENT, LOG_LEVEL_DETAIL, "Resizing send buffer to %zu bytes", send_capacity);
            client->send_buf = buf_init(client->send_buffer, send_capacity);
        }


        /* make sure that the receive buffer is the right size */
        if(client->client_to_server_max_packet > sizeof(client->recv_buffer)) {
            pdlog(LOG_MODULE_PLC_CLIENT, LOG_LEVEL_WARN,
                  "Client's negotiated max packet size (%u bytes) exceeds buffer size (%zu bytes), closing connection",
                  client->client_to_server_max_packet, sizeof(client->recv_buffer));
            break;
        }


        pdlog(LOG_MODULE_PLC_CLIENT, LOG_LEVEL_DETAIL, "Going to wait for data from the client.");

        /* Read EIP frame with automatic retry on incomplete frame */
        stream_read_yield(handle, &client->recv_buf, eip_frame_check, client, NULL, NULL, err);

        if(err != UTIL_OK) {
            pdlog(LOG_MODULE_PLC_CLIENT, LOG_LEVEL_WARN, "Client read failed: %s", util_err_str(err));
            break;
        }

        pdlog(LOG_MODULE_PLC_CLIENT, LOG_LEVEL_DETAIL, "Received data:");
        pdlog_bytes(LOG_MODULE_PLC_CLIENT, LOG_LEVEL_DETAIL, &client->recv_buf);

        /* Reset send buffer for response building */
        buf_reset(&client->send_buf);

        /* INJECT NON-BLOCKING RESPONSE DELAY (if configured) */
        plc_context_t *plc = client->plc;
        if(plc->response_delay_ms > 0) {
            /* Start delay if not already active */
            if(!client->delay_active) {
                client->delay_start_us = util_time_us();
                client->delay_active = true;
                pdlog(LOG_MODULE_PLC_CLIENT, LOG_LEVEL_DETAIL, "Starting %dms response delay", plc->response_delay_ms);
            }

            /* Check if delay period has elapsed */
            int64_t elapsed_us = util_time_us() - client->delay_start_us;
            int64_t required_us = plc->response_delay_ms * 1000;

            if(elapsed_us < required_us) {
                /* Delay not complete, yield and run again on next loop iteration */
                pdlog(LOG_MODULE_PLC_CLIENT, LOG_LEVEL_SPEW, "Delay in progress: %lld/%lld us", (long long)elapsed_us,
                      (long long)required_us);
                coro_wait_for_event(handle, CORO_EVENT_ALWAYS);
                continue; /* Go back to top of while loop to check delay again */
            }

            /* Delay complete */
            client->delay_active = false;
            pdlog(LOG_MODULE_PLC_CLIENT, LOG_LEVEL_DETAIL, "Delay complete after %lld us", (long long)elapsed_us);
        }

        /* Dispatch EIP request */
        err = eip_dispatch(&client->recv_buf, &client->send_buf, client);

        if(err != UTIL_OK) {
            pdlog(LOG_MODULE_PLC_CLIENT, LOG_LEVEL_DETAIL, "EIP dispatch returned: %s", util_err_str(err));
            /* Continue - error response was already built */
        }

        pdlog(LOG_MODULE_PLC_CLIENT, LOG_LEVEL_DETAIL, "Response data:");
        pdlog_bytes(LOG_MODULE_PLC_CLIENT, LOG_LEVEL_DETAIL, &client->send_buf);


        /* Send response */
        stream_write_yield(handle, &client->send_buf, err);

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

    /* start somewhere */
    size_t c2s_capacity = 256;
    size_t s2c_capacity = 256;

    (void)fd;

    CORO_START(handle);

    pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_INFO, "Listener started on %s:%u", listener->bind_address, listener->bind_port);

    while(listener->plc->running) {
        /* YIELD: Wait for incoming connection */
        stream_listener_accept_yield(handle, &client_fd, &client_addr, err);

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

        switch(listener->plc->plc_type) {
            case PLC_TYPE_MICRO800:
                c2s_capacity = 500;
                s2c_capacity = 500;
                break;
            case PLC_TYPE_CONTROLLOGIX:
                c2s_capacity = 500;
                s2c_capacity = 500;
                break;
            case PLC_TYPE_OMRON:
                c2s_capacity = 500;
                s2c_capacity = 500;
                break;
            default:
                c2s_capacity = 244;
                s2c_capacity = 244;
                break;
        }

        client->recv_buf = buf_init(client->recv_buffer, c2s_capacity);
        client->send_buf = buf_init(client->send_buffer, s2c_capacity);

        /* Initialize delay state for non-blocking response delay */
        client->delay_active = false;
        client->delay_start_us = 0;

        /* set up the local FO rejection count */
        client->reject_fo_count = listener->plc->reject_fo_count;

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
                                  {.name = "tag",
                                   .type = ARGS_TYPE_STRING,
                                   .required = ARGS_OPTIONAL,
                                   .repeat = ARGS_MULTIPLE,
                                   .description = "Tag: name:type[dim1] or name:type[dim1,dim2] or name:type[dim1,dim2,dim3]",
                                   .default_value = {.has_default = false}},
                                  {.name = "delay",
                                   .type = ARGS_TYPE_STRING,
                                   .required = ARGS_OPTIONAL,
                                   .repeat = ARGS_ONCE,
                                   .description = "Delay all responses by N milliseconds (for timeout testing)",
                                   .default_value = {.has_default = false}},
                                  {.name = "reject-fo",
                                   .type = ARGS_TYPE_STRING,
                                   .required = ARGS_OPTIONAL,
                                   .repeat = ARGS_ONCE,
                                   .description = "Fail first N ForwardOpen requests with 'connection ID already in use' error",
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

/* ============================================================================
 * PCCC Type Support (for PLC/5, SLC 500, MicroLogix)
 * ============================================================================ */

/**
 * PCCC type mapping: Maps type prefix to PCCC type code and element size
 * Used for parsing tags like "N7[100]", "F8[50]", "ST18[10]"
 */
static const struct {
    const char *prefix; /* Type prefix: "N", "F", "B", "L", "ST" */
    uint16_t type_code; /* PCCC type code (0x85, 0x89, 0x8A, 0x8D, 0x91) */
    size_t elem_size;   /* Element size in bytes */
} PCCC_TYPE_MAP[] = {
    {"N", 0x89, 2},   /* INT: signed 16-bit integer */
    {"F", 0x8A, 4},   /* REAL: 32-bit IEEE floating point */
    {"B", 0x85, 2},   /* BOOL: 1-bit boolean as 16-bit unsigned */
    {"L", 0x91, 4},   /* DINT: signed 32-bit integer */
    {"ST", 0x8D, 84}, /* STRING: 82-byte ASCII + 2-byte count */
    {NULL, 0, 0}      /* Sentinel */
};

/**
 * Parse PCCC tag format: "type_prefix file_number[count]"
 * Examples: "N7[100]" (INT file 7, 100 elements)
 *           "F8[50]"  (REAL file 8, 50 elements)
 *           "ST18[10]" (STRING file 18, 10 elements)
 *
 * Returns true on success, false on parse error
 */
static bool parse_pccc_tag(const char *tag_str, char *out_name, uint16_t *out_type_code, size_t *out_elem_size,
                           size_t *out_elem_count) {
    if(!tag_str || !out_name || !out_type_code || !out_elem_size || !out_elem_count) { return false; }

    /* Extract type prefix (1 or 2 letters: "N", "F", "B", "L", "ST") */
    char type_prefix[4];
    int file_num = 0;
    int elem_count = 0;

    /* Try parsing with scanf: prefix + number + [count] */
    int parsed = sscanf(tag_str, "%3[A-Za-z]%d[%d]", type_prefix, &file_num, &elem_count);

    if(parsed != 3) {
        pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_ERROR,
              "Invalid PCCC tag format '%s': expected 'TYPE_PREFIX file_num[count]' (e.g., 'N7[100]')", tag_str);
        return false;
    }

    if(file_num <= 0 || elem_count <= 0) {
        pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_ERROR, "PCCC tag '%s': file number and count must be > 0", tag_str);
        return false;
    }

    /* Look up type prefix in PCCC type map */
    uint16_t type_code = 0;
    size_t elem_size = 0;
    for(int i = 0; PCCC_TYPE_MAP[i].prefix != NULL; i++) {
        if(strcmp(type_prefix, PCCC_TYPE_MAP[i].prefix) == 0) {
            type_code = PCCC_TYPE_MAP[i].type_code;
            elem_size = PCCC_TYPE_MAP[i].elem_size;
            break;
        }
    }

    if(type_code == 0) {
        pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_ERROR, "PCCC tag '%s': unknown type prefix '%s' (valid: N, F, B, L, ST)", tag_str,
              type_prefix);
        return false;
    }

    /* Construct tag name as "PREFIX + FILE_NUM" (e.g., "N7", "F8", "ST18") */
    snprintf(out_name, 256, "%s%d", type_prefix, file_num);

    *out_type_code = type_code;
    *out_elem_size = elem_size;
    *out_elem_count = (size_t)elem_count;

    return true;
}

/**
 * Check if a tag string is PCCC format (no colon)
 * CIP format has colon: "name:type[dim]"
 * PCCC format has no colon: "N7[100]"
 */
static bool is_pccc_tag(const char *tag_str) { return (strchr(tag_str, ':') == NULL); }

/**
 * Find a UDT name in the temporary lookup table
 * Returns the index if found, or -1 if not found
 */
static int udt_lookup_find_by_name(udt_lookup_entry_t *lookup_table, size_t lookup_count, const char *name) {
    if(!lookup_table || !name) { return -1; }

    for(size_t i = 0; i < lookup_count; i++) {
        if(strcmp(lookup_table[i].name, name) == 0) { return (int)i; }
    }
    return -1;
}

/**
 * Add a UDT name to the temporary lookup table
 * Returns the index of the new entry, or -1 on error
 */
static int udt_lookup_add_entry(udt_lookup_entry_t *lookup_table, size_t *lookup_count, const char *name, uint16_t real_id) {
    if(!lookup_table || !lookup_count || !name) { return -1; }

    if(*lookup_count >= MAX_TEMP_LOOKUP_ENTRIES) {
        pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_ERROR, "Temporary lookup table full (max %u entries)", MAX_TEMP_LOOKUP_ENTRIES);
        return -1;
    }

    size_t index = *lookup_count;
    strncpy(lookup_table[index].name, name, sizeof(lookup_table[index].name) - 1);
    lookup_table[index].name[sizeof(lookup_table[index].name) - 1] = '\0';
    lookup_table[index].real_id = real_id;
    (*lookup_count)++;

    return (int)index;
}

/**
 * Recursively fixup field entry type and size references
 * Converts temporary lookup table indices (with SYSTEM_TYPE_BIT set) to real UDT IDs
 * and calculates sizes for nested struct types
 */
static util_err_t fixup_field_entry(size_t entry_index, udt_entry_t *entries, size_t entry_count,
                                    udt_lookup_entry_t *lookup_table, size_t lookup_count, int recursion_depth) {
    if(recursion_depth > MAX_RECURSION_DEPTH) {
        pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_ERROR, "Type resolution recursion depth exceeded (possible cycle)");
        return UTIL_EINVAL;
    }

    if(entry_index >= entry_count) { return UTIL_EINVAL; }

    udt_entry_t *entry = &entries[entry_index];

    /* If this is a UDT entry, we don't need to fixup */
    if(entry->entry_type == UDT_ENTRY_TYPE_DEF) { return UTIL_OK; }

    if(entry->entry_type != UDT_ENTRY_FIELD) { return UTIL_EINVAL; }

    /* Check if symbol_type has the SYSTEM_TYPE_BIT set (temporary lookup index) */
    if(entry->data.field.symbol_type & SYSTEM_TYPE_BIT) {
        /* Extract the lookup table index */
        uint16_t lookup_index = entry->data.field.symbol_type & 0x0FFF;

        if(lookup_index >= lookup_count) {
            pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_ERROR, "Field '%s': Invalid lookup table index %u", entry->name, lookup_index);
            return UTIL_EINVAL;
        }

        uint16_t real_id = lookup_table[lookup_index].real_id;
        if(real_id == 0) {
            pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_ERROR, "Field '%s': Type '%s' not found", entry->name,
                  lookup_table[lookup_index].name);
            return UTIL_EINVAL;
        }

        /* Replace temporary marker with real ID */
        entry->data.field.symbol_type = real_id;
    }

    /* Now the symbol_type has the real UDT ID or built-in type code */

    /* If size is still zero, it must be a struct type - calculate it */
    if(entry->data.field.element_length == 0) {
        uint16_t type_id = entry->data.field.symbol_type;

        /* If it's a built-in type (not in UDT range), this is an error */
        if(type_id < 1 || type_id > 4095) {
            /* Built-in type should have been sized in Pass 1 */
            pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_ERROR, "Field '%s': Zero size for non-struct type 0x%X", entry->name, type_id);
            return UTIL_EINVAL;
        }

        /* It's a struct type - find it and get its size */
        if(type_id >= entry_count) {
            pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_ERROR, "Field '%s': Invalid type ID %u (out of range)", entry->name, type_id);
            return UTIL_EINVAL;
        }

        udt_entry_t *struct_type = &entries[type_id - 1]; /* IDs are 1-based */

        if(struct_type->entry_type != UDT_ENTRY_TYPE_DEF) {
            pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_ERROR, "Field '%s': Type ID %u is not a struct definition", entry->name,
                  type_id);
            return UTIL_EINVAL;
        }

        /* Recursively fixup the struct type if it hasn't been fixed yet */
        if(struct_type->data.udt.total_size == 0) {
            /* Need to fixup fields of this struct first to calculate its size */
            uint16_t field_count = struct_type->data.udt.field_count;
            for(size_t i = 0; i < field_count; i++) {
                util_err_t err =
                    fixup_field_entry(type_id + i, entries, entry_count, lookup_table, lookup_count, recursion_depth + 1);
                if(err != UTIL_OK) { return err; }
            }
        }

        /* Calculate total size: struct size × array dimensions */
        size_t base_size = struct_type->data.udt.total_size;
        size_t total_size = base_size;

        for(int d = 0; d < 3; d++) {
            if(entry->dimensions[d] > 0) { total_size *= entry->dimensions[d]; }
        }

        entry->data.field.element_length = total_size;
    }

    return UTIL_OK;
}

/**
 * Parse a single UDT definition string and create UDT in registry (Pass 1 and 2)
 * Format: UdtName:{field:type@offset,...}(totalSize)
 *
 * Pass 1: Create UDT entries with temporary lookup markers for unresolved types
 * Pass 2: Resolve all type references via fixup_field_entry
 */
static util_err_t parse_udt_definition(const char *def_str, udt_entry_t **entries, size_t *count, size_t *capacity,
                                       udt_lookup_entry_t *lookup_table, size_t *lookup_count, bool pass2) {
    if(!def_str || !entries || !count || !capacity || !lookup_table || !lookup_count) { return UTIL_EINVAL; }

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
        size_t size_len = (size_t)(paren_close - paren_open - 1);
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
        if(udt_find_by_name(*entries, *count, udt_name)) {
            pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_ERROR, "UDT '%s' already defined", udt_name);
            free(def_copy);
            return UTIL_EINVAL;
        }
    }

    /* Find or create UDT entry */
    udt_entry_t *udt_entry = NULL;
    uint16_t udt_instance_id = 0;
    uint16_t field_count = 0;

    if(!pass2) {
        /* Pass 1: Count fields first, then add UDT entry and manage lookup table */
        char *fields_start = open_brace + 1;
        size_t fields_len = (size_t)((ptrdiff_t)(close_brace - fields_start));
        if(fields_len > 0) {
            char fields_str[1024] = {0};
            if(fields_len >= sizeof(fields_str)) { fields_len = sizeof(fields_str) - 1; }
            strncpy(fields_str, fields_start, fields_len);
            fields_str[fields_len] = '\0';

            /* Count fields by commas */
            field_count = 1;
            for(size_t i = 0; i < fields_len; i++) {
                if(fields_str[i] == ',') { field_count++; }
            }
        }

        /* Check lookup table for existing entry */
        int lookup_index = udt_lookup_find_by_name(lookup_table, *lookup_count, udt_name);
        if(lookup_index >= 0) {
            /* Entry exists in lookup table */
            if(lookup_table[lookup_index].real_id != 0) {
                pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_ERROR, "UDT '%s' already defined", udt_name);
                free(def_copy);
                return UTIL_EINVAL;
            }
            /* Entry exists but real_id is 0 - this is a forward reference that's now being defined */
        }

        /* Add UDT entry to array */
        udt_instance_id = udt_add_type_def(entries, count, capacity, udt_name, total_size, field_count);
        if(udt_instance_id == 0) {
            pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_ERROR, "Failed to add UDT entry '%s'", udt_name);
            free(def_copy);
            return UTIL_ERESOURCE;
        }
        udt_entry = udt_get_by_id(*entries, *count, udt_instance_id);

        /* Update or create lookup table entry */
        if(lookup_index >= 0) {
            /* Update existing entry with real ID */
            lookup_table[lookup_index].real_id = udt_instance_id;
        } else {
            /* Create new entry */
            lookup_index = udt_lookup_add_entry(lookup_table, lookup_count, udt_name, udt_instance_id);
            if(lookup_index < 0) {
                pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_ERROR, "Failed to add lookup table entry for UDT '%s'", udt_name);
                free(def_copy);
                return UTIL_ERESOURCE;
            }
        }
    } else {
        /* Pass 2: Find existing UDT entry */
        udt_entry = udt_find_by_name(*entries, *count, udt_name);
        if(!udt_entry) {
            pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_ERROR, "UDT '%s' not found during Pass 2", udt_name);
            free(def_copy);
            return UTIL_EINVAL;
        }
        udt_instance_id = udt_entry->instance_id;
        field_count = (uint16_t)udt_entry->dimensions[0];
    }

    /* Parse field list */
    char *fields_start = open_brace + 1;
    size_t fields_len = (size_t)((ptrdiff_t)(close_brace - fields_start));
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

            size_t fname_len = (size_t)((ptrdiff_t)(type_sep - field_str));
            if(fname_len >= sizeof(fname)) { fname_len = sizeof(fname) - 1; }
            strncpy(fname, field_str, fname_len);
            fname[fname_len] = '\0';

            /* Extract type and array count if present: type[count] */
            char *at_sign = strchr(type_sep + 1, '@');
            if(!at_sign) { continue; }

            size_t type_len = (size_t)((ptrdiff_t)(at_sign - (type_sep + 1)));
            if(type_len >= sizeof(tname)) { type_len = sizeof(tname) - 1; }
            strncpy(tname, type_sep + 1, type_len);
            tname[type_len] = '\0';

            /* Check for array syntax: type[count] */
            char *bracket = strchr(tname, '[');
            if(bracket) {
                char *close_bracket = strchr(bracket, ']');
                if(close_bracket) {
                    char count_str[64] = {0};
                    size_t count_len = (size_t)((ptrdiff_t)(close_bracket - bracket - 1));
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
                    udt_entry_t *nested_udt = udt_find_by_name(*entries, *count, tname);
                    if(nested_udt && nested_udt->entry_type == UDT_ENTRY_TYPE_DEF) {
                        resolved_type = nested_udt->instance_id; /* Use instance ID as nested type ID */
                        elem_size = nested_udt->data.udt.total_size;
                        alignment = 4; /* Default UDT alignment */
                    } else {
                        pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_ERROR, "UDT '%s': Unknown type '%s' for field '%s'", udt_name,
                              tname, fname);
                        free(field_copy);
                        free(def_copy);
                        return UTIL_EINVAL;
                    }
                }

                /* Find field entry and update symbol_type */
                for(size_t i = udt_instance_id; i < *count; i++) {
                    udt_entry_t *field_entry = &(*entries)[i];
                    if(field_entry->entry_type == UDT_ENTRY_FIELD && strcmp(field_entry->name, fname) == 0) {
                        field_entry->data.field.symbol_type = resolved_type;
                        break;
                    }
                }
            } else {
                /* Pass 1: Add field with resolved or temporary type marker */
                size_t elem_size = 0, alignment = 0;
                uint16_t type_code = lookup_builtin_type(tname, &elem_size, &alignment);

                if(type_code == 0) {
                    /* Not a built-in type - could be a UDT */
                    /* Look up or create an entry in the temp lookup table */
                    int lookup_idx = udt_lookup_find_by_name(lookup_table, *lookup_count, tname);
                    if(lookup_idx < 0) {
                        /* Create a new entry with real_id = 0 (not yet defined) */
                        lookup_idx = udt_lookup_add_entry(lookup_table, lookup_count, tname, 0);
                        if(lookup_idx < 0) {
                            pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_ERROR, "Failed to add lookup table entry for type '%s'",
                                  tname);
                            free(field_copy);
                            free(def_copy);
                            return UTIL_ERESOURCE;
                        }
                    }
                    /* Store lookup table index with SYSTEM_TYPE_BIT set */
                    type_code = SYSTEM_TYPE_BIT | (uint16_t)lookup_idx;
                    elem_size = 0; /* Will be calculated in Pass 2 */
                }

                uint32_t dims[3] = {0, 0, 0};
                if(arr_count > 0) {
                    dims[0] = (uint32_t)arr_count;
                    if(!(type_code & SYSTEM_TYPE_BIT)) {
                        /* For built-in array types, multiply size */
                        elem_size = elem_size * arr_count;
                    }
                    /* For struct array types, elem_size stays 0 until Pass 2 fixup */
                }

                uint16_t field_id = udt_add_field(entries, count, capacity, fname, type_code, byte_off, bit_off, elem_size, dims);
                if(field_id == 0) {
                    pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_ERROR, "Failed to add field '%s' to UDT '%s'", fname, udt_name);
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

    /* Initialize testing/debugging features to disabled */
    server.response_delay_ms = 0;
    server.reject_fo_count = 0;

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
        logix_symbol_object_register(server.registry);
        /* Note: UDT Object registration will happen after UDT parsing */
    } else if(strcmp(plc_type_str, "micro800") == 0) {
        pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_INFO, "Registering unified symbol object for Micro800");
        server.plc_type = PLC_TYPE_MICRO800;
        logix_symbol_object_register(server.registry);
    } else if(strcmp(plc_type_str, "plc5") == 0) {
        pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_INFO, "Registering PLC/5 with PCCC protocol support");
        server.plc_type = PLC_TYPE_PLC5;
        /* PLC/5 uses PCCC protocol */
        pccc_object_register(server.registry);
        // identity_object_register(server.registry);
        // connection_manager_object_register(server.registry);
    } else if(strcmp(plc_type_str, "slc") == 0 || strcmp(plc_type_str, "slc500") == 0
              || strcmp(plc_type_str, "micrologix") == 0) {
        if(strcmp(plc_type_str, "micrologix") == 0) {
            pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_INFO, "Registering MicroLogix with PCCC protocol support");
            server.plc_type = PLC_TYPE_MICROLOGIX;
        } else {
            pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_INFO, "Registering SLC 500 with PCCC protocol support");
            server.plc_type = PLC_TYPE_SLC;
        }
        /* SLC 500 and MicroLogix use PCCC protocol */
        pccc_object_register(server.registry);
        // identity_object_register(server.registry);
        // connection_manager_object_register(server.registry);
    } else if(strcmp(plc_type_str, "omron") == 0) {
        pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_INFO, "Registering Omron object model");
        server.plc_type = PLC_TYPE_OMRON;
        // variable_object_omron_register(server.registry, plc);
    } else {
        /* FIXME - do not default */
        pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_ERROR,
              "Unknown PLC type: %s (use controllogix|micro800|plc5|slc|micrologix|omron)", plc_type_str);
        pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_INFO, "Using micro800 as default");
        server.plc_type = PLC_TYPE_MICRO800;
        logix_symbol_object_register(server.registry);
    }

    identity_object_register(server.registry);
    connection_manager_object_register(server.registry);

    /* ===== PHASE 5: UDT ARRAY INITIALIZATION ===== */
    /* Initialize UDT array for UDT definitions and field definitions */
    if(udt_array_init(&server.udts, &server.udt_capacity) != 0) {
        pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_ERROR, "Failed to initialize UDT array");
        args_free(&args_result);
        cip_registry_destroy(server.registry);
        socket_cleanup();
        return EXIT_FAILURE;
    }
    server.udt_count = 0;

    /* Temporary lookup table for resolving forward references */
    udt_lookup_entry_t temp_lookup[MAX_TEMP_LOOKUP_ENTRIES];
    size_t temp_lookup_count = 0;

    /* Get count of --udt arguments */
    size_t udt_count = args_get_count(&args_result, "udt");

    if(udt_count > 0) {
        /* Parse UDT definitions into class 0x6C array (2-pass for nested type resolution) */
        for(size_t i = 0; i < udt_count; i++) {
            args_value_t val = args_get_at(&args_result, "udt", i);
            if(!val.present) {
                pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_ERROR, "Failed to get UDT definition at index %zu", i);
                args_free(&args_result);
                cip_registry_destroy(server.registry);
                udt_array_destroy(server.udts, server.udt_count);
                socket_cleanup();
                return EXIT_FAILURE;
            }
            err = parse_udt_definition(val.value.string_val, &server.udts, &server.udt_count, &server.udt_capacity, temp_lookup,
                                       &temp_lookup_count, false);
            if(err != UTIL_OK) {
                pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_ERROR, "Failed to parse UDT definition: %s", val.value.string_val);
                args_free(&args_result);
                cip_registry_destroy(server.registry);
                udt_array_destroy(server.udts, server.udt_count);
                socket_cleanup();
                return EXIT_FAILURE;
            }
        }

        /* Pass 2: Resolve nested UDT references and fixup field entries */
        for(size_t i = 0; i < udt_count; i++) {
            args_value_t val = args_get_at(&args_result, "udt", i);
            if(!val.present) { continue; }
            err = parse_udt_definition(val.value.string_val, &server.udts, &server.udt_count, &server.udt_capacity, temp_lookup,
                                       &temp_lookup_count, true);
            if(err != UTIL_OK) {
                pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_ERROR, "Pass 2: Failed to resolve UDT definition: %s",
                      val.value.string_val);
                args_free(&args_result);
                cip_registry_destroy(server.registry);
                udt_array_destroy(server.udts, server.udt_count);
                socket_cleanup();
                return EXIT_FAILURE;
            }
        }

        /* Pass 3: Fixup all field entries to resolve type references and calculate sizes */
        for(size_t i = 0; i < server.udt_count; i++) {
            udt_entry_t *entry = &server.udts[i];

            if(entry->entry_type == UDT_ENTRY_TYPE_DEF) {
                /* For each UDT, fixup its fields */
                uint16_t field_count = entry->data.udt.field_count;
                for(size_t j = 0; j < field_count; j++) {
                    err = fixup_field_entry(i + 1 + j, server.udts, server.udt_count, temp_lookup, temp_lookup_count, 0);
                    if(err != UTIL_OK) {
                        pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_ERROR, "Pass 3: Failed to fixup field entry %zu of UDT",
                              i + 1 + j);
                        args_free(&args_result);
                        cip_registry_destroy(server.registry);
                        udt_array_destroy(server.udts, server.udt_count);
                        socket_cleanup();
                        return EXIT_FAILURE;
                    }
                }
            }
        }

        /* Log UDT summary */
        for(size_t i = 0; i < server.udt_count; i++) {
            udt_entry_t *entry = &server.udts[i];
            if(entry->entry_type == UDT_ENTRY_TYPE_DEF) {
                pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_INFO,
                      "Created UDT '%s' (Instance ID: %u) with %u members, %zu bytes total", entry->name, entry->instance_id,
                      entry->dimensions[0], entry->data.udt.total_size);
            }
        }
    }

    /* Register UDT Definition Object (Class 0x6C) for ControlLogix, Micro800, and Omron */
    if(server.plc_type == PLC_TYPE_CONTROLLOGIX || server.plc_type == PLC_TYPE_MICRO800) {
        /* ControlLogix/Micro800 use unified symbol object */
        if(udt_object_register(server.registry, &server) != 0) {
            pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_ERROR, "Failed to register UDT Definition Object");
            args_free(&args_result);
            cip_registry_destroy(server.registry);
            udt_array_destroy(server.udts, server.udt_count);
            socket_cleanup();
            return EXIT_FAILURE;
        }
    } else if(server.plc_type == PLC_TYPE_OMRON) {
        /* Omron uses its own Variable Type Object (Class 0x6C) */
        variable_type_object_omron_register(server.registry, &server);
        pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_INFO, "Registered Omron Variable Type Object (Class 0x6C)");
    }

    /* Register Omron objects (if PLC type is Omron) */
    if(server.plc_type == PLC_TYPE_OMRON) {
        tag_name_server_omron_register(server.registry, &server);
        variable_object_omron_register(server.registry, &server);

        pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_INFO, "Omron object model registered");
    }

    /* Initialize tag list (will be populated below with both UDT-based and regular tags) */
    /* Initialize tag array storage */
    if(tag_array_init(&server.tags, &server.tag_capacity) != 0) {
        pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_ERROR, "Failed to initialize tag array");
        args_free(&args_result);
        cip_registry_destroy(server.registry);
        udt_array_destroy(server.udts, server.udt_count);
        socket_cleanup();
        return EXIT_FAILURE;
    }
    server.tag_count = 0;

    /* ===== PHASE 5A: TESTING/DEBUGGING FEATURE CONFIGURATION ===== */
    /* Parse testing/debugging flags */
    args_value_t delay_val = args_get_at(&args_result, "delay", 0);
    if(delay_val.present) {
        int delay_ms = atoi(delay_val.value.string_val);
        if(delay_ms > 0) {
            server.response_delay_ms = (uint32_t)delay_ms;
            pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_INFO, "Response delay set to %dms for timeout testing", delay_ms);
        } else {
            pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_ERROR, "Invalid --delay value '%s' (must be positive integer)",
                  delay_val.value.string_val);
            args_free(&args_result);
            cip_registry_destroy(server.registry);
            udt_array_destroy(server.udts, server.udt_count);
            socket_cleanup();
            return EXIT_FAILURE;
        }
    }

    args_value_t reject_fo_val = args_get_at(&args_result, "reject-fo", 0);
    if(reject_fo_val.present) {
        int reject_count = atoi(reject_fo_val.value.string_val);
        if(reject_count >= 0) {
            server.reject_fo_count = (uint32_t)reject_count;
            pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_INFO, "Will reject first %d ForwardOpen requests", reject_count);
        } else {
            pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_ERROR, "Invalid --reject-fo value '%s' (must be non-negative integer)",
                  reject_fo_val.value.string_val);
            args_free(&args_result);
            cip_registry_destroy(server.registry);
            udt_array_destroy(server.udts, server.udt_count);
            socket_cleanup();
            return EXIT_FAILURE;
        }
    }

    /* ===== PHASE 6: TAG PARSING (UNIFIED FOR ALL PLC TYPES) ===== */
    /* Parse --tag arguments (format: name:type[dim1,dim2,dim3] for CIP or N7[100] for PCCC) */
    size_t tag_count_from_args = args_get_count(&args_result, "tag");

    for(size_t i = 0; i < tag_count_from_args; i++) {
        args_value_t val = args_get_at(&args_result, "tag", i);
        if(!val.present) {
            pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_ERROR, "Failed to get tag argument at index %zu", i);
            continue;
        }

        const char *tag_str = val.value.string_val;

        /* Check if this is a PCCC tag (for PLC/5, SLC, MicroLogix) */
        bool is_pccc = is_pccc_tag(tag_str);
        bool plc_supports_pccc = (server.plc_type == PLC_TYPE_PLC5 || server.plc_type == PLC_TYPE_SLC);

        if(is_pccc && plc_supports_pccc) {
            /* Parse PCCC format: "N7[100]", "F8[50]", "ST18[10]", etc. */
            char tag_name[256];
            uint16_t tag_type_code;
            size_t elem_size;
            size_t elem_count;

            if(!parse_pccc_tag(tag_str, tag_name, &tag_type_code, &elem_size, &elem_count)) {
                pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_ERROR, "Failed to parse PCCC tag: %s", tag_str);
                continue;
            }

            /* Check for duplicate PCCC files */
            tag_def_t *existing = tag_find_by_name(server.tags, server.tag_count, tag_name, strlen(tag_name));
            if(existing) {
                pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_ERROR, "Duplicate PCCC file '%s'", tag_name);
                continue;
            }

            /* Create PCCC tag */
            tag_def_t *new_tag = tag_create(tag_name, tag_type_code, elem_size, elem_count);
            if(!new_tag) {
                pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_ERROR, "Failed to create PCCC tag: %s", tag_name);
                continue;
            }

            /* Set dimensions (always 1D for PCCC) */
            new_tag->dim_count = 1;
            new_tag->dimensions[0] = elem_count;

            /* Add to tag array */
            if(tag_array_add(&server.tags, &server.tag_count, &server.tag_capacity, new_tag) != 0) {
                pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_ERROR, "Failed to add PCCC tag to array: %s", tag_name);
                tag_destroy(new_tag);
                continue;
            }

            pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_INFO,
                  "Created PCCC tag '%s' (type=0x%02X, elem_size=%zu, elem_count=%zu, instance_id=%u)", tag_name, tag_type_code,
                  elem_size, elem_count, new_tag->instance_id);

            continue; /* Skip CIP parsing for this tag */
        } else if(is_pccc && !plc_supports_pccc) {
            /* PCCC tag format used but PLC type doesn't support PCCC */
            pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_ERROR,
                  "PCCC tag format '%s' not supported for PLC type %d (only PLC/5 and SLC support PCCC)", tag_str,
                  server.plc_type);
            continue;
        }

        /* Parse CIP format: "name:type[dims]" */
        char *colon_pos = strchr(tag_str, ':');
        if(!colon_pos) {
            pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_ERROR, "Invalid tag format (missing ':'): %s", tag_str);
            continue;
        }

        /* Extract tag name */
        char tag_name[256];
        size_t name_len = (size_t)((ptrdiff_t)(colon_pos - tag_str));
        if(name_len >= sizeof(tag_name)) {
            pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_ERROR, "Tag name too long: %s", tag_str);
            continue;
        }
        strncpy(tag_name, tag_str, name_len);
        tag_name[name_len] = '\0';

        /* Parse type[dims] */
        const char *type_and_dims = colon_pos + 1;
        char *bracket_pos = strchr(type_and_dims, '[');

        char type_name[64];
        if(bracket_pos) {
            size_t type_len = (size_t)((ptrdiff_t)(bracket_pos - type_and_dims));
            if(type_len >= sizeof(type_name)) {
                pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_ERROR, "Type name too long: %s", tag_str);
                continue;
            }
            strncpy(type_name, type_and_dims, type_len);
            type_name[type_len] = '\0';
        } else {
            /* No dimensions specified, just type */
            if(strlen(type_and_dims) >= sizeof(type_name)) {
                pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_ERROR, "Type name too long: %s", tag_str);
                continue;
            }
            strcpy(type_name, type_and_dims);
        }

        /* Look up type: first check built-in types */
        uint16_t tag_type_code = 0;
        size_t elem_size = 0;
        uint16_t udt_id = 0;

        /* Check built-in types */
        for(size_t j = 0; TYPE_MAP[j].name != NULL; j++) {
            if(strcmp(type_name, TYPE_MAP[j].name) == 0) {
                tag_type_code = TYPE_MAP[j].symbol_type;
                elem_size = TYPE_MAP[j].element_size;
                break;
            }
        }

        /* If not found, check if it's a UDT */
        if(tag_type_code == 0) {
            udt_entry_t *udt = udt_find_by_name(server.udts, server.udt_count, type_name);
            if(udt && udt->entry_type == UDT_ENTRY_TYPE_DEF) {
                udt_id = udt->instance_id;
                tag_type_code = udt_id; /* For UDT-based tags, type code is the UDT ID */
                elem_size = udt->data.udt.total_size;
            } else {
                pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_ERROR, "Unknown type: %s", type_name);
                continue;
            }
        }

        /* Parse dimensions */
        size_t dimensions[8];
        size_t dim_count = 1;
        size_t elem_count = 1;

        if(bracket_pos) {
            char *close_bracket = strchr(bracket_pos, ']');
            if(!close_bracket) {
                pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_ERROR, "Invalid dimension syntax (missing ']'): %s", tag_str);
                continue;
            }

            /* Parse dimension values (comma-separated) */
            const char *dim_start = bracket_pos + 1;
            char dim_str[256];
            size_t dim_len = (size_t)((ptrdiff_t)(close_bracket - dim_start));
            if(dim_len >= sizeof(dim_str)) {
                pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_ERROR, "Dimension string too long: %s", tag_str);
                continue;
            }
            strncpy(dim_str, dim_start, dim_len);
            dim_str[dim_len] = '\0';

            /* Split on commas */
            char *saveptr = NULL;
            char *token = strtok_r(dim_str, ",", &saveptr);
            dim_count = 0;
            while(token && dim_count < 8) {
                dimensions[dim_count] = (size_t)atoi(token);
                if(dimensions[dim_count] == 0) {
                    pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_ERROR, "Invalid dimension (must be > 0): %s", tag_str);
                    goto next_tag; /* Skip this tag */
                }
                elem_count *= dimensions[dim_count];
                dim_count++;
                token = strtok_r(NULL, ",", &saveptr);
            }
        } else {
            /* Scalar tag */
            dimensions[0] = 1;
            dim_count = 1;
            elem_count = 1;
        }

        /* Create tag */
        tag_def_t *new_tag = tag_create(tag_name, tag_type_code, elem_size, elem_count);
        if(!new_tag) {
            pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_ERROR, "Failed to create tag: %s", tag_name);
            goto next_tag;
        }

        /* Set UDT ID and dimensions */
        new_tag->udt_id = udt_id;
        new_tag->dim_count = dim_count;
        for(size_t j = 0; j < dim_count; j++) { new_tag->dimensions[j] = dimensions[j]; }

        /* Add to tag array */
        if(tag_array_add(&server.tags, &server.tag_count, &server.tag_capacity, new_tag) != 0) {
            pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_ERROR, "Failed to add tag to array: %s", tag_name);
            tag_destroy(new_tag);
            goto next_tag;
        }

        pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_INFO, "Created tag '%s' (type=%s, elem_size=%zu, elem_count=%zu, instance_id=%u)",
              tag_name, type_name, elem_size, elem_count, new_tag->instance_id);

    next_tag:
        (void)0; /* Label requires statement */
    }

    /* Create UDT-based tags FIRST (Motor type) if Motor UDT exists and no --tag args provided */
    if(server.udt_count > 0) {
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

                if(tag_array_add(&server.tags, &server.tag_count, &server.tag_capacity, new_tag) != 0) {
                    pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_ERROR, "Failed to add Motor UDT tag: %s", name);
                    tag_destroy(new_tag);
                } else {
                    pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_INFO, "Created Motor UDT tag '%s' (UDT ID: 0x%04X, instance_id=%u)",
                          name, new_tag->udt_id, new_tag->instance_id);
                }
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

    /* Create scalar DINT tags (tags 1-30) */
    for(size_t i = 1; i <= 30; i++) {
        char name[64];
        snprintf(name, sizeof(name), "Scalar_DINT_%03zu", i);
        tag_def_t *new_tag = tag_create_dint(name, (int32_t)(i * 100));
        if(new_tag) {
            if(tag_array_add(&server.tags, &server.tag_count, &server.tag_capacity, new_tag) != 0) {
                pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_ERROR, "Failed to add scalar DINT tag: %s", name);
                tag_destroy(new_tag);
            }
        }
    }

    /* Create scalar REAL tags (tags 31-60) */
    for(size_t i = 1; i <= 30; i++) {
        char name[64];
        snprintf(name, sizeof(name), "Scalar_REAL_%03zu", i);
        tag_def_t *new_tag = tag_create_real(name, (float)i * 3.14159f);
        if(new_tag) {
            if(tag_array_add(&server.tags, &server.tag_count, &server.tag_capacity, new_tag) != 0) {
                pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_ERROR, "Failed to add scalar REAL tag: %s", name);
                tag_destroy(new_tag);
            }
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
            if(tag_array_add(&server.tags, &server.tag_count, &server.tag_capacity, new_tag) != 0) {
                pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_ERROR, "Failed to add 1D array DINT tag: %s", name);
                tag_destroy(new_tag);
            }
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
            if(tag_array_add(&server.tags, &server.tag_count, &server.tag_capacity, new_tag) != 0) {
                pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_ERROR, "Failed to add 2D array DINT tag: %s", name);
                tag_destroy(new_tag);
            }
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
            if(tag_array_add(&server.tags, &server.tag_count, &server.tag_capacity, new_tag) != 0) {
                pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_ERROR, "Failed to add 1D array REAL tag: %s", name);
                tag_destroy(new_tag);
            }
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
        socket_t listen_fd = stream_listener_socket_create(&listen_address, 128);
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
