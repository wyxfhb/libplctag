#pragma once

#include <stdint.h>
#include "../cip_object_registry.h"

/* Forward declarations */
typedef struct udt_def_s udt_def_t;
typedef struct plc_context_s plc_context_t;

/**
 * Register UDT Definition Object (Class 0x6C) with CIP registry
 *
 * This object implements:
 *   - Service 0x03 (Get Attribute List) - Returns basic UDT metadata
 *   - Service 0x4C (Read Template) - Returns detailed field information
 *
 * Supported by both ControlLogix and Micro800 PLC types.
 *
 * @param registry CIP object registry to register with
 * @param client   Client context containing PLC and other information
 * @return         0 on success, non-zero on error
 */
util_err_t udt_object_register(cip_object_registry_t *registry, client_context_t *client);
