#pragma once

#include <stdint.h>
#include "../../utils/buf.h"
#include "../../utils/err.h"

/* Forward declarations */
typedef struct plc_context_s plc_context_t;

/* ============================================================================
 * CIP Message Router Constants
 * ============================================================================ */

/* CIP Status Codes (Appendix A from DESIGN.md) */
#define CIP_STATUS_OK 0x00
#define CIP_STATUS_INVALID_PARAM 0x03
#define CIP_STATUS_PATH_DEST_UNKNOWN 0x05
#define CIP_STATUS_PARTIAL_TRANSFER 0x06
#define CIP_STATUS_SERVICE_NOT_SUPPORTED 0x08
#define CIP_STATUS_INVALID_ATTRIBUTE 0x09
#define CIP_STATUS_TOO_MUCH_DATA 0x15
#define CIP_STATUS_SERVICE_ERROR 0x1E

/* ============================================================================
 * CIP Data Structures
 * ============================================================================ */

/**
 * CIP Request
 *
 * Parsed CIP service request header.
 */
typedef struct {
    uint8_t service;   /* Service code (0x4C, 0x4D, etc.) */
    uint8_t path_size; /* Path size in words */
    /* Path data follows in buffer */
} cip_request_t;

/* ============================================================================
 * CIP Message Router Functions
 * ============================================================================ */

/**
 * @brief Parse CIP request header from buffer
 *
 * Reads service code and path size.
 * Advances buffer read position accordingly.
 *
 * @param input Buffer positioned at start of CIP message
 * @param request OUT: Parsed request structure
 * @return UTIL_OK on success, error code on failure
 */
util_err_t cip_parse_request(buf_t *input, cip_request_t *request);

/**
 * @brief Main CIP Message Router dispatcher
 *
 * Parses CIP request, routes to target object class,
 * builds response with service reply + status.
 *
 * @param input Buffer positioned at start of CIP message
 * @param output Buffer to write CIP response to
 * @param plc PLC context (for object registry, tags, etc.)
 * @return UTIL_OK on success, error code on failure
 */
util_err_t cip_message_router_dispatch(buf_t *input, buf_t *output, plc_context_t *plc);

/**
 * @brief Build CIP response header
 *
 * Writes reply service code and status to response buffer.
 *
 * @param output Buffer to write response to
 * @param service Original service code
 * @param status CIP status code
 * @return UTIL_OK on success, error code on failure
 */
util_err_t cip_build_response(buf_t *output, uint8_t service, uint8_t status);
