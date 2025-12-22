#pragma once

/* Include cip_path.h first to avoid forward declaration conflicts */
#include "../cip_path.h"
#include "../cip_object_registry.h"

/* Forward declarations */
typedef struct plc_context_s plc_context_t;

/**
 * @brief Register Omron Variable Object (Class 0x6B)
 *
 * Registers the Variable Object for Omron NX/NJ PLC simulator.
 * This object provides access to individual variables (tags) with read/write capability.
 *
 * Uses unified generic tag storage (tag_def_t) that is shared across all PLC types.
 *
 * Supports the following services:
 *   - Get Attribute All (0x01) - Returns variable metadata (size, type, array info, type ID)
 *   - Read Tag (0x4C)          - Read variable data by instance ID (supports 0x80 data segments)
 *   - Write Tag (0x4D)         - Write variable data by instance ID (supports 0x80 data segments)
 *
 * Instances are dynamically allocated based on tags in plc->tags list (1, 2, 3...).
 *
 * @param registry CIP object registry to register class in
 * @param plc Server context (for tag list access via plc->tags)
 */
void variable_object_omron_register(cip_object_registry_t *registry, client_context_t *client);
