#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "../plc_context.h"
#include "../../utils/buf.h"
#include "../../utils/err.h"


/* ============================================================================
 * CPF Protocol Functions
 * ============================================================================ */

/**
 * @brief Dispatch CPF packet through CIP router
 *
 * Routes CPF packets to appropriate handler based on item types:
 * - Unconnected messages: Extracts CIP message from unconnected data item (0x00B2)
 * - Connected messages: Extracts CIP message from connected data item (0x00B1),
 *   validates connection ID against server_connection_id, and echoes sequence number
 *
 * Builds response wrapped in CPF format with appropriate item types.
 *
 * @param input Buffer positioned after EIP encapsulation header.
 * @param output Buffer to write CPF response to.  Write position after EIP header.
 * @param client Client context (passed to CIP router)
 * @return UTIL_OK on success, error code on failure
 */
util_err_t cpf_dispatch(buf_t *input, buf_t *output, client_context_t *client);
