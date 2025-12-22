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

#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include "../../../utils/err.h"
#include "../../protocols/cip/cip_registry.h"

/* ============================================================================
 * Symbol Object (Class 0x6B)
 * ============================================================================
 *
 * The Symbol Object provides access to tags on AB ControlLogix PLCs.
 * This is the primary way that clients read and write tag values.
 *
 * Service codes:
 * - 0x4C: Read Tag (via route)
 * - 0x4D: Write Tag (via route)
 * - 0x52: Read Tag Fragmented (for large reads)
 * - 0x53: Write Tag Fragmented (for large writes)
 */

/**
 * @brief Register Symbol Object service handlers
 *
 * Registers the read/write tag handlers with the CIP registry.
 *
 * @param registry CIP registry to register with
 * @return UTIL_OK on success, error code on failure
 */
util_err_t symbol_object_register(cip_class_registry_t *registry);

#ifdef __cplusplus
}
#endif
