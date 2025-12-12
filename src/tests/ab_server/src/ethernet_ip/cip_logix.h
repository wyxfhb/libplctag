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
 * logix_handle_read_request - Handle ControlLogix-specific read request
 *
 * ControlLogix reads use:
 * - Service 0x4C for initial read
 * - Service 0x52 for fragmented continuation
 * - Returns status 0x06 if more data available
 * - Supports partial responses with offset in payload
 *
 * Returns: Response slice
 */
slice_s logix_handle_read_request(uint8_t cip_service, slice_s cip_service_path, slice_s cip_service_payload,
                                  slice_s output, plc_s *plc);

/**
 * logix_handle_write_request - Handle ControlLogix-specific write request
 *
 * Similar to read but supports fragmentation for writes
 *
 * Returns: Response slice
 */
slice_s logix_handle_write_request(uint8_t cip_service, slice_s cip_service_path, slice_s cip_service_payload,
                                   slice_s output, plc_s *plc);

/**
 * logix_handle_list_tags - Handle ControlLogix tag enumeration (Service 0x55)
 *
 * Handles List Tags Info command for discovering and reading tag metadata.
 * - Service 0x55 (List Tags Info)
 * - Returns tag attributes: type, size, dimensions, name
 * - Supports controller and program-scoped tags
 *
 * Returns: Response slice with tag metadata
 */
slice_s logix_handle_list_tags(uint8_t cip_service, slice_s cip_service_path, slice_s cip_service_payload,
                               slice_s output, plc_s *plc);
