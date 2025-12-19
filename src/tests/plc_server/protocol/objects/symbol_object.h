#pragma once

#include "../cip_object_registry.h"

/* Forward declarations */
typedef struct plc_context_s plc_context_t;

/**
 * @brief Register Symbol Object (Class 0x6B)
 *
 * Unified symbol object supporting both ControlLogix and Micro800 PLC types.
 * Registers the following services:
 *   - Read Tag (0x4C)           - Read tag data by symbolic name
 *   - Write Tag (0x4D)          - Write tag data by symbolic name
 *   - List Tags (0x55)          - List all available tags (with pagination)
 *   - Read Tag Fragmented (0x52) - Read large tags in fragments
 *
 * @param registry Registry to register class in
 */
void symbol_object_register(cip_object_registry_t *registry);
