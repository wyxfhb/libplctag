#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "../utils/buf.h"
#include "../utils/coro_net.h"
#include "protocol/cip_object_registry.h"
#include "protocol/eip_protocol.h"

/* Forward declarations */
typedef struct tag_def_s tag_def_t;
typedef struct udt_entry_s udt_entry_t;


/**
 * PLC Type Enumeration
 */
typedef enum {
    PLC_TYPE_MICRO800 = 0,
    PLC_TYPE_CONTROLLOGIX = 1,
    PLC_TYPE_PLC5 = 2,
    PLC_TYPE_SLC = 3,
    PLC_TYPE_OMRON = 4,
    PLC_TYPE_MICROLOGIX = 5,
} plc_type_t;

/* forward declarations */
typedef struct plc_context_s plc_context_t;
typedef struct client_context_s client_context_t;
typedef struct cip_object_registry_s cip_object_registry_t;


/**
 * PLC Server Context
 *
 * Represents the running PLC server and its state.
 */
typedef struct plc_context_s {
    coro_net_t *coro_net;  /* Event loop */
    volatile int running;  /* Shutdown flag */
    int64_t start_time_us; /* Server start time (for uptime) */

    plc_type_t plc_type;             /* PLC type for this server */
    cip_object_registry_t *registry; /* CIP object registry */

    /* Tag storage (array-based, O(1) lookups) - used by all PLC types */
    tag_def_t **tags;    /* Array of tag pointers */
    size_t tag_count;    /* Number of tags */
    size_t tag_capacity; /* Allocated capacity (for resizing) */

    /* UDT storage (unified array for UDT definitions and their fields) - used by all PLC types */
    udt_entry_t *udts;   /* Flat array containing both UDT definitions and field entries */
    size_t udt_count;    /* Total number of entries (UDT definitions + fields) */
    size_t udt_capacity; /* Allocated capacity (for resizing) */

    /* EIP encapsulation state support, the actual session handle is in the client.  */
    uint32_t next_session; /* Next session handle to assign */

    // /* Connection Manager state (will be moved to client_context in full refactoring) */
    // uint32_t client_connection_id;        /* Client connection ID */
    // uint32_t server_connection_id;        /* Server connection ID */
    // uint16_t client_connection_serial;    /* Client connection serial number */
    // uint16_t client_vendor_id;            /* Client vendor ID */
    // uint32_t client_serial_number;        /* Client serial number */
    // uint32_t client_to_server_rpi;        /* Requested RPI */
    // uint32_t server_to_client_rpi;        /* Requested RPI */
    // uint16_t client_to_server_max_packet; /* Max packet size */
    // uint16_t server_to_client_max_packet; /* Max packet size */
    // bool is_forward_open;                 /* Forward Open is active */

    /* PLC Path Configuration (for ControlLogix and other PLCs requiring paths) */
    uint8_t path[32]; /* Device path (backplane/slot in CIP format) */
    size_t path_len;  /* Length of path in bytes */

    /* Testing/debugging features */
    uint32_t response_delay_ms; /* Delay all responses by N milliseconds (0 = disabled) */
    uint32_t reject_fo_count;   /* Remaining ForwardOpen failures (0 = disabled) */

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

    /* I/O Buffers */
    uint8_t recv_buffer[8192]; /* Receive buffer (static allocation) - sized for Omron 8000+ byte packets */
    uint8_t send_buffer[8192]; /* Send buffer (static allocation) - sized for Omron 8000+ byte packets */
    struct buf_s recv_buf;     /* Receive buffer wrapper */
    struct buf_s send_buf;     /* Send buffer wrapper */

    /* eip encapsulation state (per-client) */
    uint32_t session_handle; /* Session handle */

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
    uint32_t c2s_connection_size;         /* Client to server connection size */
    uint32_t s2c_connection_size;         /* Server to client connection size */
    bool is_forward_open;                 /* Forward Open is active */

    /* Delay state for non-blocking response delay */
    int64_t delay_start_us; /* Timestamp when delay started (microseconds) */
    bool delay_active;      /* Is delay in progress? */

    /* local FO rejection count */
    uint32_t reject_fo_count; /* Remaining ForwardOpen failures (0 = disabled) */


    /* Will be added in later phases: */
    // request_timing_t timing;        /* Request timing (for stats) */
} client_context_t;
