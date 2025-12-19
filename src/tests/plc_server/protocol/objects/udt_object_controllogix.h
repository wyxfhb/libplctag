#pragma once

#include <stdint.h>
#include "../cip_object_registry.h"

/* Forward declarations */
typedef struct udt_def_s udt_def_t;
typedef struct plc_context_s plc_context_t;

/**
 * Register UDT Definition Object (Class 0x6C) with CIP registry
 *
 * This object implements Service 0x55 (List UDTs) which returns
 * enumeration of all User-Defined Types in the PLC.
 *
 * @param registry CIP object registry to register with
 * @param plc      PLC context containing UDT definitions
 * @return         0 on success, non-zero on error
 */
int udt_object_controllogix_register(cip_object_registry_t *registry, plc_context_t *plc);
