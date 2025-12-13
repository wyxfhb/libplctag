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
#include "cip_object_registry.h"
#include "cip_message_router.h"
#include "log.h"

/* ============================================================================
 * Registry Creation and Destruction
 * ============================================================================ */

cip_object_registry_t* cip_registry_create(void) {
    cip_object_registry_t *registry = (cip_object_registry_t *)malloc(
        sizeof(cip_object_registry_t));
    if (!registry) {
        return NULL;
    }

    memset(registry, 0, sizeof(*registry));
    return registry;
}

void cip_registry_destroy(cip_object_registry_t *registry) {
    if (!registry) {
        return;
    }

    /* Note: We don't free the classes themselves - they're typically
     * statically allocated or managed by the owner */
    free(registry);
}

/* ============================================================================
 * Class Registration and Lookup
 * ============================================================================ */

util_err_t cip_registry_register_class(cip_object_registry_t *registry,
                                       cip_object_class_t *obj_class) {
    if (!registry || !obj_class) {
        return UTIL_EINVAL;
    }

    if (obj_class->class_id >= 256) {
        pdlog(LOG_MODULE_CIP_ROUTER, LOG_LEVEL_WARN,
              "Registry: class ID 0x%04X exceeds max 255",
              obj_class->class_id);
        return UTIL_EINVAL;
    }

    registry->classes[obj_class->class_id] = obj_class;
    registry->class_count++;

    pdlog(LOG_MODULE_CIP_ROUTER, LOG_LEVEL_INFO,
          "Registry: registered class 0x%02X (%s)",
          obj_class->class_id, obj_class->class_name);

    return UTIL_OK;
}

cip_object_class_t* cip_registry_find_class(cip_object_registry_t *registry,
                                            uint16_t class_id) {
    if (!registry || class_id >= 256) {
        return NULL;
    }

    return registry->classes[class_id];
}

/* ============================================================================
 * Service Dispatch
 * ============================================================================ */

util_err_t cip_registry_dispatch(cip_object_registry_t *registry,
                                uint16_t class_id, uint32_t instance_id,
                                uint8_t service, const cip_path_t *path,
                                buf_t *request, buf_t *response,
                                plc_context_t *plc) {
    /* Find class */
    cip_object_class_t *obj_class = cip_registry_find_class(registry, class_id);
    if (!obj_class) {
        pdlog(LOG_MODULE_CIP_ROUTER, LOG_LEVEL_WARN,
              "Registry dispatch: class 0x%02X not found", class_id);
        cip_build_response(response, service, CIP_STATUS_PATH_DEST_UNKNOWN);
        return UTIL_ENOTFOUND;
    }

    /* Get instance */
    cip_object_instance_t *instance = NULL;
    if (obj_class->get_instance) {
        instance = obj_class->get_instance(instance_id, plc);
        if (!instance) {
            pdlog(LOG_MODULE_CIP_ROUTER, LOG_LEVEL_WARN,
                  "Registry dispatch: instance %u of class 0x%02X not found",
                  instance_id, class_id);
            cip_build_response(response, service, CIP_STATUS_PATH_DEST_UNKNOWN);
            return UTIL_ENOTFOUND;
        }
    }

    /* Find service handler */
    cip_service_handler_t handler = obj_class->service_handlers[service];
    if (!handler) {
        pdlog(LOG_MODULE_CIP_ROUTER, LOG_LEVEL_WARN,
              "Registry dispatch: service 0x%02X not supported by class 0x%02X",
              service, class_id);
        cip_build_response(response, service, CIP_STATUS_SERVICE_NOT_SUPPORTED);
        return UTIL_ENOTSUPPORTED;
    }

    pdlog(LOG_MODULE_CIP_ROUTER, LOG_LEVEL_DETAIL,
          "Registry dispatch: class=0x%02X instance=%u service=0x%02X",
          class_id, instance_id, service);

    /* Dispatch to handler */
    util_err_t err = handler(service, path, request, response, instance, plc);

    if (err != UTIL_OK) {
        pdlog(LOG_MODULE_CIP_ROUTER, LOG_LEVEL_DETAIL,
              "Registry dispatch: handler returned %s", util_err_str(err));
    }

    return err;
}
