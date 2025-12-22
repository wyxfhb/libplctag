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
#include <unistd.h>

#include "../../utils/coro_net.h"
#include "../../utils/args.h"
#include "../../utils/log.h"
#include "../../utils/socket.h"
#include "../../utils/utils.h"
#include "../../utils/err.h"
#include "../plcs/ab/ab_plc_logix.h"

/* ============================================================================
 * Global State
 * ============================================================================ */

static coro_net_t *g_coro = NULL;

/* ============================================================================
 * Signal Handler
 * ============================================================================ */

static void signal_handler(void) {
    pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_INFO, "Shutdown signal received");
    if(g_coro) {
        pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_INFO, "Stopping event loop");
        coro_stop(g_coro);
    }
}


/* ===========================================================================
 * Argument Definitions
 * =========================================================================== */

/* ===== Setup Argument Definitions ===== */
static args_flag_def_t flags[] = {
    {.name = "plc-type",
     .type = ARGS_TYPE_STRING,
     .required = ARGS_REQUIRED,
     .repeat = ARGS_ONCE,
     .description = "PLC type (controllogix, micro800, omron, etc.)",
     .default_value = {.has_default = true, .value.string_val = "controllogix"}},
    {.name = "debug",
     .type = ARGS_TYPE_STRING,
     .required = ARGS_OPTIONAL,
     .repeat = ARGS_ONCE,
     .description = "Debug logging level (ERROR, WARN, INFO, DETAIL, SPEW)",
     .default_value = {.has_default = true, .value.string_val = "INFO"}},
    {.name = "port",
     .type = ARGS_TYPE_INT,
     .required = ARGS_OPTIONAL,
     .repeat = ARGS_ONCE,
     .description = "Listen port (PLC type determines default)",
     .default_value = {.has_default = false}},
    {.name = "tag",
     .type = ARGS_TYPE_STRING,
     .required = ARGS_REQUIRED,
     .repeat = ARGS_MULTIPLE,
     .description = "Define a tag (NAME:TYPE[dimensions], can be repeated)",
     .default_value = {.has_default = false}},
    {.name = "path",
     .type = ARGS_TYPE_STRING,
     .required = ARGS_OPTIONAL,
     .repeat = ARGS_ONCE,
     .description = "CIP path (comma-separated node IDs)",
     .default_value = {.has_default = false}},
    {.name = "help",
     .type = ARGS_TYPE_BOOL,
     .required = ARGS_OPTIONAL,
     .repeat = ARGS_ONCE,
     .description = "Show this help message",
     .default_value = {.has_default = true, .value.bool_val = false}},
};
static size_t num_flags = sizeof(flags) / sizeof(flags[0]);


/* ============================================================================
 * Main Entry Point
 * ============================================================================ */

int main(int argc, char **argv) {
    printf("PLC Server\n");

    /* ===== Parse Arguments ===== */
    args_result_t args = {0};
    util_err_t rc = args_parse(argc, argv, flags, num_flags, &args);
    if(rc != UTIL_OK) {
        pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_ERROR, "Failed to parse arguments: %s", args_get_error_detail(&args));
        args_print_help(argv[0], flags, num_flags);
        args_free(&args);
        return 1;
    }

    /* ===== Handle Help ===== */
    if(args_get_bool(&args, "help")) {
        args_print_help(argv[0], flags, num_flags);
        args_free(&args);
        return EXIT_SUCCESS;
    }

    /* ===== Setup Logging ===== */
    char *debug_str = args_get_string(&args, "debug");
    log_level_t log_level = LOG_LEVEL_NONE;
    if(debug_str) {
        if(strcasecmp(debug_str, "ERROR") == 0) {
            log_level = LOG_LEVEL_ERROR;
        } else if(strcasecmp(debug_str, "WARN") == 0) {
            log_level = LOG_LEVEL_WARN;
        } else if(strcasecmp(debug_str, "INFO") == 0) {
            log_level = LOG_LEVEL_INFO;
        } else if(strcasecmp(debug_str, "DETAIL") == 0) {
            log_level = LOG_LEVEL_DETAIL;
        } else if(strcasecmp(debug_str, "SPEW") == 0) {
            log_level = LOG_LEVEL_SPEW;
        } else if(strcasecmp(debug_str, "NONE") == 0) {
            log_level = LOG_LEVEL_NONE;
        } else {
            pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_WARN, "Unknown debug level '%s', using INFO", debug_str);
        }
    }

    log_set_all_modules(log_level);

    pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_INFO, "PLC Server 2 starting");

    /* ===== Initialize Socket Library ===== */
    rc = socket_init();
    if(rc != UTIL_OK) {
        pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_ERROR, "Failed to initialize socket library: %s", util_err_str(rc));
        args_free(&args);
        return 1;
    }

    /* ===== Setup Event Loop ===== */
    rc = coro_create(&g_coro, 256);
    if(rc != UTIL_OK) {
        pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_ERROR, "Failed to create event loop: %s", util_err_str(rc));
        socket_cleanup();
        args_free(&args);
        return 1;
    }

    /* ===== Setup Signal Handler ===== */
    util_set_interrupt_handler(signal_handler);

    /* ===== Dispatch by PLC Type ===== */
    const char *plc_type = args_get_string(&args, "plc-type");
    if(!plc_type || plc_type[0] == '\0') {
        pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_ERROR, "Invalid PLC type");
        coro_destroy(&g_coro);
        socket_cleanup();
        args_free(&args);
        return 1;
    }

    pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_INFO, "Dispatching to PLC type: %s", plc_type);

    if(strcasecmp(plc_type, "controllogix") != 0) {
        pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_ERROR, "Unknown PLC type: %s", plc_type);
        coro_destroy(&g_coro);
        socket_cleanup();
        args_free(&args);
        return 1;
    }

    rc = ab_plc_logix_main(&args, g_coro);
    if(rc != UTIL_OK) {
        pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_ERROR, "PLC setup failed: %s", util_err_str(rc));
        coro_destroy(&g_coro);
        socket_cleanup();
        args_free(&args);
        return rc;
    }

    pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_INFO, "Server initialized successfully");

    /* ===== Run Event Loop ===== */
    pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_INFO, "Running event loop");
    coro_run(&g_coro, 50); /* 50ms tick interval */

    /* ===== Cleanup ===== */
    pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_INFO, "Shutting down");
    coro_destroy(&g_coro);
    socket_cleanup();
    args_free(&args);

    pdlog(LOG_MODULE_PLC_SERVER, LOG_LEVEL_INFO, "Server stopped");
    return EXIT_SUCCESS;
}
