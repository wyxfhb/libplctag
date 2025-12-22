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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../plcs/ab/ab_context.h"
#include "../plcs/ab/ab_logix.h"
#include "../plcs/generic/tag_storage.h"
#include "../context/context_registry.h"
#include "../context/context_defs.h"

/* ============================================================================
 * Usage and Help
 * ============================================================================ */

void print_usage(const char *program) {
    printf("Usage: %s [OPTIONS]\n", program);
    printf("\nOptions:\n");
    printf("  --plc-type TYPE     PLC type (default: controllogix)\n");
    printf("                       Supported: controllogix, micro800\n");
    printf("  --port PORT         Listen port (default: 2222)\n");
    printf("  --help              Show this help message\n");
    printf("\nExample:\n");
    printf("  %s --plc-type controllogix --port 2222\n", program);
}

/* ============================================================================
 * Main Server Entry Point - Stub
 * ============================================================================
 *
 * TODO: Complete the server implementation
 * 1. Create listener socket
 * 2. Accept client connections
 * 3. Create per-client context registry
 * 4. Read EIP packets and dispatch
 * 5. Handle disconnections
 *
 * For now, this is a stub that just demonstrates the structure.
 */

int main(int argc, char **argv) {
    printf("PLC Server 2 - Refactored Architecture\n");
    printf("Copyright (c) 2025 Kyle Hayes\n\n");

    /* Parse command line arguments */
    const char *plc_type_str = "controllogix";
    int port = 2222;

    for(int i = 1; i < argc; i++) {
        if(strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if(strcmp(argv[i], "--plc-type") == 0 && i + 1 < argc) {
            plc_type_str = argv[++i];
        } else if(strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            port = atoi(argv[++i]);
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    printf("Starting PLC Server\n");
    printf("  PLC Type: %s\n", plc_type_str);
    printf("  Port: %d\n\n", port);

    /* Determine PLC type */
    plc_type_t plc_type = PLC_TYPE_CONTROLLOGIX;
    if(strcmp(plc_type_str, "controllogix") == 0) {
        plc_type = PLC_TYPE_CONTROLLOGIX;
    } else if(strcmp(plc_type_str, "micro800") == 0) {
        plc_type = PLC_TYPE_MICRO800;
        fprintf(stderr, "Error: Micro800 support not yet implemented\n");
        return 1;
    } else {
        fprintf(stderr, "Error: Unknown PLC type: %s\n", plc_type_str);
        return 1;
    }

    /* Create AB PLC context */
    printf("Creating PLC context...\n");
    ab_plc_context_t *plc = ab_plc_context_create(plc_type);
    if(!plc) {
        fprintf(stderr, "Error: Failed to create PLC context\n");
        return 1;
    }

    /* Register ControlLogix objects */
    printf("Registering CIP objects...\n");
    if(ab_logix_register_objects(plc) != 0) {
        fprintf(stderr, "Error: Failed to register CIP objects\n");
        ab_plc_context_destroy(plc);
        return 1;
    }

    /* Add some sample tags for testing */
    printf("Creating sample tags...\n");
    tag_def_t *tag1 = tag_create_dint("MyTag", 42);
    if(!tag1) {
        fprintf(stderr, "Error: Failed to create sample tag\n");
        ab_plc_context_destroy(plc);
        return 1;
    }
    if(tag_array_add(&plc->tags, &plc->tag_count, &plc->tag_capacity, tag1) != 0) {
        fprintf(stderr, "Error: Failed to add sample tag\n");
        tag_destroy(tag1);
        ab_plc_context_destroy(plc);
        return 1;
    }
    printf("  Added tag: MyTag = %d\n", *(int32_t *)tag1->data);

    printf("\nServer initialized successfully.\n");
    printf("TODO: Implement socket listener and connection handler\n");

    /* TODO: Implement the actual server loop */
    printf("\nShutting down...\n");
    ab_plc_context_destroy(plc);

    printf("Goodbye!\n");
    return 0;
}
