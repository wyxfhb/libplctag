#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "buf.h"
#include "err.h"

/* Forward declarations */
typedef struct plc_context_s plc_context_t;

/* ============================================================================
 * CPF (Common Packet Format) Constants
 * ============================================================================ */

/* CPF Item Type IDs */
#define CPF_ITEM_NULL_ADDRESS       0x0000  /* Null Address */
#define CPF_ITEM_CONNECTED_ADDRESS  0x00A1  /* Connected Address Item */
#define CPF_ITEM_CONNECTED_DATA     0x00B1  /* Connected Data Item */
#define CPF_ITEM_UNCONNECTED_DATA   0x00B2  /* Unconnected Data Item (most common) */

/* ============================================================================
 * CPF Data Structures
 * ============================================================================ */

/**
 * CPF Item
 *
 * Represents one item in a CPF packet.
 * Data is stored as pointer + length, not copied.
 */
typedef struct {
    uint16_t type_id;               /* Item type (CPF_ITEM_*) */
    uint16_t length;                /* Length of data */
    const uint8_t *data;            /* Pointer to item data (in input buffer) */
} cpf_item_t;

/**
 * CPF Packet
 *
 * Represents a parsed CPF packet with its items.
 */
typedef struct {
    uint16_t item_count;            /* Number of items in packet */
    cpf_item_t items[8];            /* Item array (max 8 items) */
} cpf_packet_t;

/* ============================================================================
 * CPF Protocol Functions
 * ============================================================================ */

/**
 * @brief Parse CPF packet from buffer
 *
 * Reads CPF item count and item headers, storing pointers to data.
 * Does NOT copy item data, only stores references.
 *
 * @param input Buffer positioned at start of CPF (after EIP header)
 * @param packet OUT: Parsed CPF packet structure
 * @return UTIL_OK on success, error code on failure
 */
util_err_t cpf_parse_packet(buf_t *input, cpf_packet_t *packet);

/**
 * @brief Dispatch CPF packet through CIP router
 *
 * Extracts CIP message from unconnected data item and routes through
 * the CIP message router. Builds response wrapped in CPF format.
 *
 * @param packet Parsed CPF packet
 * @param input Buffer positioned after CPF header (for CIP parsing)
 * @param output Buffer to write CPF response to
 * @param plc PLC context (passed to CIP router)
 * @return UTIL_OK on success, error code on failure
 */
util_err_t cpf_dispatch(const cpf_packet_t *packet, buf_t *input,
                       buf_t *output, plc_context_t *plc);
