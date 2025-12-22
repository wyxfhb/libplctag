#pragma once

#include "../cip_object_registry.h"

/* Forward declarations */
typedef struct plc_context_s plc_context_t;

/**
 * @brief Register PCCC Object (Class 0x67)
 *
 * Registers the PCCC (Programmable Controller Communication Commands) object
 * for handling legacy PLC/5, SLC 500, and MicroLogix PCCC protocol requests.
 *
 * Supports the following service:
 *   - PCCC Execute (0x4B) - Handles PCCC command dispatch
 *
 * Supported PCCC Commands:
 *   PLC/5:
 *     - 0x01: Read
 *     - 0x00: Write
 *     - 0x26: Read-Modify-Write (RMW)
 *
 *   SLC/MicroLogix:
 *     - 0xA2: Read
 *     - 0xAA: Write
 *     - 0xAB: Read-Modify-Write (RMW)
 *
 * @param registry Registry to register class in
 */
void pccc_object_register(cip_object_registry_t *registry);
