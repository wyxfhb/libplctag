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
#include "ab_context.h"

/* ============================================================================
 * AB PLC Context Creation
 * ============================================================================ */

ab_plc_context_t *ab_plc_context_create(plc_type_t plc_type) {
    ab_plc_context_t *plc = (ab_plc_context_t *)malloc(sizeof(ab_plc_context_t));
    if(!plc) { return NULL; }

    memset(plc, 0, sizeof(*plc));

    plc->plc_type = plc_type;

    /* Create CIP registry */
    plc->cip_registry = cip_class_registry_create();
    if(!plc->cip_registry) {
        free(plc);
        return NULL;
    }

    /* Initialize tag array */
    if(tag_array_init(&plc->tags, &plc->tag_capacity) != 0) {
        cip_class_registry_destroy(plc->cip_registry);
        free(plc);
        return NULL;
    }

    /* Initialize UDT array */
    if(udt_array_init(&plc->udts, &plc->udt_capacity) != 0) {
        tag_array_destroy(plc->tags, plc->tag_count);
        cip_class_registry_destroy(plc->cip_registry);
        free(plc);
        return NULL;
    }

    plc->next_session_id = 1;

    return plc;
}

/* ============================================================================
 * AB PLC Context Destruction
 * ============================================================================ */

void ab_plc_context_destroy(ab_plc_context_t *plc) {
    if(!plc) { return; }

    /* Destroy all tags */
    if(plc->tags) {
        tag_array_destroy(plc->tags, plc->tag_count);
    }

    /* Destroy all UDTs */
    if(plc->udts) {
        udt_array_destroy(plc->udts, plc->udt_count);
    }

    /* Destroy CIP registry */
    if(plc->cip_registry) {
        cip_class_registry_destroy(plc->cip_registry);
    }

    free(plc);
}
