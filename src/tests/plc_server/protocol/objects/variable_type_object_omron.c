/***************************************************************************
 *   Copyright (C) 2025 by Kyle Hayes                                      *
 *   Author Kyle Hayes  kyle.hayes@gmail.com                               *
 *                                                                         *
 * This software is available under either the Mozilla Public License      *
 * version 2.0 or the GNU LGPL version 2 (or later) license, whichever     *
 * you choose.                                                             *
 *                                                                         *
 * MPL 2.0:                                                                *
 *                                                                         *
 *   This Source Code Form is subject to the terms of the Mozilla Public   *
 *   License, v. 2.0. If a copy of the MPL was not distributed with this   *
 *   file, You can obtain one at http://mozilla.org/MPL/2.0/.              *
 *                                                                         *
 *                                                                         *
 * LGPL 2:                                                                 *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU Library General Public License as       *
 *   published by the Free Software Foundation; either version 2 of the    *
 *   License, or (at your option) any later version.                       *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this program; if not, write to the                 *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/

#include <stdlib.h>
#include <string.h>
#include "variable_type_object_omron.h"
#include "../cip_message_router.h"
#include "../../udt_storage.h"
#include "../../plc_context.h"
#include "../../../utils/log.h"
#include "../../../utils/buf.h"

/* ============================================================================
 * Variable Type Object Context Storage
 * ============================================================================ */

typedef struct {
    plc_context_t *plc;
} variable_type_object_omron_context_t;

static variable_type_object_omron_context_t *vto_context = NULL;

/**
 * Instance type wrapper for Variable Type Object
 *
 * Can represent either a type definition or a type member.
 */
typedef enum {
    VTO_TYPE_DEF,   /* Instance is a type definition */
    VTO_TYPE_MEMBER /* Instance is a type member */
} vto_instance_type_t;

typedef struct {
    vto_instance_type_t type;
    union {
        udt_def_t *udt;
        udt_member_t *member;
    } data;
} vto_instance_data_t;

/* ============================================================================
 * Service: Get Attribute All (0x01)
 * ============================================================================ */

/**
 * Service 0x01: Get Attributes All on Instance N
 *
 * Returns attributes for a type definition or type member.
 *
 * For Type Definition:
 *   [0-3]   uint32_le  Size in bytes
 *   [4]     uint8      Type code (0xA0=structure)
 *   [5]     uint8      Array type (0=scalar)
 *   [6]     uint8      Reserved
 *   [7]     uint8      Reserved
 *   [8-9]   uint16_le  Member count
 *   [10-11] uint16_le  Reserved
 *   [12-13] uint16_le  CRC code
 *   [14]    uint8      Name length
 *   [15+]   uint8[]    Type name (padded to word boundary)
 *   [+4]    uint32_le  First member instance ID
 *   [+4]    uint32_le  Nesting type ID (0 for non-nested)
 *
 * For Type Member:
 *   [0-3]   uint32_le  Size in bytes
 *   [4]     uint8      Type code (CIP type or nested type ID)
 *   [5]     uint8      Array type
 *   [6-7]   uint16_le  Reserved
 *   [8-11]  uint32_le  Offset within parent
 *   [12-15] uint32_le  Next member instance ID (0=end)
 *   [16-19] uint32_le  Nesting type ID (if member is structure)
 */
static util_err_t variable_type_service_get_attributes_all(uint8_t service, const cip_path_t *path, buf_t *request,
                                                           buf_t *response, cip_object_instance_t *instance, plc_context_t *plc) {

    (void)path;
    (void)request;
    (void)plc;

    if(!instance || !instance->instance_data) {
        pdlog(LOG_MODULE_OMRON_TYPE_OBJECT, LOG_LEVEL_WARN, "Get Attributes All: invalid instance");
        cip_build_response(response, service, CIP_STATUS_PATH_DEST_UNKNOWN);
        return UTIL_ENOTFOUND;
    }

    vto_instance_data_t *data = (vto_instance_data_t *)instance->instance_data;
    bool ok = true;

    /* Build response header */
    cip_build_response(response, service, CIP_STATUS_OK);

    if(data->type == VTO_TYPE_DEF) {
        /* Type definition response */
        udt_def_t *udt = data->data.udt;

        pdlog(LOG_MODULE_OMRON_TYPE_OBJECT, LOG_LEVEL_DETAIL, "Get Attributes All: type '%s' id=%u size=%zu members=%zu",
              udt->name, udt->udt_id, udt->total_size, udt->member_count);

        /* Size, type code, array info, reserved */
        ok &= buf_write_u32_le(response, "size", (uint32_t)udt->total_size);
        ok &= buf_write_u8(response, "type_code", 0xA0);  /* Structure type code */
        ok &= buf_write_u8(response, "array_type", 0);
        ok &= buf_write_u8(response, "reserved1", 0);
        ok &= buf_write_u8(response, "reserved2", 0);

        /* Member count and CRC */
        ok &= buf_write_u16_le(response, "member_count", (uint16_t)udt->member_count);
        ok &= buf_write_u16_le(response, "reserved3", 0);
        ok &= buf_write_u16_le(response, "crc_code", 0);  /* CRC not computed in unified storage */

        /* Type name with padding */
        size_t name_len = strlen(udt->name);
        ok &= buf_write_u8(response, "name_length", (uint8_t)name_len);
        size_t padded_name_len = ((name_len + 1) / 2) * 2;
        uint8_t padded_name[256];
        memset(padded_name, 0, sizeof(padded_name));
        memcpy(padded_name, udt->name, name_len);
        ok &= buf_write_bytes(response, "type_name", padded_name, padded_name_len);

        /* First member ID and nesting type ID */
        /* For unified storage, member instance IDs are encoded as (udt_id << 16) | member_index */
        uint32_t first_member_id = (udt->member_count > 0) ? ((udt->udt_id << 16) | 0) : 0;
        ok &= buf_write_u32_le(response, "first_member_id", first_member_id);
        ok &= buf_write_u32_le(response, "nesting_type_id", 0);

    } else {
        /* Type member response */
        udt_member_t *member = data->data.member;

        /* We need to find the parent UDT to get the next member ID */
        /* For now, we'll calculate it from the instance_id encoding */
        uint32_t parent_udt_id = instance->instance_id >> 16;
        uint16_t member_index = instance->instance_id & 0xFFFF;

        /* Find parent UDT to determine next member */
        uint32_t next_member_id = 0;
        if(vto_context && vto_context->plc) {
            udt_def_t *parent_udt = vto_context->plc->udts;
            while(parent_udt) {
                if(parent_udt->udt_id == parent_udt_id && member_index + 1 < parent_udt->member_count) {
                    next_member_id = ((parent_udt_id << 16) | (member_index + 1));
                    break;
                }
                parent_udt = parent_udt->next;
            }
        }

        pdlog(LOG_MODULE_OMRON_TYPE_OBJECT, LOG_LEVEL_DETAIL, "Get Attributes All: member '%s' id=0x%X size=%zu offset=%zu next=0x%X",
              member->name, instance->instance_id, member->element_length, member->byte_offset, next_member_id);

        /* Size, type code, array info, reserved */
        ok &= buf_write_u32_le(response, "size", (uint32_t)member->element_length);
        ok &= buf_write_u8(response, "type_code", (uint8_t)member->symbol_type);
        ok &= buf_write_u8(response, "array_type", 0);
        ok &= buf_write_u16_le(response, "reserved1", 0);

        /* Offset within parent structure */
        ok &= buf_write_u32_le(response, "offset", (uint32_t)member->byte_offset);

        /* Next member ID (for linked list traversal) */
        ok &= buf_write_u32_le(response, "next_member_id", next_member_id);

        /* Nesting type ID (if this member is a structure) */
        ok &= buf_write_u32_le(response, "nesting_type_id", 0);  /* TODO: Support nested types */
    }

    if(!ok) {
        pdlog(LOG_MODULE_OMRON_TYPE_OBJECT, LOG_LEVEL_ERROR, "Get Attributes All: failed to write response");
        return buf_get_error(response);
    }

    return UTIL_OK;
}

/* ============================================================================
 * Instance Management
 * ============================================================================ */

static cip_object_instance_t *variable_type_get_instance(uint32_t instance_id, plc_context_t *plc) {
    if(!vto_context || !vto_context->plc) { return NULL; }

    /* Check if this is a type definition instance (udt_id as instance_id) */
    udt_def_t *udt = vto_context->plc->udts;
    while(udt) {
        if(udt->udt_id == instance_id) {
            /* Allocate instance data */
            vto_instance_data_t *data = (vto_instance_data_t *)calloc(1, sizeof(*data));
            if(!data) { return NULL; }
            data->type = VTO_TYPE_DEF;
            data->data.udt = udt;

            /* Allocate instance structure */
            cip_object_instance_t *instance = (cip_object_instance_t *)calloc(1, sizeof(*instance));
            if(!instance) {
                free(data);
                return NULL;
            }

            instance->object_class = NULL; /* Will be set by registry */
            instance->instance_id = instance_id;
            instance->instance_data = (void *)data;

            return instance;
        }
        udt = udt->next;
    }

    /* Check if this is a type member instance (encoded as (udt_id << 16) | member_index) */
    uint32_t parent_udt_id = instance_id >> 16;
    uint16_t member_index = instance_id & 0xFFFF;

    if(parent_udt_id > 0 && member_index < 0xFFFF) {
        udt = vto_context->plc->udts;
        while(udt) {
            if(udt->udt_id == parent_udt_id && member_index < udt->member_count) {
                /* Allocate instance data */
                vto_instance_data_t *data = (vto_instance_data_t *)calloc(1, sizeof(*data));
                if(!data) { return NULL; }
                data->type = VTO_TYPE_MEMBER;
                data->data.member = &udt->members[member_index];

                /* Allocate instance structure */
                cip_object_instance_t *instance = (cip_object_instance_t *)calloc(1, sizeof(*instance));
                if(!instance) {
                    free(data);
                    return NULL;
                }

                instance->object_class = NULL; /* Will be set by registry */
                instance->instance_id = instance_id;
                instance->instance_data = (void *)data;

                return instance;
            }
            udt = udt->next;
        }
    }

    return NULL;
}

/* ============================================================================
 * Registration
 * ============================================================================ */

void variable_type_object_omron_register(cip_object_registry_t *registry, plc_context_t *plc) {
    if(!registry || !plc) { return; }

    /* Store PLC context for service handlers and instance lookup */
    if(!vto_context) {
        vto_context = (variable_type_object_omron_context_t *)calloc(1, sizeof(*vto_context));
        if(!vto_context) { return; }
    }
    vto_context->plc = plc;

    /* Create and register the object class */
    cip_object_class_t *cls = (cip_object_class_t *)calloc(1, sizeof(*cls));
    if(!cls) { return; }

    cls->class_id = 0x6C;
    cls->class_name = "Variable Type Object (Omron)";

    /* Register service handlers */
    cls->service_handlers[0x01] = variable_type_service_get_attributes_all;

    /* Instance management */
    cls->get_instance = variable_type_get_instance;

    /* Register in registry */
    cip_registry_register_class(registry, cls);

    pdlog(LOG_MODULE_OMRON_TYPE_OBJECT, LOG_LEVEL_INFO, "Registered Variable Type Object (Class 0x6C)");
}
