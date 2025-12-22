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
#include "../cip_defs.h"
#include "../cip_message_router.h"
#include "../../tag_storage.h"
#include "../../plc_context.h"
#include "../../../utils/log.h"
#include "../../../utils/buf.h"

/* ============================================================================
 * Tag Name Server Context Storage
 * ============================================================================ */

typedef struct {
    plc_context_t *plc;
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
                                                             client_context_t *client) {

    (void)path;
    (void)request;
    (void)client;
    (void)instance;

    if(!tns_context || !tns_context->plc) {
        pdlog(LOG_MODULE_OMRON_TAG_NAME_SERVER, LOG_LEVEL_ERROR, "Get Attributes All: context not initialized");
        cip_build_response(response, service, CIP_STATUS_SERVICE_ERROR);
        return UTIL_EINTERNAL;
    }

    /* Get tag count from array */
    size_t tag_count = tns_context->plc->tag_count;

    pdlog(LOG_MODULE_OMRON_TAG_NAME_SERVER, LOG_LEVEL_DETAIL, "Get Attributes All: returning count=%zu", tag_count);

    /* Build response */
    cip_build_response(response, service, CIP_STATUS_OK);

    bool ok = true;
    ok &= buf_write_u16_le(response, "reserved", 0);
    ok &= buf_write_u16_le(response, "count", (uint16_t)tag_count);

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
                                                            client_context_t *client) {

    (void)path;
    (void)client;
    (void)instance;

    if(!tns_context || !tns_context->plc) {
        pdlog(LOG_MODULE_OMRON_TAG_NAME_SERVER, LOG_LEVEL_ERROR, "Get Instance List: context not initialized");
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

    /* Only support kind=2 (user tags) */
    if(kind != 2) {
        pdlog(LOG_MODULE_OMRON_TAG_NAME_SERVER, LOG_LEVEL_DETAIL, "Get Instance List: kind=%u (only 2=user tags supported)",
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

    /* Find starting index for start_instance_id */
    size_t start_index = 0;
    if(start_instance_id > 0) {
        for(size_t i = 0; i < tns_context->plc->tag_count; i++) {
            if(tns_context->plc->tags[i]->instance_id >= start_instance_id) {
                start_index = i;
                break;
            }
        }
    }

    /* Count matching records and determine if more exist */
    uint32_t returned_count = 0;
    uint8_t more_flag = 0;

    for(size_t i = start_index; i < tns_context->plc->tag_count && returned_count < max_count; i++) { returned_count++; }
    if(start_index + returned_count < tns_context->plc->tag_count) { more_flag = 1; }

    /* Write header */
    bool ok = true;
    ok &= buf_write_u16_le(response, "returned_count", (uint16_t)returned_count);
    ok &= buf_write_u8(response, "more_flag", more_flag);
    ok &= buf_write_u8(response, "reserved", 0);

    if(!ok) {
        pdlog(LOG_MODULE_OMRON_TAG_NAME_SERVER, LOG_LEVEL_ERROR, "Get Instance List: failed to write response header");
        return buf_get_error(response);
    }

    /* Build records from array */
    uint32_t record_count = 0;

    for(size_t i = start_index; i < tns_context->plc->tag_count && record_count < max_count; i++) {
        tag_def_t *tag = tns_context->plc->tags[i];
        size_t name_len = strlen(tag->name);
        size_t padded_name_len = ((name_len + 1) / 2) * 2; /* Pad to word boundary */

        /* Calculate record length in words:
         * length(2) + class_id(2) + instance_id(4) + name_len(1) + name(padded) = 9 + padded_name_len
         * In words: (9 + padded_name_len) / 2 = 4-5 words typically */
        size_t record_len_bytes = 2 + 2 + 4 + 1 + padded_name_len;
        uint16_t record_len_words = (uint16_t)(record_len_bytes / 2);

        /* Write record */
        ok &= buf_write_u16_le(response, "record_length", record_len_words);
        ok &= buf_write_u16_le(response, "class_id", 0x6B); /* Variable Object */
        ok &= buf_write_u32_le(response, "instance_id", tag->instance_id);
        ok &= buf_write_u8(response, "name_length", (uint8_t)name_len);

        /* Write name with padding */
        uint8_t padded_name[256];
        memset(padded_name, 0, sizeof(padded_name));
        memcpy(padded_name, tag->name, name_len);
        ok &= buf_write_bytes(response, "name", padded_name, padded_name_len);

        if(!ok) {
            pdlog(LOG_MODULE_OMRON_TAG_NAME_SERVER, LOG_LEVEL_ERROR, "Get Instance List: failed to write record for '%s'",
                  tag->name);
            return buf_get_error(response);
        }

        pdlog(LOG_MODULE_OMRON_TAG_NAME_SERVER, LOG_LEVEL_DETAIL, "Get Instance List: record %u - id=%u name='%s'", record_count,
              tag->instance_id, tag->name);

        record_count++;
    }

    pdlog(LOG_MODULE_OMRON_TAG_NAME_SERVER, LOG_LEVEL_DETAIL, "Get Instance List: returned %u records, more_flag=%u",
          returned_count, more_flag);

    return UTIL_OK;
}

/* ============================================================================
 * Instance Management
 * ============================================================================ */

static cip_object_instance_t *tag_name_server_get_instance(uint32_t instance_id, client_context_t *client) {
    (void)client;

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

void tag_name_server_omron_register(cip_object_registry_t *registry, client_context_t *client) {
    if(!registry || !client) { return; }

    plc_context_t *plc = client->plc;

    /* Store PLC context for service handlers */
    if(!tns_context) {
        tns_context = (tag_name_server_omron_context_t *)calloc(1, sizeof(*tns_context));
        if(!tns_context) { return; }
    }

    tns_context->plc = plc;

    /* Create and register the object class */
    cip_object_class_t *cls = (cip_object_class_t *)calloc(1, sizeof(*cls));
    if(!cls) { return; }

    cls->class_id = 0x6A;
    cls->class_name = "Tag Name Server (Omron)";

    /* Register service handlers */
    cls->service_handlers[CIP_SRV_GET_ATTR_ALL] = tag_name_server_service_get_attributes_all;
    cls->service_handlers[CIP_SRV_GET_INSTANCE_LIST_OMRON] = tag_name_server_service_get_instance_list;

    /* Instance management */
    cls->get_instance = tag_name_server_get_instance;

    /* Register in registry */
    cip_registry_register_class(registry, cls);

    pdlog(LOG_MODULE_OMRON_TAG_NAME_SERVER, LOG_LEVEL_INFO, "Registered Tag Name Server (Class 0x6A)");
}
