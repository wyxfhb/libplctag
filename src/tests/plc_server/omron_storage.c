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
#include "omron_storage.h"

/* ============================================================================
 * Registry Management
 * ============================================================================ */

omron_registry_t* omron_registry_create(void) {
    omron_registry_t *reg = (omron_registry_t *)calloc(1, sizeof(*reg));
    if (!reg) {
        return NULL;
    }

    /* Initialize ID counters (start at 1, not 0) */
    reg->next_var_instance_id = 1;
    reg->next_type_instance_id = 1;

    return reg;
}

void omron_registry_destroy(omron_registry_t *registry) {
    if (!registry) {
        return;
    }

    /* Destroy all variables */
    omron_variable_t *var = registry->variables;
    while (var) {
        omron_variable_t *next = var->next;
        omron_variable_destroy(var);
        var = next;
    }

    /* Destroy all type members */
    omron_type_member_t *member = registry->members;
    while (member) {
        omron_type_member_t *next = member->next;
        omron_member_destroy(member);
        member = next;
    }

    /* Destroy all types */
    omron_type_def_t *type = registry->types;
    while (type) {
        omron_type_def_t *next = type->next;
        free(type);
        type = next;
    }

    free(registry);
}

/* ============================================================================
 * Variable Management
 * ============================================================================ */

omron_variable_t* omron_variable_create(omron_registry_t *registry,
                                         const char *name,
                                         uint16_t type_code,
                                         size_t size) {
    if (!registry || !name) {
        return NULL;
    }

    omron_variable_t *var = (omron_variable_t *)calloc(1, sizeof(*var));
    if (!var) {
        return NULL;
    }

    /* Assign instance ID */
    var->instance_id = registry->next_var_instance_id++;
    var->type_code = type_code;
    var->data_size = size;
    var->dim_count = 0;  /* Scalar */
    var->type_instance_id = 0;  /* No structure */

    /* Copy name */
    strncpy(var->name, name, sizeof(var->name) - 1);
    var->name[sizeof(var->name) - 1] = '\0';

    /* Allocate data buffer */
    var->data = (uint8_t *)calloc(1, size);
    if (!var->data && size > 0) {
        free(var);
        return NULL;
    }

    /* Add to list */
    var->next = registry->variables;
    registry->variables = var;

    return var;
}

omron_variable_t* omron_variable_create_array(omron_registry_t *registry,
                                               const char *name,
                                               uint16_t type_code,
                                               size_t elem_size,
                                               size_t elem_count) {
    if (!registry || !name || elem_count == 0) {
        return NULL;
    }

    size_t total_size = elem_size * elem_count;
    omron_variable_t *var = omron_variable_create(registry, name, type_code, total_size);
    if (!var) {
        return NULL;
    }

    /* Set 1D array info */
    var->dim_count = 1;
    var->dimensions[0] = (uint32_t)elem_count;

    return var;
}

omron_variable_t* omron_variable_create_array_multi(omron_registry_t *registry,
                                                     const char *name,
                                                     uint16_t type_code,
                                                     size_t elem_size,
                                                     size_t dim_count,
                                                     const uint32_t *dimensions) {
    if (!registry || !name || dim_count == 0 || dim_count > 3 || !dimensions) {
        return NULL;
    }

    /* Calculate total elements */
    size_t total_elems = 1;
    for (size_t i = 0; i < dim_count; i++) {
        if (dimensions[i] == 0) {
            return NULL;
        }
        total_elems *= dimensions[i];
    }

    size_t total_size = elem_size * total_elems;
    omron_variable_t *var = omron_variable_create(registry, name, type_code, total_size);
    if (!var) {
        return NULL;
    }

    /* Set multi-dimensional array info */
    var->dim_count = (uint8_t)dim_count;
    memcpy(var->dimensions, dimensions, dim_count * sizeof(uint32_t));

    return var;
}

omron_variable_t* omron_variable_create_struct(omron_registry_t *registry,
                                                const char *name,
                                                uint32_t type_id,
                                                omron_type_def_t *type_def) {
    if (!registry || !name || !type_def) {
        return NULL;
    }

    omron_variable_t *var = omron_variable_create(registry, name, 0xA2, type_def->total_size);
    if (!var) {
        return NULL;
    }

    /* Mark as structure */
    var->type_code = 0xA2;  /* Abbreviated structure */
    var->type_instance_id = type_id;
    var->dim_count = 0;  /* Scalar structure */

    return var;
}

omron_variable_t* omron_variable_create_string(omron_registry_t *registry,
                                                const char *name,
                                                const char *initial_value) {
    if (!registry || !name) {
        return NULL;
    }

    /* Omron string: 2-byte length + 82-byte buffer = 84 bytes */
    const size_t STRING_SIZE = 84;
    omron_variable_t *var = omron_variable_create(registry, name, 0xD0, STRING_SIZE);
    if (!var) {
        return NULL;
    }

    /* Initialize string with length prefix */
    if (initial_value) {
        size_t len = strlen(initial_value);
        if (len > 82) {
            len = 82;
        }

        /* Write 2-byte length (little-endian) */
        var->data[0] = (uint8_t)(len & 0xFF);
        var->data[1] = (uint8_t)((len >> 8) & 0xFF);

        /* Copy string data */
        memcpy(var->data + 2, initial_value, len);

        /* Zero-fill remainder */
        if (len < 82) {
            memset(var->data + 2 + len, 0, 82 - len);
        }
    } else {
        /* Empty string */
        var->data[0] = 0;
        var->data[1] = 0;
        memset(var->data + 2, 0, 82);
    }

    return var;
}

omron_variable_t* omron_variable_find_by_id(omron_registry_t *registry,
                                             uint32_t instance_id) {
    if (!registry) {
        return NULL;
    }

    for (omron_variable_t *var = registry->variables; var != NULL; var = var->next) {
        if (var->instance_id == instance_id) {
            return var;
        }
    }

    return NULL;
}

omron_variable_t* omron_variable_find_by_name(omron_registry_t *registry,
                                               const char *name) {
    if (!registry || !name) {
        return NULL;
    }

    for (omron_variable_t *var = registry->variables; var != NULL; var = var->next) {
        if (strcmp(var->name, name) == 0) {
            return var;
        }
    }

    return NULL;
}

omron_variable_t* omron_variable_find_by_name_len(omron_registry_t *registry,
                                                   const char *name, size_t name_len) {
    if (!registry || !name) {
        return NULL;
    }

    for (omron_variable_t *var = registry->variables; var != NULL; var = var->next) {
        if (strlen(var->name) == name_len && strncmp(var->name, name, name_len) == 0) {
            return var;
        }
    }

    return NULL;
}

void omron_variable_destroy(omron_variable_t *var) {
    if (!var) {
        return;
    }

    if (var->data) {
        free(var->data);
    }

    free(var);
}

/* ============================================================================
 * Type Definition Management
 * ============================================================================ */

omron_type_def_t* omron_type_create(omron_registry_t *registry,
                                     const char *type_name,
                                     size_t total_size) {
    if (!registry || !type_name) {
        return NULL;
    }

    omron_type_def_t *type = (omron_type_def_t *)calloc(1, sizeof(*type));
    if (!type) {
        return NULL;
    }

    /* Assign instance ID */
    type->type_instance_id = registry->next_type_instance_id++;
    type->type_code = 0xA0;  /* Structure */
    type->total_size = total_size;
    type->member_count = 0;
    type->first_member_id = 0;

    /* Copy name */
    strncpy(type->type_name, type_name, sizeof(type->type_name) - 1);
    type->type_name[sizeof(type->type_name) - 1] = '\0';

    /* Add to list */
    type->next = registry->types;
    registry->types = type;

    return type;
}

omron_type_member_t* omron_member_add(omron_registry_t *registry,
                                       omron_type_def_t *parent_type,
                                       const char *member_name,
                                       uint16_t member_type,
                                       size_t offset,
                                       size_t size) {
    if (!registry || !parent_type || !member_name) {
        return NULL;
    }

    omron_type_member_t *member = (omron_type_member_t *)calloc(1, sizeof(*member));
    if (!member) {
        return NULL;
    }

    /* Assign instance ID */
    member->member_instance_id = registry->next_type_instance_id++;
    member->member_type = member_type;
    member->member_offset = offset;
    member->member_size = size;
    member->nesting_type_id = 0;  /* Updated if nested structure */

    /* Copy name */
    strncpy(member->member_name, member_name, sizeof(member->member_name) - 1);
    member->member_name[sizeof(member->member_name) - 1] = '\0';

    /* Link to type definition */
    if (parent_type->member_count == 0) {
        /* First member */
        parent_type->first_member_id = member->member_instance_id;
    } else {
        /* Find last member and link */
        omron_type_member_t *last = omron_member_find_by_id(registry,
                                                             registry->next_type_instance_id - 2);
        if (last) {
            last->next_member_id = member->member_instance_id;
        }
    }

    parent_type->member_count++;

    /* Add to global member list */
    member->next = registry->members;
    registry->members = member;

    return member;
}

omron_type_def_t* omron_type_find_by_id(omron_registry_t *registry,
                                         uint32_t type_instance_id) {
    if (!registry) {
        return NULL;
    }

    for (omron_type_def_t *type = registry->types; type != NULL; type = type->next) {
        if (type->type_instance_id == type_instance_id) {
            return type;
        }
    }

    return NULL;
}

omron_type_member_t* omron_member_find_by_id(omron_registry_t *registry,
                                              uint32_t member_instance_id) {
    if (!registry) {
        return NULL;
    }

    for (omron_type_member_t *member = registry->members; member != NULL; member = member->next) {
        if (member->member_instance_id == member_instance_id) {
            return member;
        }
    }

    return NULL;
}

omron_type_member_t* omron_member_find_by_name(omron_registry_t *registry,
                                                omron_type_def_t *parent_type,
                                                const char *member_name) {
    if (!registry || !parent_type || !member_name) {
        return NULL;
    }

    /* Walk member chain starting from first_member_id */
    uint32_t current_id = parent_type->first_member_id;
    while (current_id != 0) {
        omron_type_member_t *member = omron_member_find_by_id(registry, current_id);
        if (!member) {
            break;
        }

        if (strcmp(member->member_name, member_name) == 0) {
            return member;
        }

        current_id = member->next_member_id;
    }

    return NULL;
}

void omron_type_destroy(omron_registry_t *registry, omron_type_def_t *type_def) {
    if (!type_def) {
        return;
    }

    /* Note: Members are not destroyed here - they remain in global members list
     * This is by design to keep the implementation simple. In production code,
     * you might want to also remove members from the registry. */

    free(type_def);
}

void omron_member_destroy(omron_type_member_t *member) {
    if (!member) {
        return;
    }

    free(member);
}
