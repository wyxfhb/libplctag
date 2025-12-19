#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "../utils/buf.h"
#include "../utils/coro_net.h"
#include "protocol/cip_object_registry.h"
#include "protocol/eip_protocol.h"

/* Forward declarations */
typedef struct tag_def_s tag_def_t;
typedef struct udt_def_s udt_def_t;

/* Log module for PLC server (will be defined in log_modules.def) */
// #define LOG_MODULE_PLC_SERVER (1ULL << 14)
// #define LOG_MODULE_PLC_CLIENT (1ULL << 15)
// #define LOG_MODULE_EIP_PROTOCOL (1ULL << 16)
// #define LOG_MODULE_CPF_PROTOCOL (1ULL << 17)
// #define LOG_MODULE_CIP_ROUTER (1ULL << 18)
// #define LOG_MODULE_SYMBOL_OBJECT (1ULL << 19)
// #define LOG_MODULE_IDENTITY_OBJECT (1ULL << 20)
// #define LOG_MODULE_OMRON_TAG_NAME_SERVER (1ULL << 21)
// #define LOG_MODULE_OMRON_VARIABLE_OBJECT (1ULL << 22)
// #define LOG_MODULE_OMRON_TYPE_OBJECT (1ULL << 23)

/**
 * PLC Type Enumeration
 */
typedef enum {
    PLC_TYPE_MICRO800 = 0,
    PLC_TYPE_CONTROLLOGIX = 1,
    PLC_TYPE_PLC5 = 2,
    PLC_TYPE_SLC = 3,
    PLC_TYPE_OMRON = 4,
} plc_type_t;

/**
 * PLC Server Context
 *
 * Represents the running PLC server and its state.
 */
typedef struct plc_context_s {
    coro_net_t *coro_net;  /* Event loop */
    volatile int running;  /* Shutdown flag */
    int64_t start_time_us; /* Server start time (for uptime) */

    plc_type_t plc_type;                /* PLC type for this server */
    cip_object_registry_t *registry;  /* CIP object registry */
    tag_def_t *tags;                  /* Tag storage (linked list) - used by all PLC types */
    udt_def_t *udts;                  /* UDT definitions (linked list) - used by all PLC types */

    /* Connection Manager state (will be moved to client_context in full refactoring) */
    uint32_t client_connection_id;        /* Client connection ID */
    uint32_t server_connection_id;        /* Server connection ID */
    uint16_t client_connection_serial;    /* Client connection serial number */
    uint16_t client_vendor_id;            /* Client vendor ID */
    uint32_t client_serial_number;        /* Client serial number */
    uint32_t client_to_server_rpi;        /* Requested RPI */
    uint32_t server_to_client_rpi;        /* Requested RPI */
    uint16_t client_to_server_max_packet; /* Max packet size */
    uint16_t server_to_client_max_packet; /* Max packet size */
    bool is_forward_open;                 /* Forward Open is active */

    /* PLC Path Configuration (for ControlLogix and other PLCs requiring paths) */
    uint8_t path[32];                     /* Device path (backplane/slot in CIP format) */
    size_t path_len;                      /* Length of path in bytes */

    /* Will be added in later phases: */
    // server_stats_t stats;             /* Performance statistics */
} plc_context_t;

/**
 * Listener Information
 *
 * Context passed to listener_handler coroutine.
 */
typedef struct listener_info_s {
    plc_context_t *plc;        /* Server context */
    coro_task_handle_t handle; /* Coroutine handle for cleanup */
    char bind_address[64];     /* Bind address string */
    uint16_t bind_port;        /* Bind port */
} listener_info_t;


/**
 * Client Connection Context
 *
 * Per-connection state maintained across coroutine yields.
 */
typedef struct client_context_s {
    coro_task_handle_t handle; /* Task handle for cleanup */
    plc_context_t *plc;        /* Server context reference */
    eip_session_t session;     /* EIP session state */

    /* I/O Buffers */
    uint8_t recv_buffer[8192]; /* Receive buffer (static allocation) - sized for ControlLogix 4000+ byte packets */
    uint8_t send_buffer[8192]; /* Send buffer (static allocation) - sized for ControlLogix 4000+ byte packets */
    struct buf_s recv_buf;     /* Receive buffer wrapper */
    struct buf_s send_buf;     /* Send buffer wrapper */

    /* Connection Manager state (per-client) */
    uint32_t client_connection_id;        /* Client connection ID */
    uint32_t server_connection_id;        /* Server connection ID */
    uint16_t client_connection_serial;    /* Client connection serial number */
    uint16_t client_vendor_id;            /* Client vendor ID */
    uint32_t client_serial_number;        /* Client serial number */
    uint32_t client_to_server_rpi;        /* Requested RPI */
    uint32_t server_to_client_rpi;        /* Requested RPI */
    uint16_t client_to_server_max_packet; /* Max packet size */
    uint16_t server_to_client_max_packet; /* Max packet size */
    bool is_forward_open;                 /* Forward Open is active */

    /* Will be added in later phases: */
    // request_timing_t timing;        /* Request timing (for stats) */
} client_context_t;
