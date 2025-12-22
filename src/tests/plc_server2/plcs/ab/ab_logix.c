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

#include "ab_logix.h"
#include "symbol_object.h"

/* ============================================================================
 * AB ControlLogix Setup
 * ============================================================================
 *
 * Registers the standard AB ControlLogix CIP objects:
 * - Identity Object (Class 0x01)
 * - Connection Manager (Class 0x06)
 * - Symbol Object (Class 0x6B) for tag access
 *
 * TODO: Port actual service handlers from plc_server/protocol/objects/
 */

util_err_t ab_logix_register_objects(ab_plc_context_t *plc) {
    if(!plc || !plc->cip_registry) {
        return UTIL_EINVAL;
    }

    /* Set Symbol Object as default for symbolic segments (tag names) */
    util_err_t err = cip_class_registry_set_symbolic_handler_class(plc->cip_registry, 0x6B);
    if(err != UTIL_OK) {
        return err;
    }

    /* Register Symbol Object service handlers */
    err = symbol_object_register(plc->cip_registry);
    if(err != UTIL_OK) {
        return err;
    }

    /* TODO: Register Identity Object services */
    /* TODO: Register Connection Manager services */

    return UTIL_OK;
}
