#pragma once

#include "../cip_object_registry.h"

/* Forward declarations */
typedef struct plc_context_s plc_context_t;

/**
 * @brief Register Omron Tag Name Server (Class 0x6A)
 *
 * Registers the Tag Name Server for Omron NX/NJ PLC simulator.
 * This object provides tag discovery and enumeration.
 *
 * Uses unified generic tag storage (tag_def_t) that is shared across all PLC types.
 *
 * Supports the following services:
 *   - Get Attribute All (0x01) - Returns tag count on Instance 0
 *   - Get Instance List (0x5F) - Enumerate tags with instance IDs
 *
 * @param registry CIP object registry to register class in
 * @param plc Server context (for tag list access via plc->tags)
 */
void tag_name_server_omron_register(cip_object_registry_t *registry, client_context_t *client);
