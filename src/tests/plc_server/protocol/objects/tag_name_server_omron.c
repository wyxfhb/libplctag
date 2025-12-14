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
#include "tag_name_server_omron.h"
#include "../cip_message_router.h"
#include "../../omron_storage.h"
#include "../../plc_context.h"
#include "../../../utils/log.h"
#include "../../../utils/buf.h"

/* ============================================================================
 * Tag Name Server Context Storage
 * ============================================================================ */

typedef struct {
    omron_registry_t *registry;
} tag_name_server_omron_context_t;

static tag_name_server_omron_context_t *tns_context = NULL;

/* ============================================================================
 * Service: Get Attribute All (0x01)
 * ============================================================================ */

/**
 * Service 0x01: Get Attributes All on Instance 0
 *
 * Returns the tag name server attributes (primarily variable count).
 *
 * Request:
 *   (empty)
 *
 * Response:
 *   [0-1]  uint16_le  Reserved
 *   [2-3]  uint16_le  Total variable count
 */
static util_err_t tag_name_server_service_get_attributes_all(uint8_t service, const cip_path_t *path, buf_t *request,
                                                             buf_t *response, cip_object_instance_t *instance,
                                                             plc_context_t *plc) {

    (void)path;
    (void)request;
    (void)plc;
    (void)instance;

    if(!tns_context || !tns_context->registry) {
        pdlog(LOG_MODULE_OMRON_TAG_NAME_SERVER, LOG_LEVEL_ERROR, "Get Attributes All: registry not initialized");
        cip_build_response(response, service, CIP_STATUS_SERVICE_ERROR);
        return UTIL_EINTERNAL;
    }

    /* Count variables */
    size_t var_count = 0;
    omron_variable_t *var = tns_context->registry->variables;
    while(var) {
        var_count++;
        var = var->next;
    }

    pdlog(LOG_MODULE_OMRON_TAG_NAME_SERVER, LOG_LEVEL_DETAIL, "Get Attributes All: returning count=%zu", var_count);

    /* Build response */
    cip_build_response(response, service, CIP_STATUS_OK);

    bool ok = true;
    ok &= buf_write_u16_le(response, "reserved", 0);
    ok &= buf_write_u16_le(response, "count", (uint16_t)var_count);

    if(!ok) {
        pdlog(LOG_MODULE_OMRON_TAG_NAME_SERVER, LOG_LEVEL_ERROR, "Get Attributes All: failed to write response");
        return buf_get_error(response);
    }

    return UTIL_OK;
}

/* ============================================================================
 * Service: Get Instance List (0x5F)
 * ============================================================================ */

/**
 * Service 0x5F: Get Instance List
 *
 * Enumerates variable instances with names and metadata.
 *
 * Request:
 *   [0-3]   uint32_le  Start instance ID (0 for first)
 *   [4-7]   uint32_le  Max count to return
 *   [8-9]   uint16_le  Kind (2 = user variables, 1 = system/reserved)
 *
 * Response:
 *   [0-1]   uint16_le  Number of instances returned
 *   [2]     uint8      More flag (1=more exist, 0=end)
 *   [3]     uint8      Reserved
 *   [4+]    Instance records (one per variable):
 *     [0-1]   uint16_le  Record length in words
 *     [2-3]   uint16_le  Class ID (0x6B = Variable Object)
 *     [4-7]   uint32_le  Instance ID
 *     [8]     uint8      Name length
 *     [9+]    uint8[]    Name (padded to word boundary)
 */
static util_err_t tag_name_server_service_get_instance_list(uint8_t service, const cip_path_t *path, buf_t *request,
                                                            buf_t *response, cip_object_instance_t *instance,
                                                            plc_context_t *plc) {

    (void)path;
    (void)plc;
    (void)instance;

    if(!tns_context || !tns_context->registry) {
        pdlog(LOG_MODULE_OMRON_TAG_NAME_SERVER, LOG_LEVEL_ERROR, "Get Instance List: registry not initialized");
        cip_build_response(response, service, CIP_STATUS_SERVICE_ERROR);
        return UTIL_EINTERNAL;
    }

    /* Parse request */
    uint32_t start_instance_id = 0;
    uint32_t max_count = 0;
    uint16_t kind = 0;

    if(!buf_read_u32_le(request, "start_instance_id", &start_instance_id)) {
        pdlog(LOG_MODULE_OMRON_TAG_NAME_SERVER, LOG_LEVEL_WARN, "Get Instance List: failed to read start_instance_id");
        cip_build_response(response, service, CIP_STATUS_INVALID_PARAM);
        return UTIL_EINVAL;
    }

    if(!buf_read_u32_le(request, "max_count", &max_count)) {
        pdlog(LOG_MODULE_OMRON_TAG_NAME_SERVER, LOG_LEVEL_WARN, "Get Instance List: failed to read max_count");
        cip_build_response(response, service, CIP_STATUS_INVALID_PARAM);
        return UTIL_EINVAL;
    }

    if(!buf_read_u16_le(request, "kind", &kind)) {
        pdlog(LOG_MODULE_OMRON_TAG_NAME_SERVER, LOG_LEVEL_WARN, "Get Instance List: failed to read kind");
        cip_build_response(response, service, CIP_STATUS_INVALID_PARAM);
        return UTIL_EINVAL;
    }

    /* Only support kind=2 (user variables) */
    if(kind != 2) {
        pdlog(LOG_MODULE_OMRON_TAG_NAME_SERVER, LOG_LEVEL_DETAIL, "Get Instance List: kind=%u (only 2=user vars supported)",
              kind);
        cip_build_response(response, service, CIP_STATUS_OK);
        bool ok = buf_write_u16_le(response, "returned_count", 0);
        if(!ok) { return buf_get_error(response); }
        return UTIL_OK;
    }

    pdlog(LOG_MODULE_OMRON_TAG_NAME_SERVER, LOG_LEVEL_DETAIL, "Get Instance List: start=%u max_count=%u kind=%u",
          start_instance_id, max_count, kind);

    /* Build response header */
    cip_build_response(response, service, CIP_STATUS_OK);

    /* First pass: count matching records and determine if more exist */
    uint32_t returned_count = 0;
    uint8_t more_flag = 0;
    omron_variable_t *var = tns_context->registry->variables;

    /* Skip to start_instance_id */
    while(var && var->instance_id < start_instance_id) { var = var->next; }

    /* Count how many we'll return and if more exist */
    omron_variable_t *count_var = var;
    while(count_var && returned_count < max_count) {
        returned_count++;
        count_var = count_var->next;
    }
    if(count_var) { more_flag = 1; }

    /* Write header */
    bool ok = true;
    ok &= buf_write_u16_le(response, "returned_count", (uint16_t)returned_count);
    ok &= buf_write_u8(response, "more_flag", more_flag);
    ok &= buf_write_u8(response, "reserved", 0);

    if(!ok) {
        pdlog(LOG_MODULE_OMRON_TAG_NAME_SERVER, LOG_LEVEL_ERROR, "Get Instance List: failed to write response header");
        return buf_get_error(response);
    }

    /* Second pass: build records */
    uint32_t record_count = 0;
    var = tns_context->registry->variables;

    /* Skip to start_instance_id again */
    while(var && var->instance_id < start_instance_id) { var = var->next; }

    /* Build records up to max_count */
    while(var && record_count < max_count) {
        size_t name_len = strlen(var->name);
        size_t padded_name_len = ((name_len + 1) / 2) * 2; /* Pad to word boundary */

        /* Calculate record length in words:
         * length(2) + class_id(2) + instance_id(4) + name_len(1) + name(padded) = 9 + padded_name_len
         * In words: (9 + padded_name_len) / 2 = 4-5 words typically */
        size_t record_len_bytes = 2 + 2 + 4 + 1 + padded_name_len;
        uint16_t record_len_words = (uint16_t)(record_len_bytes / 2);

        /* Write record */
        ok &= buf_write_u16_le(response, "record_length", record_len_words);
        ok &= buf_write_u16_le(response, "class_id", 0x6B); /* Variable Object */
        ok &= buf_write_u32_le(response, "instance_id", var->instance_id);
        ok &= buf_write_u8(response, "name_length", (uint8_t)name_len);

        /* Write name with padding */
        uint8_t padded_name[256];
        memset(padded_name, 0, sizeof(padded_name));
        memcpy(padded_name, var->name, name_len);
        ok &= buf_write_bytes(response, "name", padded_name, padded_name_len);

        if(!ok) {
            pdlog(LOG_MODULE_OMRON_TAG_NAME_SERVER, LOG_LEVEL_ERROR, "Get Instance List: failed to write record for '%s'",
                  var->name);
            return buf_get_error(response);
        }

        pdlog(LOG_MODULE_OMRON_TAG_NAME_SERVER, LOG_LEVEL_DETAIL, "Get Instance List: record %u - id=%u name='%s'", record_count,
              var->instance_id, var->name);

        record_count++;
        var = var->next;
    }

    pdlog(LOG_MODULE_OMRON_TAG_NAME_SERVER, LOG_LEVEL_DETAIL, "Get Instance List: returned %u records, more_flag=%u",
          returned_count, more_flag);

    return UTIL_OK;
}

/* ============================================================================
 * Instance Management
 * ============================================================================ */

static cip_object_instance_t *tag_name_server_get_instance(uint32_t instance_id, plc_context_t *plc) {
    (void)plc;

    /* Tag Name Server is a singleton on Instance 0 */
    if(instance_id != 0) { return NULL; }

    static cip_object_instance_t instance = {
        .object_class = NULL, /* Will be set by registry */
        .instance_id = 0,
        .instance_data = NULL,
    };
    return &instance;
}

/* ============================================================================
 * Registration
 * ============================================================================ */

void tag_name_server_omron_register(cip_object_registry_t *registry, omron_registry_t *omron_registry) {
    if(!registry || !omron_registry) { return; }

    /* Store registry for service handlers */
    if(!tns_context) {
        tns_context = (tag_name_server_omron_context_t *)calloc(1, sizeof(*tns_context));
        if(!tns_context) { return; }
    }
    tns_context->registry = omron_registry;

    /* Create and register the object class */
    cip_object_class_t *cls = (cip_object_class_t *)calloc(1, sizeof(*cls));
    if(!cls) { return; }

    cls->class_id = 0x6A;
    cls->class_name = "Tag Name Server (Omron)";

    /* Register service handlers */
    cls->service_handlers[0x01] = tag_name_server_service_get_attributes_all;
    cls->service_handlers[0x5F] = tag_name_server_service_get_instance_list;

    /* Instance management */
    cls->get_instance = tag_name_server_get_instance;

    /* Register in registry */
    cip_registry_register_class(registry, cls);

    pdlog(LOG_MODULE_OMRON_TAG_NAME_SERVER, LOG_LEVEL_INFO, "Registered Tag Name Server (Class 0x6A)");
}
