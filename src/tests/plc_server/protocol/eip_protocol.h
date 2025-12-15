#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "../../utils/buf.h"
#include "../../utils/err.h"

/* Forward declarations */
typedef struct plc_context_s plc_context_t;
typedef struct client_context_s client_context_t;

/* ============================================================================
 * EIP Constants
 * ============================================================================ */

#define EIP_HEADER_SIZE 24

/* EIP Command Codes (Appendix B from DESIGN.md) */
#define EIP_CMD_NOP 0x0000
#define EIP_CMD_LIST_SERVICES 0x0004
#define EIP_CMD_LIST_IDENTITY 0x0063
#define EIP_CMD_LIST_INTERFACES 0x0064
#define EIP_CMD_REGISTER_SESSION 0x0065
#define EIP_CMD_UNREGISTER_SESSION 0x0066
#define EIP_CMD_SEND_RR_DATA 0x006F   /* Unconnected */
#define EIP_CMD_SEND_UNIT_DATA 0x0070 /* Connected */

/* ============================================================================
 * EIP Data Structures
 * ============================================================================ */

/**
 * EIP Encapsulation Header (24 bytes fixed)
 *
 * Layout (all little-endian):
 * [0-1]   uint16_le  Command (EIP_CMD_*)
 * [2-3]   uint16_le  Length (of data following header)
 * [4-7]   uint32_le  Session Handle
 * [8-11]  uint32_le  Status
 * [12-19] uint64_le  Sender Context (echo back in response)
 * [20-23] uint32_le  Options (usually 0)
 */
typedef struct {
    uint16_t command;        /* EIP command code */
    uint16_t length;         /* Length of data after header */
    uint32_t session_handle; /* Session identifier */
    uint32_t status;         /* Response status code */
    uint64_t sender_context; /* Echo back in response */
    uint32_t options;        /* Command-specific flags */
} eip_header_t;

/**
 * EIP Session Context
 *
 * Tracks session state for a single client connection.
 */
typedef struct {
    uint32_t session_handle; /* Session handle assigned by server */
    bool registered;         /* True after successful RegisterSession */
} eip_session_t;

/* ============================================================================
 * EIP Protocol Functions
 * ============================================================================ */

/**
 * @brief Frame check callback for socket_read_yield
 *
 * Checks if a complete EIP packet is available in the buffer.
 *
 * @param buf Buffer to check
 * @param context Unused (NULL)
 * @return UTIL_OK if complete frame available, UTIL_EAGAIN if need more data,
 *         error code on invalid frame
 */
util_err_t eip_frame_check(buf_t *buf, void *context);

/**
 * @brief Parse EIP header from buffer
 *
 * Reads and validates 24-byte EIP encapsulation header.
 * Advances read cursor by 24 bytes on success.
 *
 * @param input Buffer positioned at start of EIP header
 * @param header OUT: Parsed header structure
 * @return UTIL_OK on success, UTIL_EINVAL on parse error,
 *         UTIL_EAGAIN if buffer too small
 */
util_err_t eip_parse_header(buf_t *input, eip_header_t *header);

/**
 * @brief Build EIP response header
 *
 * Creates a 24-byte EIP response header, echoing relevant fields
 * from the request and setting status and data length.
 *
 * @param output Buffer to write header to
 * @param req Request header (to echo command, session handle, context)
 * @param status EIP status code to return
 * @param data_length Length of data following this header
 * @return UTIL_OK on success, UTIL_EBOUNDS if buffer too small
 */
util_err_t eip_build_response_header(buf_t *output, const eip_header_t *req, uint32_t status, uint16_t data_length);

/**
 * @brief Main EIP dispatcher
 *
 * Routes EIP commands to appropriate handlers.
 * Parses request header, dispatches by command code, builds response.
 *
 * @param input Buffer positioned at start of EIP packet (will advance read cursor)
 * @param output Buffer to write response to (cleared before use)
 * @param session EIP session state (may be updated)
 * @param plc PLC context (passed to command handlers)
 * @return UTIL_OK on success, error code on failure
 */
util_err_t eip_dispatch(buf_t *input, buf_t *output, eip_session_t *session, plc_context_t *plc);
