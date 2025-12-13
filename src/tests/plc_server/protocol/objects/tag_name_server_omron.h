#pragma once

#include "../cip_object_registry.h"

/* Forward declarations */
typedef struct plc_context_s plc_context_t;
typedef struct omron_registry_s omron_registry_t;

/**
 * @brief Register Omron Tag Name Server (Class 0x6A)
 *
 * Registers the Tag Name Server for Omron NX/NJ PLC simulator.
 * This object provides variable discovery and enumeration.
 *
 * Supports the following services:
 *   - Get Attribute All (0x01) - Returns variable count on Instance 0
 *   - Get Instance List (0x5F) - Enumerate variables with instance IDs
 *
 * @param registry CIP object registry to register class in
 * @param omron_registry Omron variable registry (for variable enumeration)
 */
void tag_name_server_omron_register(cip_object_registry_t *registry,
                                     omron_registry_t *omron_registry);
