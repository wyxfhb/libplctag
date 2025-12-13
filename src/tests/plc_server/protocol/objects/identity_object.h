#pragma once

#include "../cip_object_registry.h"

/* Forward declarations */
typedef struct plc_context_s plc_context_t;

/**
 * @brief Register Identity Object (Class 0x01)
 *
 * Registers an Identity Object for device information.
 * Supports the following services:
 *   - Get Attributes All (0x01) - Returns device information
 *
 * @param registry Registry to register class in
 */
void identity_object_register(cip_object_registry_t *registry);
