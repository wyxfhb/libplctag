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

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "err.h"

/**
 * @brief Command-line argument parsing module.
 *
 * Provides declarative, table-driven parsing of command-line arguments.
 * Supports typed flags (strings, integers, floats, booleans), required/optional
 * flags, and repeated flags with iteration support.
 *
 * Features:
 * - Name-based accessor functions (no index tracking)
 * - Debug names for structured logging
 * - Type validation with error reporting
 * - Zero-copy string values
 * - Stack-allocatable result structure
 *
 * Example:
 *   args_flag_def_t flags[] = {
 *       { "host", ARGS_TYPE_STRING, ARGS_REQUIRED, ARGS_ONCE,
 *         "server.host", "Server hostname" },
 *       { "port", ARGS_TYPE_INT, ARGS_REQUIRED, ARGS_ONCE,
 *         "server.port", "Server port" },
 *       { "verbose", ARGS_TYPE_BOOL, ARGS_OPTIONAL, ARGS_ONCE,
 *         "logging.verbose", "Verbose output" },
 *   };
 *
 *   args_result_t result;
 *   util_err_t err = args_parse(argc, argv, flags, 3, &result);
 *   if (err != UTIL_OK) {
 *       log_error("Parse failed: %s", args_get_error_debug_name(&result));
 *       args_free(&result);
 *       return 1;
 *   }
 *
 *   char *host = args_get_string(&result, "host");
 *   int64_t port = args_get_int(&result, "port");
 *   args_free(&result);
 */

/* ================================================================
 * Constants
 * ================================================================ */

#define ARGS_MAX_FLAGS 64
#define ARGS_MAX_REPETITIONS 256

/* ================================================================
 * Type Definitions
 * ================================================================ */

/**
 * Type of value a flag accepts
 */
typedef enum {
    ARGS_TYPE_STRING,   // --flag=value (string)
    ARGS_TYPE_INT,      // --flag=123 (integer, base 10)
    ARGS_TYPE_INT_HEX,  // --flag=0xFF (integer, base 16)
    ARGS_TYPE_BOOL,     // --flag=true|false|yes|no|on|off|1|0
    ARGS_TYPE_FLOAT,    // --flag=3.14 (floating point)
} args_type_t;

/**
 * Whether a flag is required or optional
 */
typedef enum {
    ARGS_REQUIRED = 0,  // Flag must be present
    ARGS_OPTIONAL = 1,  // Flag is optional
} args_required_t;

/**
 * Whether a flag can appear multiple times
 */
typedef enum {
    ARGS_ONCE = 0,      // Flag appears at most once
    ARGS_MULTIPLE = 1,  // Flag can appear multiple times
} args_repetition_t;

/**
 * Single parsed value with type and presence indicator
 */
typedef struct {
    args_type_t type;
    union {
        char *string_val;
        int64_t int_val;
        double float_val;
        bool bool_val;
    } value;
    bool present;
} args_value_t;

/**
 * Array of values for a repeated flag
 */
typedef struct {
    args_value_t *values;
    size_t count;
} args_repeated_t;

/**
 * Default value for a flag (union of all types)
 * Used when flag is not provided on command line
 */
typedef struct {
    bool has_default;  // Whether a default is defined
    union {
        char *string_val;
        int64_t int_val;
        double float_val;
        bool bool_val;
    } value;
} args_default_t;

/**
 * Flag definition for declarative configuration
 *
 * name: CLI flag name (e.g., "host" for --host=value)
 * type: Type of value expected
 * required: Whether flag must be present (ignored if has default)
 * repeat: Whether flag can appear multiple times
 * debug_name: Qualified name for logging (e.g., "server.host")
 * description: Help text
 * default_value: Default value if flag not provided (optional)
 *
 * Example with defaults:
 *   { "port", ARGS_TYPE_INT, ARGS_REQUIRED, ARGS_ONCE,
 *     "server.port", "Server port",
 *     { .has_default = true, .value.int_val = 8080 } }
 *
 *   { "verbose", ARGS_TYPE_BOOL, ARGS_OPTIONAL, ARGS_ONCE,
 *     "logging.verbose", "Verbose output",
 *     { .has_default = true, .value.bool_val = false } }
 *
 *   { "name", ARGS_TYPE_STRING, ARGS_REQUIRED, ARGS_ONCE,
 *     "app.name", "Application name",
 *     { .has_default = false } }  // No default
 */
typedef struct {
    char *name;
    args_type_t type;
    args_required_t required;
    args_repetition_t repeat;
    char *description;
    args_default_t default_value;
    bool has_default;
} args_flag_def_t;

/**
 * Complete result of argument parsing
 * Internal structure - access via accessor functions
 */
typedef struct {
    args_value_t *values;
    args_repeated_t *repeat_groups;
    args_flag_def_t *flags;
    size_t flags_count;
    util_err_t error;
    char *error_detail;
    // char *error_debug_name;
    int error_flag_index;
} args_result_t;

/* ================================================================
 * Parsing
 * ================================================================ */

/**
 * Parse command-line arguments according to flag definitions.
 *
 * @param argc Number of arguments
 * @param argv Array of argument strings
 * @param flags Array of flag definitions
 * @param flags_count Number of flag definitions
 * @param result OUT: Parsed result
 * @return UTIL_OK on success, error code on failure
 */
util_err_t args_parse(int argc, char *argv[], args_flag_def_t *flags, size_t flags_count, args_result_t *result);

/* ================================================================
 * Value Access (by Flag Name)
 * ================================================================ */

/**
 * Get a flag value by name.
 *
 * @return args_value_t with presence indicator
 */
args_value_t args_get_value(args_result_t *result, char *flag_name);

/**
 * Get values for a repeated flag by name.
 */
args_repeated_t args_get_repeated(args_result_t *result, char *flag_name);

/**
 * Get string value by flag name.
 * @return NULL if not present or wrong type
 */
char *args_get_string(args_result_t *result, char *flag_name);

/**
 * Get integer value by flag name.
 * @return Value if present, INT64_MIN on error, 0 if not present
 */
int64_t args_get_int(args_result_t *result, char *flag_name);

/**
 * Get boolean value by flag name.
 * @return true if flag present, false otherwise
 */
bool args_get_bool(args_result_t *result, char *flag_name);

/**
 * Get float value by flag name.
 * @return Value if present, NaN if not present or error
 */
double args_get_float(args_result_t *result, char *flag_name);

/* ================================================================
 * Iteration (for ARGS_MULTIPLE flags)
 * ================================================================ */

/**
 * Get count of how many times a repeated flag appeared.
 * @return 0 if not present, > 0 for repeated
 */
size_t args_get_count(args_result_t *result, char *flag_name);

/**
 * Get value at index in a repeated flag's values.
 * @return args_value_t with presence indicator
 */
args_value_t args_get_at(args_result_t *result, char *flag_name, size_t index);

/* ================================================================
 * Error Handling & Introspection
 * ================================================================ */

/**
 * Check if parsing succeeded
 */
bool args_is_ok(args_result_t *result);

/**
 * Get error status
 */
util_err_t args_get_error(args_result_t *result);

/**
 * Get human-readable error message
 */
char *args_get_error_detail(args_result_t *result);

/**
 * Get debug name of flag that caused error
 */
char *args_get_error_debug_name(args_result_t *result);

/**
 * Get index of flag that caused error (-1 for general errors)
 */
int args_get_error_flag_index(args_result_t *result);

/**
 * Free resources associated with parsed result
 */
void args_free(args_result_t *result);

/* ================================================================
 * Introspection
 * ================================================================ */

/**
 * Look up flag definition by flag name
 * @return Pointer to flag definition, or NULL if not found
 */
args_flag_def_t *args_get_flag_def(args_flag_def_t *flags, size_t flags_count, char *flag_name);

/**
 * Get debug name from a flag definition
 */
char *args_get_debug_name(args_flag_def_t *flag_def);

/**
 * Print usage/help information with program name and all flags
 */
void args_print_help(char *program_name, args_flag_def_t *flags, size_t flags_count);

/**
 * Print all available flags with their metadata for usage functions.
 * Displays each flag on a separate line with:
 * - Flag name
 * - Required/Optional status
 * - Single/Multiple values status
 * - Description
 *
 * This is useful for building custom usage() functions.
 *
 * @param flags Array of flag definitions
 * @param flags_count Number of flag definitions
 */
void args_print_flags(args_flag_def_t *flags, size_t flags_count);

#ifdef __cplusplus
}
#endif
