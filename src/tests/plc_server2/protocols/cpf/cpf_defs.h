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
 * Common Packet Format (CPF) Item Types
 * ============================================================================ */

#define CPF_ITEM_NULL ((uint16_t)0x0000)               /* Null item - no data */
#define CPF_ITEM_NULL_ADDRESS ((uint16_t)0x0000)       /* Null Address item */
#define CPF_ITEM_ADDRESS_INFO ((uint16_t)0x8000)       /* Address information (target) */
#define CPF_ITEM_CONNECTED_ADDRESS ((uint16_t)0x00A1)  /* Connected Address item */
#define CPF_ITEM_CONNECTED_DATA ((uint16_t)0x00B1)     /* Connected data */
#define CPF_ITEM_UNCONNECTED_DATA ((uint16_t)0x00B2)   /* Unconnected data (CIP service) */
#define CPF_ITEM_INDENTITY_INFO ((uint16_t)0x8000)     /* Identity Object info */
#define CPF_ITEM_SOCK_ADDR_INFO_OPT ((uint16_t)0x0001) /* Socket address info (optional) */

/* ============================================================================
 * CPF Header Information
 * ============================================================================ */

#define CPF_MAX_ITEMS ((size_t)8) /* Arbitrary limit on number of items in CPF */

/* CPF Header size (interface handle + router timeout + item count) */
#define CPF_HEADER_SIZE 8

/* ===========================================================================
 * CPF Item Size Definitions
 * =========================================================================== */

/* address item sizes */
#define CPF_ITEM_UCONN_ADDRESS_SIZE ((size_t)4) /* type + length, both with value zero */
#define CPF_ITEM_CONN_ADDRESS_SIZE ((size_t)8)  /* type + length + connection ID (4 bytes in our case)*/

/* data item sizes */
#define CPF_ITEM_CONN_DATA_SIZE ((size_t)6)  /* type + length + conn sequence number */
#define CPF_ITEM_UCONN_DATA_SIZE ((size_t)4) /* type + length */
