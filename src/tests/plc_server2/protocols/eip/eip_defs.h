#pragma once

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

#include <stdint.h>

/* ============================================================================
 * EIP Constants
 * ============================================================================ */

/* EIP Encapsulation Header Size */
#define EIP_HEADER_SIZE 24

/* EIP Command Codes */
#define EIP_CMD_NOP 0x0000
#define EIP_CMD_LIST_SERVICES 0x0004
#define EIP_CMD_LIST_IDENTITY 0x0063
#define EIP_CMD_LIST_INTERFACES 0x0064
#define EIP_CMD_REGISTER_SESSION 0x0065
#define EIP_CMD_UNREGISTER_SESSION 0x0066
#define EIP_CMD_SEND_RR_DATA 0x006F   /* Unconnected message */
#define EIP_CMD_SEND_UNIT_DATA 0x0070 /* Connected message */

/* EIP Status Codes */
#define EIP_STATUS_OK 0x0000              /* Command executed OK */
#define EIP_STATUS_INVALID_CMD 0x0001     /* Invalid command */
#define EIP_STATUS_NO_RESOURCES 0x0002    /* Device is unable to process command */
#define EIP_STATUS_INVALID_DATA 0x0003    /* Error in data provided */
#define EIP_STATUS_INVALID_SESSION 0x0064 /* Invalid Session Handle */
#define EIP_STATUS_INVALID_LENGTH 0x0065  /* Encapsulation message too short */
