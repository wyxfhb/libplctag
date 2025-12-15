#pragma once

#include "../cip_object_registry.h"

/* Forward declarations */
typedef struct plc_context_s plc_context_t;

/**
 * @brief Register Connection Manager Object (Class 0x06)
 *
 * Registers a Connection Manager Object for managing connections.
 * Supports the following services:
 *   - Forward Open (0x54)
 *   - Forward Open Extended (0x5B)
 *   - Forward Close (0x4E)
 *
 * @param registry Registry to register class in
 */
void connection_manager_object_register(cip_object_registry_t *registry);
