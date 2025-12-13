#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "coro_net.h"
#include "protocol/cip_object_registry.h"

/* Forward declarations */
typedef struct tag_def_s tag_def_t;
typedef struct omron_registry_s omron_registry_t;

/* Log module for PLC server (will be defined in log_modules.def) */
#define LOG_MODULE_PLC_SERVER (1ULL << 14)
#define LOG_MODULE_PLC_CLIENT (1ULL << 15)
#define LOG_MODULE_EIP_PROTOCOL (1ULL << 16)
#define LOG_MODULE_CPF_PROTOCOL (1ULL << 17)
#define LOG_MODULE_CIP_ROUTER (1ULL << 18)
#define LOG_MODULE_SYMBOL_OBJECT (1ULL << 19)
#define LOG_MODULE_IDENTITY_OBJECT (1ULL << 20)
#define LOG_MODULE_OMRON_TAG_NAME_SERVER (1ULL << 21)
#define LOG_MODULE_OMRON_VARIABLE_OBJECT (1ULL << 22)
#define LOG_MODULE_OMRON_TYPE_OBJECT (1ULL << 23)

/**
 * PLC Server Context
 *
 * Represents the running PLC server and its state.
 */
typedef struct plc_context_s {
    coro_net_t *coro_net;           /* Event loop */
    volatile int running;           /* Shutdown flag */
    int64_t start_time_us;          /* Server start time (for uptime) */

    cip_object_registry_t *registry;/* CIP object registry */
    tag_def_t *tags;                /* Tag storage (linked list) */
    omron_registry_t *omron_registry;/* Omron variable/type registry (Phase 6) */

    /* Will be added in later phases: */
    // server_stats_t stats;             /* Performance statistics */
} plc_context_t;

/**
 * Listener Information
 *
 * Context passed to listener_handler coroutine.
 */
typedef struct listener_info_s {
    plc_context_t *plc;             /* Server context */
    coro_task_handle_t handle;      /* Coroutine handle for cleanup */
    char bind_address[64];          /* Bind address string */
    uint16_t bind_port;             /* Bind port */
} listener_info_t;

/**
 * EIP Session
 *
 * Tracks session state for each client.
 */
typedef struct {
    uint32_t session_handle;        /* Session handle assigned by server */
    bool registered;                /* True after successful RegisterSession */
} eip_session_t;

/**
 * Client Connection Context
 *
 * Per-connection state maintained across coroutine yields.
 * Expanded in later phases.
 */
typedef struct client_context_s {
    coro_task_handle_t handle;      /* Task handle for cleanup */
    plc_context_t *plc;             /* Server context reference */
    eip_session_t session;          /* EIP session state (Phase 2) */

    /* I/O Buffers */
    uint8_t recv_buffer[4096];      /* Receive buffer (static allocation) */
    uint8_t send_buffer[4096];      /* Send buffer (static allocation) */
    struct buf_s recv_buf;          /* Receive buffer wrapper */
    struct buf_s send_buf;          /* Send buffer wrapper */

    /* Will be added in later phases: */
    // request_timing_t timing;        /* Request timing (for stats) */
} client_context_t;
