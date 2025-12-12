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

#include "../plc.h"
#include "../slice.h"

/**
 * omron_handle_read_request - Handle Omron-specific read request
 *
 * Omron reads use:
 * - Service 0x4C (not fragmented 0x52)
 * - 0x80 segment in payload for offset encoding
 * - Returns error (0x15 TOO_MUCH_DATA) if response doesn't fit
 * - No partial responses or stub responses
 *
 * Returns: Response slice
 */
slice_s omron_handle_read_request(uint8_t cip_service, slice_s cip_service_path, slice_s cip_service_payload,
                                  slice_s output, plc_s *plc);

/**
 * omron_handle_write_request - Handle Omron-specific write request
 *
 * Similar to read but writes data to a tag
 *
 * Returns: Response slice
 */
slice_s omron_handle_write_request(uint8_t cip_service, slice_s cip_service_path, slice_s cip_service_payload,
                                   slice_s output, plc_s *plc);
