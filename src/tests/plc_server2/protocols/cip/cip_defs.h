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
 * CIP Service Codes
 * ============================================================================ */

/* General Services (all objects) */
#define CIP_SRV_GET_ATTR_ALL 0x01
#define CIP_SRV_SET_ATTR_ALL 0x02
#define CIP_SRV_GET_ATTR_LIST 0x03
#define CIP_SRV_SET_ATTR_LIST 0x04
#define CIP_SRV_GET_ATTR_SINGLE 0x0E
#define CIP_SRV_SET_ATTR_SINGLE 0x10

/* Message Router Services */
#define CIP_SRV_MULTI_REQUEST 0x0A

/* Connection Manager Services */
#define CIP_SRV_FORWARD_CLOSE 0x4E
#define CIP_SRV_FORWARD_OPEN 0x54
#define CIP_SRV_LARGE_FORWARD_OPEN 0x5B

/* Common tag services */
#define CIP_SRV_READ_TAG 0x4C
#define CIP_SRV_WRITE_TAG 0x4D

/* Symbol Object Services (Rockwell/AB) */
#define CIP_SRV_READ_TAG_FRAG_AB 0x52
#define CIP_SRV_WRITE_TAG_FRAG_AB 0x53

/* Special CIP Services Rockwell/AB */
#define CIP_SRV_INSTANCE_ATTRS_AB 0x55
#define CIP_SRV_EXEC_PCCC_AB 0x4B

/* Symbol Object Services (Omron) */
#define CIP_SRV_GET_INSTANCE_LIST_OMRON 0x5F

/* ============================================================================
 * CIP Data Type Codes
 * ============================================================================ */

#define CIP_TYPE_BOOL 0xC1 /* BOOL (logical) */
#define CIP_TYPE_SINT 0xC2 /* SINT (8-bit signed) */
#define CIP_TYPE_INT 0xC3  /* INT (16-bit signed) */
#define CIP_TYPE_DINT 0xC4 /* DINT (32-bit signed) */
#define CIP_TYPE_REAL 0xCA /* REAL (32-bit floating point) */

/* ============================================================================
 * CIP Class IDs
 * ============================================================================ */

#define CIP_CLASS_IDENTITY 0x01           /* Identity Object */
#define CIP_CLASS_MESSAGE_ROUTER 0x02     /* Message Router */
#define CIP_CLASS_CONNECTION_MANAGER 0x06 /* Connection Manager */
#define CIP_CLASS_SYMBOL_OBJECT 0x6B      /* Symbol Object (AB) */

/* ============================================================================
 * CIP Status Codes
 * ============================================================================ */

#define CIP_STATUS_SUCCESS 0x00              /* Success */
#define CIP_STATUS_OK 0x00                   /* Alias for success */
#define CIP_STATUS_INVALID_PARAM 0x04        /* Invalid parameter value */
#define CIP_STATUS_NOT_FOUND 0x05            /* Object specified in the request is not available */
#define CIP_STATUS_INVALID_SERVICE 0x08      /* The service is not supported */
#define CIP_STATUS_NOT_ENOUGH_DATA 0x13      /* Not enough data in request */
#define CIP_STATUS_REPLY_DATA_TOO_LARGE 0x01 /* Reply message is too large */
#define CIP_STATUS_INVALID_PATH 0x20         /* Path segment error */
#define CIP_STATUS_PATH_DEST_UNKNOWN 0x20    /* Path destination unknown */
#define CIP_STATUS_INVALID_DATA 0x04         /* Invalid parameter value */
#define CIP_STATUS_PARTIAL_TRANSFER 0x06     /* Partial transfer (fragmented data) */

/* ============================================================================
 * CIP Instance Numbers
 * ============================================================================ */

#define CIP_INSTANCE_ALL 0x00 /* Instance 0 (all instances) */
