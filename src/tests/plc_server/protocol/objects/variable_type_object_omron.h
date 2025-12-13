#pragma once

#include "../cip_object_registry.h"

/* Forward declarations */
typedef struct plc_context_s plc_context_t;
typedef struct omron_registry_s omron_registry_t;

/**
 * @brief Register Omron Variable Type Object (Class 0x6C)
 *
 * Registers the Variable Type Object for Omron NX/NJ PLC simulator.
 * This object provides type definitions and structure member information.
 *
 * Supports the following services:
 *   - Get Attribute All (0x01) - Returns type/member attributes with member linking
 *
 * Instance management:
 *   - Type definition instances (parent structures)
 *   - Member definition instances (nested under types, forming linked chains via next_member_id)
 *   - Instances continue numbering after Variable Object instances
 *
 * @param registry CIP object registry to register class in
 * @param omron_registry Omron variable registry (for type and member lookup)
 */
void variable_type_object_omron_register(cip_object_registry_t *registry,
                                          omron_registry_t *omron_registry);
