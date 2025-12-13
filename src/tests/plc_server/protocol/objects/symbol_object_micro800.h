#pragma once

#include "../cip_object_registry.h"

/* Forward declarations */
typedef struct plc_context_s plc_context_t;

/**
 * @brief Register Micro800 Symbol Object (Class 0x6B)
 *
 * Registers a Symbol Object for Micro800 PLC type.
 * Supports the following services:
 *   - Read Tag (0x4C)   - Read tag data by symbolic name
 *   - Write Tag (0x4D)  - Write tag data by symbolic name
 *   - List Tags (0x55)  - List all available tags
 *
 * @param registry Registry to register class in
 */
void symbol_object_micro800_register(cip_object_registry_t *registry);
