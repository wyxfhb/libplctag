#pragma once

#include "../cip_object_registry.h"

/* Forward declarations */
typedef struct plc_context_s plc_context_t;

/**
 * @brief Register Omron Variable Type Object (Class 0x6C)
 *
 * Registers the Variable Type Object for Omron NX/NJ PLC simulator.
 * This object provides type definitions and structure member information.
 *
 * Uses unified generic UDT storage (udt_def_t) that is shared across all PLC types.
 *
 * Supports the following services:
 *   - Get Attribute All (0x01) - Returns type/member attributes with member linking
 *
 * Instance management:
 *   - Type definition instances (uses udt_id as instance_id)
 *   - Member definition instances (encoded as (udt_id << 16) | member_index)
 *   - Member linking via calculated next_member_id for CIP compliance
 *
 * @param registry CIP object registry to register class in
 * @param plc Server context (for UDT list access via plc->udts)
 */
void variable_type_object_omron_register(cip_object_registry_t *registry, client_context_t *client);
