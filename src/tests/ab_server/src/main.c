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

#include "compat.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(IS_WINDOWS)
#    include <windows.h>
#else
/* assume it is POSIX of some sort... */
#    include <signal.h>
#    include <strings.h>
#endif

#include "ethernet_ip/eip.h"
#include "mutex.h"
#include "plc.h"
#include "slice.h"
#include "tcp_server.h"
#include "utils.h"
#include "mutex.h"
#include "../../utils/log.h"
#include "plcs/omron_plc.h"
#include "plcs/controllogix_plc.h"

static void usage(void);
static void process_args(int argc, const char **argv, plc_s *plc);
static void parse_path(const char *path, plc_s *plc);
static void parse_pccc_tag(const char *tag, plc_s *plc);
static void parse_cip_tag(const char *tag, plc_s *plc);
static void parse_struct_definition(const char *struct_str, plc_s *plc);
static void assign_type_instance_ids(plc_s *plc);
static slice_s request_handler(slice_s input, slice_s output, void *plc);


#ifdef IS_WINDOWS

typedef volatile int sig_flag_t;

sig_flag_t done = 0;

/* straight from MS' web site :-) */
int WINAPI CtrlHandler(DWORD fdwCtrlType) {
    switch(fdwCtrlType) {
            // Handle the CTRL-C signal.
        case CTRL_C_EVENT:
            pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "^C event");
            done = 1;
            return TRUE;

            // CTRL-CLOSE: confirm that the user wants to exit.
        case CTRL_CLOSE_EVENT:
            pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Close event");
            done = 1;
            return TRUE;

            // Pass other signals to the next handler.
        case CTRL_BREAK_EVENT:
            pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "^Break event");
            done = 1;
            return TRUE;

        case CTRL_LOGOFF_EVENT:
            pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Logoff event");
            done = 1;
            return TRUE;

        case CTRL_SHUTDOWN_EVENT:
            pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Shutdown event");
            done = 1;
            return TRUE;

        default: pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Default Event: %d", fdwCtrlType); return FALSE;
    }
}


void setup_break_handler(void) {
    if(!SetConsoleCtrlHandler(CtrlHandler, TRUE)) {
        printf("\nERROR: Could not set control handler!\n");
        usage();
    }
}

#else

typedef volatile sig_atomic_t sig_flag_t;

sig_flag_t done = 0;

void SIGINT_handler(int not_used) {
    (void)not_used;

    done = 1;
}

void setup_break_handler(void) {
    struct sigaction act;

    /* set up signal handler. */
    // NOLINTNEXTLINE
    memset(&act, 0, sizeof(act));
    act.sa_handler = SIGINT_handler;
    sigaction(SIGINT, &act, NULL);
}

#endif


int main(int argc, const char **argv) {
    tcp_server_p server = NULL;
    plc_s plc;

    /* set up handler for ^C etc. */
    setup_break_handler();

    log_set_all_modules(LOG_LEVEL_DETAIL);

    /* clear out context to make sure we do not get gremlins */
    // NOLINTNEXTLINE
    memset(&plc, 0, sizeof(plc));

    /* set the random seed. */
    srand((unsigned int)time(NULL));

    process_args(argc, argv, &plc);

    /* open a server connection and listen on the right port. */
    server = tcp_server_create("0.0.0.0", (plc.port_str ? plc.port_str : "44818"), request_handler, &plc, sizeof(plc));

    tcp_server_start(server, &done);

    /* Dump fairness statistics before shutdown */
    dump_fairness_stats(&plc);

    tcp_server_destroy(server);

    return 0;
}


void usage(void) {
    // NOLINTNEXTLINE
    fprintf(stderr, "Usage: ab_server --plc=<plc_type> [--path=<path>] [--port=<port>] --tag=<tag>\n"
                    "   <plc type> = one of the CIP PLCs: \"ControlLogix\", \"Micro800\" or \"Omron\",\n"
                    "                or one of the PCCC PLCs: \"PLC/5\", \"SLC500\" or \"Micrologix\".\n"
                    "\n"
                    "   <path> = (required for ControlLogix) internal path to CPU in PLC.  E.g. \"1,0\".\n"
                    "\n"
                    "   <port> = (required for ControlLogix) internal path to CPU in PLC.  E.g. \"1,0\".\n"
                    "            Defaults to 44818.\n"
                    "\n"
                    "    PCCC-based PLC tags are in the format: <file>[<size>] where:\n"
                    "        <file> is the data file, only the following are supported:\n"
                    "            B3   - 1-bit boolean value (as unsigned 16-bit integer).\n"
                    "            N7   - 2-byte signed integer.\n"
                    "            F8   - 4-byte floating point number.\n"
                    "            ST18 - 82-byte ASCII string.\n"
                    "            L19  - 4-byte signed integer.\n"
                    "\n"
                    "        <size> field is the length of the data file.\n"
                    "\n"
                    "    CIP-based PLC tags are in the format: <name>:<type>[<sizes>] where:\n"
                    "        <name> is alphanumeric, starting with an alpha character.\n"
                    "        <type> is one of:\n"
                    "            SINT   - 1-byte signed integer.  Requires array size(s).\n"
                    "            INT    - 2-byte signed integer.  Requires array size(s).\n"
                    "            DINT   - 4-byte signed integer.  Requires array size(s).\n"
                    "            LINT   - 8-byte signed integer.  Requires array size(s).\n"
                    "            REAL   - 4-byte floating point number.  Requires array size(s).\n"
                    "            LREAL  - 8-byte floating point number.  Requires array size(s).\n"
                    "            STRING - 82-byte string.  Requires array size(s).\n"
                    "            BOOL   - 1-byte boolean value.  Requires array size(s).\n"
                    "\n"
                    "        <sizes> field is one or more (up to 3) numbers separated by commas.\n"
                    "\n"
                    "Example: ab_server --plc=ControlLogix --path=1,0 --tag=MyTag:DINT[10,10]\n"
                    "         ab_server --plc=Micrologix --tag=B3[10] --tag=N7[10] --tag=L19[10]\n");

    exit(1);
}


void process_args(int argc, const char **argv, plc_s *plc) {
    bool has_path = false;
    bool needs_path = false;
    bool has_plc = false;
    bool has_tag = false;

    /* make sure that the reject FO count is zero. */
    plc->reject_fo_count = 0;

    /* PASS 1: Process --plc= only to set PLC type first (order-independent) */
    for(int i = 0; i < argc; i++) {
        if(strncmp(argv[i], "--plc=", 6) == 0) {
            if(has_plc) {
                // NOLINTNEXTLINE
                fprintf(stderr, "PLC type can only be specified once!\n");
                usage();
                return;
            }

            if(str_cmp_i(&(argv[i][6]), "ControlLogix") == 0) {
                // NOLINTNEXTLINE
                fprintf(stderr, "Selecting ControlLogix simulator.\n");
                plc->plc_type = PLC_CONTROL_LOGIX;
                plc->path[0] = (uint8_t)0x00; /* filled in later. */
                plc->path[1] = (uint8_t)0x00; /* filled in later. */
                plc->path[2] = (uint8_t)0x20;
                plc->path[3] = (uint8_t)0x02;
                plc->path[4] = (uint8_t)0x24;
                plc->path[5] = (uint8_t)0x01;
                plc->path_len = 6;
                plc->client_to_server_max_packet = 504;
                plc->server_to_client_max_packet = 504;
                needs_path = true;
                has_plc = true;
            } else if(str_cmp_i(&(argv[i][6]), "Micro800") == 0) {
                // NOLINTNEXTLINE
                fprintf(stderr, "Selecting Micro8xx simulator.\n");
                plc->plc_type = PLC_MICRO800;
                plc->path[0] = (uint8_t)0x20;
                plc->path[1] = (uint8_t)0x02;
                plc->path[2] = (uint8_t)0x24;
                plc->path[3] = (uint8_t)0x01;
                plc->path_len = 4;
                plc->client_to_server_max_packet = 504;
                plc->server_to_client_max_packet = 504;
                needs_path = false;
                has_plc = true;
            } else if(str_cmp_i(&(argv[i][6]), "Omron") == 0) {
                // NOLINTNEXTLINE
                fprintf(stderr, "Selecting Omron NJ/NX simulator.\n");
                plc->plc_type = PLC_OMRON;
                plc->path[0] = (uint8_t)0x12;  /* Extended segment, port A */
                plc->path[1] = (uint8_t)0x09;  /* 9 bytes length. */
                plc->path[2] = (uint8_t)0x31;  /* '1' */
                plc->path[3] = (uint8_t)0x32;  /* '2' */
                plc->path[4] = (uint8_t)0x37;  /* '7' */
                plc->path[5] = (uint8_t)0x2e;  /* '.' */
                plc->path[6] = (uint8_t)0x30;  /* '0' */
                plc->path[7] = (uint8_t)0x2e;  /* '.' */
                plc->path[8] = (uint8_t)0x30;  /* '0' */
                plc->path[9] = (uint8_t)0x2e;  /* '.' */
                plc->path[10] = (uint8_t)0x31; /* '1' */
                plc->path[11] = (uint8_t)0x00; /* padding */
                plc->path[12] = (uint8_t)0x20;
                plc->path[13] = (uint8_t)0x02;
                plc->path[14] = (uint8_t)0x24;
                plc->path[15] = (uint8_t)0x01;
                plc->path_len = 16;
                plc->client_to_server_max_packet = 504;
                plc->server_to_client_max_packet = 504;
                needs_path = false;
                has_plc = true;
            } else if(str_cmp_i(&(argv[i][6]), "PLC/5") == 0) {
                // NOLINTNEXTLINE
                fprintf(stderr, "Selecting PLC/5 simulator.\n");
                plc->plc_type = PLC_PLC5;
                plc->path[0] = (uint8_t)0x20;
                plc->path[1] = (uint8_t)0x02;
                plc->path[2] = (uint8_t)0x24;
                plc->path[3] = (uint8_t)0x01;
                plc->path_len = 4;
                plc->client_to_server_max_packet = 244;
                plc->server_to_client_max_packet = 244;
                needs_path = false;
                has_plc = true;
            } else if(str_cmp_i(&(argv[i][6]), "SLC500") == 0) {
                // NOLINTNEXTLINE
                fprintf(stderr, "Selecting SLC 500 simulator.\n");
                plc->plc_type = PLC_SLC;
                plc->path[0] = (uint8_t)0x20;
                plc->path[1] = (uint8_t)0x02;
                plc->path[2] = (uint8_t)0x24;
                plc->path[3] = (uint8_t)0x01;
                plc->path_len = 4;
                plc->client_to_server_max_packet = 244;
                plc->server_to_client_max_packet = 244;
                needs_path = false;
                has_plc = true;
            } else if(str_cmp_i(&(argv[i][6]), "Micrologix") == 0) {
                // NOLINTNEXTLINE
                fprintf(stderr, "Selecting Micrologix simulator.\n");
                plc->plc_type = PLC_MICROLOGIX;
                plc->path[0] = (uint8_t)0x20;
                plc->path[1] = (uint8_t)0x02;
                plc->path[2] = (uint8_t)0x24;
                plc->path[3] = (uint8_t)0x01;
                plc->path_len = 4;
                plc->client_to_server_max_packet = 244;
                plc->server_to_client_max_packet = 244;
                needs_path = false;
                has_plc = true;
            } else {
                // NOLINTNEXTLINE
                fprintf(stderr, "Unsupported PLC type %s!\n", &(argv[i][6]));
                usage();
            }
        }
    }

    /* PASS 1b: Process all other arguments except --tag= (PLC type is now known) */
    for(int i = 0; i < argc; i++) {
        if(strncmp(argv[i], "--path=", 7) == 0) {
            parse_path(&(argv[i][7]), plc);
            has_path = true;
        }

        if(strncmp(argv[i], "--port=", 7) == 0) { plc->port_str = &(argv[i][7]); }

        if(strncmp(argv[i], "--struct=", 9) == 0) {
            /* Structure types are only supported for Omron PLCs */
            if (plc && plc->plc_type == PLC_OMRON) {
                parse_struct_definition(&(argv[i][9]), plc);
            } else {
                // NOLINTNEXTLINE
                fprintf(stderr, "Structure definitions (--struct=) are only supported for Omron PLC type!\n");
                usage();
            }
            continue;
        }

        if(strcmp(argv[i], "--debug") == 0) { debug_on(); }

        if(strncmp(argv[i], "--reject_fo=", 12) == 0) {
            if(plc) {
                pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Setting reject ForwardOpen count to %d.", atoi(&argv[i][12]));
                plc->reject_fo_count = atoi(&argv[i][12]);
            }
        }

        if(strncmp(argv[i], "--delay=", 8) == 0) {
            if(plc) {
                pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Setting response delay to %dms.", atoi(&argv[i][8]));
                plc->response_delay = atoi(&argv[i][8]);
            }
        }

        /* Skip --plc= and --tag= in this pass */
    }

    /* Finalize structure definitions now that all --struct= have been parsed */
    if (plc) {
        assign_type_instance_ids(plc);
    }

    /* Initialize PLC-type specific dispatcher */
    if (plc) {
        switch (plc->plc_type) {
            case PLC_OMRON:
                plc->dispatcher = omron_get_dispatcher();
                pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Initialized Omron dispatcher");
                break;
            case PLC_CONTROL_LOGIX:
                plc->dispatcher = controllogix_get_dispatcher();
                pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Initialized ControlLogix dispatcher");
                break;
            case PLC_MICRO800:
            case PLC_PLC5:
            case PLC_SLC:
            case PLC_MICROLOGIX:
                /* Other PLC types: dispatchers to be implemented in future phases */
                pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Dispatcher for PLC type %d not yet implemented", plc->plc_type);
                // NOLINTNEXTLINE
                fprintf(stderr, "WARNING: Dispatcher for this PLC type is not yet implemented!\n");
                break;
            default:
                pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Unknown PLC type: %d", plc->plc_type);
                break;
        }
    }

    /* PASS 2: Process --tag= arguments (now that structures are finalized) */
    for(int i = 0; i < argc; i++) {
        if(strncmp(argv[i], "--tag=", 6) == 0) {
            if(plc && (plc->plc_type == PLC_PLC5 || plc->plc_type == PLC_SLC || plc->plc_type == PLC_MICROLOGIX)) {
                parse_pccc_tag(&(argv[i][6]), plc);
            } else {
                parse_cip_tag(&(argv[i][6]), plc);
            }
            has_tag = true;
        }
    }

    if(needs_path && !has_path) {
        // NOLINTNEXTLINE
        fprintf(stderr, "This PLC type requires a path argument.\n");
        usage();
    }

    if(!has_plc) {
        // NOLINTNEXTLINE
        fprintf(stderr, "You must pass a --plc= argument!\n");
        usage();
    }

    if(!has_tag) {
        // NOLINTNEXTLINE
        fprintf(stderr, "You must define at least one tag.\n");
        usage();
    }
}


void parse_path(const char *path_str, plc_s *plc) {
    int tmp_path[2];

    // NOLINTNEXTLINE
    if(str_scanf(path_str, "%d,%d", &tmp_path[0], &tmp_path[1]) == 2) {
        plc->path[0] = (uint8_t)tmp_path[0];
        plc->path[1] = (uint8_t)tmp_path[1];

        pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Processed path %d,%d.", plc->path[0], plc->path[1]);
    } else {
        // NOLINTNEXTLINE
        fprintf(stderr, "Error processing path \"%s\"!  Path must be two numbers separated by a comma.\n", path_str);
        usage();
    }
}


/*
 * PCCC tags are in the format:
 *    <data file>[<size>]
 *
 * Where data file is one of the following:
 *     N7 - 2 byte signed integer.  Requires size.
 *     F8 - 4-byte floating point number.   Requires size.
 *     ST18 - 82-byte string with 2-byte count word.
 *     L19 - 4 byte signed integer.   Requires size.
 *
 * The size field is a single positive integer.
 */

/* Helper function: Get alignment requirement for CIP type */
static size_t get_type_alignment(tag_type_t type) {
    switch(type) {
        case TAG_CIP_TYPE_BOOL:
        case TAG_CIP_TYPE_SINT:
        case TAG_CIP_TYPE_USINT:
            return 1;
        case TAG_CIP_TYPE_INT:
        case TAG_CIP_TYPE_UINT:
            return 2;
        case TAG_CIP_TYPE_DINT:
        case TAG_CIP_TYPE_UDINT:
        case TAG_CIP_TYPE_REAL:
            return 4;
        case TAG_CIP_TYPE_LINT:
        case TAG_CIP_TYPE_ULINT:
        case TAG_CIP_TYPE_LREAL:
            return 8;
        case TAG_CIP_TYPE_STRING:
            return 2;  /* STRING starts with 2-byte length */
        default:
            return 1;
    }
}

/* Helper function: Align offset to boundary */
static size_t align_offset(size_t offset, size_t alignment) {
    if (alignment == 0) return offset;
    return (offset + alignment - 1) & ~(alignment - 1);
}

/* Helper function: Find structure type by name */
static type_def_s* find_type_by_name(plc_s *plc, const char *name) {
    type_def_s *type = plc->types;
    while (type) {
        if (strcmp(type->name, name) == 0) {
            return type;
        }
        type = type->next_type;
    }
    return NULL;
}

/* Helper function: Map type string to CIP type and size */
static bool map_type_string_to_cip(const char *type_str, tag_type_t *out_type, size_t *out_size) {
    if (str_cmp_i(type_str, "SINT") == 0) {
        *out_type = TAG_CIP_TYPE_SINT;
        *out_size = 1;
    } else if (str_cmp_i(type_str, "INT") == 0) {
        *out_type = TAG_CIP_TYPE_INT;
        *out_size = 2;
    } else if (str_cmp_i(type_str, "DINT") == 0) {
        *out_type = TAG_CIP_TYPE_DINT;
        *out_size = 4;
    } else if (str_cmp_i(type_str, "LINT") == 0) {
        *out_type = TAG_CIP_TYPE_LINT;
        *out_size = 8;
    } else if (str_cmp_i(type_str, "USINT") == 0) {
        *out_type = TAG_CIP_TYPE_USINT;
        *out_size = 1;
    } else if (str_cmp_i(type_str, "UINT") == 0) {
        *out_type = TAG_CIP_TYPE_UINT;
        *out_size = 2;
    } else if (str_cmp_i(type_str, "UDINT") == 0) {
        *out_type = TAG_CIP_TYPE_UDINT;
        *out_size = 4;
    } else if (str_cmp_i(type_str, "ULINT") == 0) {
        *out_type = TAG_CIP_TYPE_ULINT;
        *out_size = 8;
    } else if (str_cmp_i(type_str, "REAL") == 0) {
        *out_type = TAG_CIP_TYPE_REAL;
        *out_size = 4;
    } else if (str_cmp_i(type_str, "LREAL") == 0) {
        *out_type = TAG_CIP_TYPE_LREAL;
        *out_size = 8;
    } else if (str_cmp_i(type_str, "STRING") == 0) {
        *out_type = TAG_CIP_TYPE_STRING;
        *out_size = 88;
    } else if (str_cmp_i(type_str, "BOOL") == 0) {
        *out_type = TAG_CIP_TYPE_BOOL;
        *out_size = 1;
    } else {
        return false;
    }
    return true;
}


/* Calculate CRC16-CCITT for data
 * Uses same algorithm as aphyt: poly=0xa001, initial=0x0000
 */
static uint16_t calculate_crc16(const uint8_t *data, size_t len) {
    uint16_t crc = 0x0000;
    const uint16_t poly = 0xa001;

    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            uint16_t carry = crc & 1;
            crc >>= 1;
            if (carry) {
                crc ^= poly;
            }
        }
    }

    return crc;
}


/* Calculate CRC16 for structure type definition
 * Hashes: type name + all member names and types (in order)
 */
static uint16_t calculate_structure_crc(type_def_s *type) {
    /* Build hash data: type name + member info */
    size_t hash_size = strlen(type->name);

    /* Count total size needed */
    member_def_s *member = type->members;
    while (member) {
        hash_size += strlen(member->name) + strlen(member->member_type_string);
        member = member->next_member;
    }

    /* Build hash data */
    uint8_t *hash_data = calloc(hash_size, 1);
    if (!hash_data) {
        pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_ERROR, "Failed to allocate hash_data for CRC calculation");
        return 0;
    }

    size_t offset = 0;

    /* Add type name */
    size_t name_len = strlen(type->name);
    memcpy(hash_data + offset, type->name, name_len);
    offset += name_len;

    /* Add member info */
    member = type->members;
    while (member) {
        /* Add member name */
        name_len = strlen(member->name);
        memcpy(hash_data + offset, member->name, name_len);
        offset += name_len;

        /* Add member type string */
        size_t type_len = strlen(member->member_type_string);
        memcpy(hash_data + offset, member->member_type_string, type_len);
        offset += type_len;

        member = member->next_member;
    }

    /* Calculate CRC */
    uint16_t crc = calculate_crc16(hash_data, offset);

    free(hash_data);
    return crc;
}

void parse_pccc_tag(const char *tag_str, plc_s *plc) {
    tag_def_s *tag = calloc(1, sizeof(*tag));
    char data_file_name[200] = {0};
    char size_str[200] = {0};
    int num_dims = 0;
    size_t start = 0;
    size_t len = 0;

    pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Starting.");

    if(!tag) {
        pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_ERROR, "Unable to allocate memory for new tag!");
        return;
    }

    /* create the tag data mutex */
    if(mutex_create(&(tag->data_mutex)) != MUTEX_STATUS_OK) { pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_ERROR, "Unable to create tag data mutex!"); }

    /* try to match the two parts of a tag definition string. */

    pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Match data file.");

    /* first match the data file. */
    start = 0;
    len = strspn(tag_str + start, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789");
    if(!len) {
        // NOLINTNEXTLINE
        fprintf(stderr, "Unable to parse tag definition string, cannot find tag name in \"%s\"!\n", tag_str);
        usage();
    } else {
        /* copy the string. */
        for(size_t i = 0; i < len && i < (size_t)200; i++) { data_file_name[i] = tag_str[start + i]; }

        /* check data file for a match. */
        if(str_cmp_i(data_file_name, "B3") == 0) {
            pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Found B3 data file.");
            tag->tag_type = TAG_PCCC_TYPE_BIT;
            tag->elem_size = 2;
            tag->data_file_num = 3;
        } else if(str_cmp_i(data_file_name, "N7") == 0) {
            pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Found N7 data file.");
            tag->tag_type = TAG_PCCC_TYPE_INT;
            tag->elem_size = 2;
            tag->data_file_num = 7;
        } else if(str_cmp_i(data_file_name, "F8") == 0) {
            pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Found F8 data file.");
            tag->tag_type = TAG_PCCC_TYPE_REAL;
            tag->elem_size = 4;
            tag->data_file_num = 8;
        } else if(str_cmp_i(data_file_name, "ST18") == 0) {
            pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Found ST18 data file.");
            tag->tag_type = TAG_PCCC_TYPE_STRING;
            tag->elem_size = 84;
            tag->data_file_num = 18;
        } else if(str_cmp_i(data_file_name, "L19") == 0) {
            pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Found L19 data file.");
            tag->tag_type = TAG_PCCC_TYPE_DINT;
            tag->elem_size = 4;
            tag->data_file_num = 19;
        } else {
            // NOLINTNEXTLINE
            fprintf(stderr, "Unknown data file %s, unable to create tag!", data_file_name);
            usage();
        }

        start += len;
    }

    /* get the array size delimiter. */
    if(tag_str[start] != '[') {
        // NOLINTNEXTLINE
        fprintf(stderr, "Unable to parse tag definition string, cannot find starting square bracket after data file in \"%s\"!\n",
                tag_str);
        usage();
    } else {
        start++;
    }

    /* get the size field */
    len = strspn(tag_str + start, "0123456789");
    if(!len) {
        // NOLINTNEXTLINE
        fprintf(stderr, "Unable to parse tag definition string, cannot match array size in \"%s\"!\n", tag_str);
        usage();
    } else {
        /* copy the string. */
        for(size_t i = 0; i < len && i < (size_t)200; i++) { size_str[i] = tag_str[start + i]; }

        start += len;
    }

    if(tag_str[start] != ']') {
        // NOLINTNEXTLINE
        fprintf(stderr, "Unable to parse tag definition string, cannot find ending square bracket after size in \"%s\"!\n",
                tag_str);
        usage();
    }

    /* make sure all the dimensions are defaulted to something sane. */
    tag->dimensions[0] = 1;
    tag->dimensions[1] = 1;
    tag->dimensions[2] = 1;

    /* match the size. */
    // NOLINTNEXTLINE
    num_dims = str_scanf(size_str, "%zu", &tag->dimensions[0]);
    if(num_dims != 1) {
        // NOLINTNEXTLINE
        fprintf(stderr, "Unable to parse tag size in \"%s\"!\n", tag_str);
        usage();
    }

    /* check the size. */
    if(tag->dimensions[0] <= 0) {
        // NOLINTNEXTLINE
        fprintf(stderr, "The array size must least 1 and may not be negative!\n");
        usage();
    } else {
        tag->elem_count = tag->dimensions[0];
        tag->num_dimensions = 1;
    }

    /* copy the tag name */
    tag->name = strdup(data_file_name);
    if(!tag->name) {
        // NOLINTNEXTLINE
        fprintf(stderr, "Unable to allocate a copy of the data file \"%s\"!\n", data_file_name);
        exit(1);
    }

    /* allocate the tag data array. */
    pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "allocating %zu elements of %zu bytes each.", tag->elem_count, tag->elem_size);
    tag->data = calloc(tag->elem_count, (size_t)tag->elem_size);
    if(!tag->data) {
        // NOLINTNEXTLINE
        fprintf(stderr, "Unable to allocate tag data buffer!\n");
        free(tag->name);
        exit(1);
    }

    pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Processed \"%s\" into tag %s of type %x with dimensions (%zu, %zu, %zu).", tag_str, tag->name, tag->tag_type,
         tag->dimensions[0], tag->dimensions[1], tag->dimensions[2]);

    /* add the tag to the list. */
    tag->next_tag = plc->tags;
    plc->tags = tag;
}


/*
 * CIP tags are in the format:
 *    <name>:<type>[<sizes>]
 *
 * Where name is alphanumeric, starting with an alpha character.
 *
 * Type is one of:
 *     INT - 2-byte signed integer.  Requires array size(s).
 *     DINT - 4-byte signed integer.  Requires array size(s).
 *     LINT - 8-byte signed integer.  Requires array size(s).
 *     REAL - 4-byte floating point number.  Requires array size(s).
 *     LREAL - 8-byte floating point number.  Requires array size(s).
 *     STRING - 82-byte string with 4-byte count word and 2 bytes of padding.
 *
 * Array size field is one or more (up to 3) numbers separated by commas.
 */

void parse_cip_tag(const char *tag_str, plc_s *plc) {
    tag_def_s *tag = calloc(1, sizeof(*tag));
    char tag_name[200] = {0};
    char type_str[200] = {0};
    char dim_str[200] = {0};
    int num_dims = 0;
    size_t start = 0;
    size_t len = 0;

    if(!tag) {
        pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_ERROR, "Unable to allocate memory for new tag!");
        return;
    }

    /* create the tag data mutex */
    if(mutex_create(&(tag->data_mutex)) != MUTEX_STATUS_OK) { pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_ERROR, "Unable to create tag data mutex!"); }


    /* create the tag data mutex */
    if(mutex_create(&(tag->data_mutex)) != MUTEX_STATUS_OK) { pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_ERROR, "Unable to create tag data mutex!"); }


    /* try to match the three parts of a tag definition string. */

    /* first match the name. */
    start = 0;
    len = strspn(tag_str + start, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_");
    if(!len) {
        // NOLINTNEXTLINE
        fprintf(stderr, "Unable to parse tag definition string, cannot find tag name in \"%s\"!\n", tag_str);
        usage();
    } else {
        /* copy the string. */
        for(size_t i = 0; i < len && i < (size_t)200; i++) { tag_name[i] = tag_str[start + i]; }

        start += len;
    }

    if(tag_str[start] != ':') {
        // NOLINTNEXTLINE
        fprintf(stderr, "Unable to parse tag definition string, cannot find colon after tag name in \"%s\"!\n", tag_str);
        usage();
    } else {
        start++;
    }

    /* get the type field */
    len = strspn(tag_str + start, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ");
    if(!len) {
        // NOLINTNEXTLINE
        fprintf(stderr, "Unable to parse tag definition string, cannot match tag type in \"%s\"!\n", tag_str);
        usage();
    } else {
        /* copy the string. */
        for(size_t i = 0; i < len && i < (size_t)200; i++) { type_str[i] = tag_str[start + i]; }

        start += len;
    }

    if(tag_str[start] != '[') {
        // NOLINTNEXTLINE
        fprintf(stderr, "Unable to parse tag definition string, cannot find starting square bracket after tag type in \"%s\"!\n",
                tag_str);
        usage();
    } else {
        start++;
    }

    /* get the dimension field */
    len = strspn(tag_str + start, "0123456789,");
    if(!len) {
        // NOLINTNEXTLINE
        fprintf(stderr, "Unable to parse tag definition string, cannot match dimension in \"%s\"!\n", tag_str);
        usage();
    } else {
        /* copy the string. */
        for(size_t i = 0; i < len && i < (size_t)200; i++) { dim_str[i] = tag_str[start + i]; }

        start += len;
    }

    if(tag_str[start] != ']') {
        // NOLINTNEXTLINE
        fprintf(stderr, "Unable to parse tag definition string, cannot find ending square bracket after tag type in \"%s\"!\n",
                tag_str);
        usage();
    }

    /* match the type - try simple types first */
    if (map_type_string_to_cip(type_str, &tag->tag_type, &tag->elem_size)) {
        /* Successfully mapped to a simple type */
        tag->type_def = NULL;
    } else {
        /* Try to find a user-defined structure type (Omron only) */
        if (plc && plc->plc_type == PLC_OMRON) {
            type_def_s *udt = find_type_by_name(plc, type_str);
            if (udt) {
                /* This is a structure type */
                tag->tag_type = TAG_CIP_TYPE_STRUCT;  /* 0x00A0 */
                tag->elem_size = udt->size;
                tag->type_def = udt;
            } else {
                // NOLINTNEXTLINE
                fprintf(stderr, "Unsupported tag type \"%s\" (not a known simple type or defined structure)!\n", type_str);
                usage();
            }
        } else {
            // NOLINTNEXTLINE
            fprintf(stderr, "Unsupported tag type \"%s\" (structure types only supported for Omron PLC)!\n", type_str);
            usage();
        }
    }

    /* match the dimensions. */
    tag->dimensions[0] = 0;
    tag->dimensions[1] = 0;
    tag->dimensions[2] = 0;

    // NOLINTNEXTLINE
    num_dims = str_scanf(dim_str, "%zu,%zu,%zu,%*u", &tag->dimensions[0], &tag->dimensions[1], &tag->dimensions[2]);
    if(num_dims < 1 || num_dims > 3) {
        // NOLINTNEXTLINE
        fprintf(stderr, "Tag dimensions must have at least one dimension non-zero and no more than three dimensions.");
        usage();
    }

    /* check the dimensions. */
    if(tag->dimensions[0] <= 0) {
        // NOLINTNEXTLINE
        fprintf(stderr, "The first tag dimension must be at least 1 and may not be negative!\n");
        usage();
    } else {
        tag->elem_count = tag->dimensions[0];
        tag->num_dimensions = 1;
    }

    if(tag->dimensions[1] > 0) {
        tag->elem_count *= tag->dimensions[1];
        tag->num_dimensions = 2;
    } else {
        tag->dimensions[1] = 1;
    }

    if(tag->dimensions[2] > 0) {
        tag->elem_count *= tag->dimensions[2];
        tag->num_dimensions = 3;
    } else {
        tag->dimensions[2] = 1;
    }

    /* copy the tag name */
    tag->name = strdup(tag_name);
    if(!tag->name) {
        // NOLINTNEXTLINE
        fprintf(stderr, "Unable to allocate a copy of the tag name \"%s\"!\n", tag_name);
        exit(1);
    }

    /* allocate the tag data array. */
    pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "allocating %zu elements of %zu bytes each.", tag->elem_count, tag->elem_size);
    tag->data = calloc(tag->elem_count, (size_t)tag->elem_size);
    if(!tag->data) {
        // NOLINTNEXTLINE
        fprintf(stderr, "Unable to allocate tag data buffer!\n");
        free(tag->name);
        exit(1);
    }

    pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Processed \"%s\" into tag %s of type %x with dimensions (%zu, %zu, %zu).", tag_str, tag->name, tag->tag_type,
         tag->dimensions[0], tag->dimensions[1], tag->dimensions[2]);

    /* assign instance ID for Omron tag enumeration */
    plc->next_instance_id++;
    tag->instance_id = plc->next_instance_id;
    pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Assigned instance ID %u to tag %s", tag->instance_id, tag->name);

    /* add the tag to the list. */
    tag->next_tag = plc->tags;
    plc->tags = tag;
    plc->tag_count++;
}


/* Parse structure definition from command line (PASS 1)
 * Format: TypeName:[field1:type1,field2:type2,...]
 * Example: MyPoint:[x:INT,y:INT]
 *
 * Pass 1: Parse and store structure definition without resolving nested types
 * Pass 2 (later): Resolve nested type references and calculate sizes
 */
void parse_struct_definition(const char *struct_str, plc_s *plc) {
    char type_name[256];
    char field_list[1024];
    type_def_s *type = NULL;
    member_def_s *last_member = NULL;
    uint16_t member_count = 0;

    /* Parse: TypeName:[...] */
    if (sscanf(struct_str, "%255[^:]:[%1023[^]]]", type_name, field_list) != 2) {
        pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_ERROR, "Invalid struct format: %s", struct_str);
        pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_ERROR, "Expected: TypeName:[field1:type1,field2:type2,...]");
        return;
    }

    /* Check for duplicate type name */
    if (find_type_by_name(plc, type_name)) {
        pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_ERROR, "Structure type '%s' already defined", type_name);
        return;
    }

    /* Allocate type definition */
    type = calloc(1, sizeof(type_def_s));
    if (!type) {
        pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_ERROR, "Failed to allocate type_def_s");
        return;
    }

    type->name = strdup(type_name);
    if (!type->name) {
        pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_ERROR, "Failed to allocate type name");
        free(type);
        return;
    }

    /* CRC will be calculated during finalization (Pass 2) */

    /* Parse field list: field1:type1,field2:type2,... */
    char *field_str_copy = strdup(field_list);
    if (!field_str_copy) {
        pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_ERROR, "Failed to allocate field_str_copy");
        free(type->name);
        free(type);
        return;
    }

    char *saveptr = NULL;
    char *token = strtok_r(field_str_copy, ",", &saveptr);

    while (token) {
        char field_name[256];
        char field_type_str[256];

        if (sscanf(token, "%255[^:]:%255s", field_name, field_type_str) != 2) {
            pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_ERROR, "Invalid field format in '%s': %s", type_name, token);
            goto cleanup;
        }

        /* Allocate member */
        member_def_s *member = calloc(1, sizeof(member_def_s));
        if (!member) {
            pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_ERROR, "Failed to allocate member_def_s");
            goto cleanup;
        }

        member->name = strdup(field_name);
        if (!member->name) {
            pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_ERROR, "Failed to allocate member name");
            free(member);
            goto cleanup;
        }

        /* Store type string for later resolution (Pass 2) */
        member->member_type_string = strdup(field_type_str);
        if (!member->member_type_string) {
            pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_ERROR, "Failed to allocate member_type_string");
            free(member->name);
            free(member);
            goto cleanup;
        }

        /* Add to linked list */
        if (last_member) {
            last_member->next_member = member;
        } else {
            type->members = member;
        }
        last_member = member;
        member_count++;

        token = strtok_r(NULL, ",", &saveptr);
    }

    type->member_count = member_count;

    /* Add type to registry (size and offsets will be calculated in Pass 2) */
    type->next_type = plc->types;
    plc->types = type;
    plc->type_count++;

    pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Parsed structure '%s' with %u members (Pass 1 - type resolution deferred)",
             type_name, member_count);

    free(field_str_copy);
    return;

cleanup:
    /* Free partial structure on error */
    if (type) {
        member_def_s *m = type->members;
        while (m) {
            member_def_s *next = m->next_member;
            free(m->name);
            free(m->member_type_string);
            free(m);
            m = next;
        }
        free(type->name);
        free(type);
    }
    free(field_str_copy);
}


/* Calculate structure size recursively with circular reference detection (PASS 2)
 * visited_chain: array of type pointers to detect cycles
 * chain_depth: current depth in the type hierarchy
 */
static size_t calculate_structure_size(type_def_s *type, type_def_s **visited_chain, int chain_depth) {
    const int MAX_DEPTH = 100;

    /* Check for circular reference */
    for (int i = 0; i < chain_depth; i++) {
        if (visited_chain[i] == type) {
            pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_ERROR, "Circular reference detected: type '%s' contains itself (directly or indirectly)", type->name);
            return 0;  /* Error indicator */
        }
    }

    /* Prevent stack overflow */
    if (chain_depth >= MAX_DEPTH) {
        pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_ERROR, "Type nesting too deep (max %d levels)", MAX_DEPTH);
        return 0;
    }

    /* Add to visited chain */
    visited_chain[chain_depth] = type;

    /* Calculate member offsets and structure size */
    size_t current_offset = 0;
    size_t max_alignment = 1;
    member_def_s *member = type->members;

    while (member) {
        /* Calculate member size and alignment */
        if (member->nested_type) {
            /* Nested structure - recursively calculate its size */
            size_t nested_size = calculate_structure_size(member->nested_type, visited_chain, chain_depth + 1);
            if (nested_size == 0) {
                return 0;  /* Error in nested type */
            }
            member->member_size = nested_size;
            member->alignment = get_type_alignment(TAG_CIP_TYPE_STRUCT);  /* Structures align based on largest member */

            /* Recalculate alignment for nested structure */
            member_def_s *nested_member = member->nested_type->members;
            size_t nested_max_align = 1;
            while (nested_member) {
                if (nested_member->alignment > nested_max_align) {
                    nested_max_align = nested_member->alignment;
                }
                nested_member = nested_member->next_member;
            }
            member->alignment = nested_max_align;
        }
        /* else: already set by resolve_member_types() */

        /* Align current offset to member alignment */
        current_offset = align_offset(current_offset, member->alignment);
        member->offset_in_struct = current_offset;
        current_offset += member->member_size;

        /* Track maximum alignment */
        if (member->alignment > max_alignment) {
            max_alignment = member->alignment;
        }

        member = member->next_member;
    }

    /* Calculate total structure size (aligned to max member alignment) */
    type->size = align_offset(current_offset, max_alignment);

    pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Calculated size of type '%s': %zu bytes, max alignment: %zu", type->name, type->size, max_alignment);

    return type->size;
}


/* Resolve member type references (PASS 2)
 * After all structures are parsed, resolve type strings to actual type pointers
 * Returns false if any type reference cannot be resolved
 */
static bool resolve_member_types(plc_s *plc) {
    type_def_s *type = plc->types;
    bool errors = false;

    while (type) {
        member_def_s *member = type->members;
        while (member) {
            /* Try to map to simple type first */
            if (map_type_string_to_cip(member->member_type_string, &member->member_type, &member->member_size)) {
                /* Simple type - no nested structure */
                member->nested_type = NULL;
                member->alignment = get_type_alignment(member->member_type);
            } else {
                /* Try to find user-defined structure type */
                type_def_s *nested = find_type_by_name(plc, member->member_type_string);
                if (nested) {
                    /* Nested structure */
                    member->member_type = TAG_CIP_TYPE_STRUCT;  /* 0x00A0 */
                    member->nested_type = nested;
                    /* Size will be calculated in calculate_structure_size() */
                    /* Alignment will be set based on largest member */
                } else {
                    /* Unknown type */
                    pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_ERROR, "Type '%s' used in member '%s' of struct '%s' is not defined",
                             member->member_type_string, member->name, type->name);
                    errors = true;
                }
            }

            member = member->next_member;
        }

        type = type->next_type;
    }

    return !errors;
}


/* Finalize structure definitions (PASS 2)
 * Resolve nested type references and calculate structure sizes
 * Must be called after all structure definitions are parsed
 */
static void finalize_structure_definitions(plc_s *plc) {
    pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Finalizing structure definitions (Pass 2)...");

    /* Step 1: Resolve all member type references */
    if (!resolve_member_types(plc)) {
        pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_ERROR, "Failed to resolve all member type references");
        return;
    }

    /* Step 2: Calculate sizes with circular reference detection */
    type_def_s *type = plc->types;
    type_def_s **visited_chain = calloc(100, sizeof(type_def_s *));
    if (!visited_chain) {
        pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_ERROR, "Failed to allocate visited_chain for circular reference detection");
        return;
    }

    while (type) {
        memset(visited_chain, 0, 100 * sizeof(type_def_s *));
        size_t size = calculate_structure_size(type, visited_chain, 0);
        if (size == 0) {
            pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_ERROR, "Failed to calculate size for type '%s'", type->name);
        }
        type = type->next_type;
    }

    free(visited_chain);

    /* Step 3: Calculate CRC16 for each type */
    type = plc->types;
    while (type) {
        uint16_t crc = calculate_structure_crc(type);
        type->crc_code = crc;
        pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Calculated CRC16 for type '%s': 0x%04x", type->name, crc);
        type = type->next_type;
    }

    pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Structure definitions finalized successfully");
}


/* Assign instance IDs to structure types and their members
 * Called after all tags and types are parsed
 */
void assign_type_instance_ids(plc_s *plc) {
    /* First, finalize all structure definitions (Pass 2) - Omron only */
    if (plc->types && plc->plc_type == PLC_OMRON) {
        finalize_structure_definitions(plc);
    }

    type_def_s *type = plc->types;

    while (type) {
        /* Assign instance ID to type */
        plc->next_instance_id++;
        type->instance_id = plc->next_instance_id;

        pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "Assigned instance ID %u to type '%s'", type->instance_id, type->name);

        /* Assign instance IDs to members */
        member_def_s *member = type->members;
        while (member) {
            plc->next_instance_id++;
            member->instance_id = plc->next_instance_id;

            pdlog(LOG_MODULE_AB_SERVER, LOG_LEVEL_INFO, "  Assigned instance ID %u to member '%s.%s'",
                     member->instance_id, type->name, member->name);

            member = member->next_member;
        }

        type = type->next_type;
    }
}


/*
 * Process each request.  Dispatch to the correct
 * request type handler.
 */

slice_s request_handler(slice_s input, slice_s output, void *plc_arg) {
    // Remember that we get a copy of the plc_arg/context contents. So values are frozen
    // in time, but references are to a shared resource and must be mutex'ed.
    plc_s *plc = (plc_s *)plc_arg;

    /* check to see if we have a full packet. */
    if(slice_len(input) >= EIP_HEADER_SIZE) {
        uint16_t eip_len = slice_get_uint16_le(input, 2);

        if(slice_len(input) >= (size_t)(EIP_HEADER_SIZE + eip_len)) {
            slice_s resp = eip_dispatch_request(input, output, plc);

            /* if there is a response delay requested, then wait a bit. */
            if(plc->response_delay > 0) { util_sleep_ms(plc->response_delay); }

            return resp;
        }
    }

    /* we do not have a complete packet, get more data. */
    return slice_make_err(TCP_SERVER_INCOMPLETE);
}
