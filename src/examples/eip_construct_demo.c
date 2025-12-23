#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "../tests/utils/buf_view.h"

/* Constants for EtherNet/IP */
#define EIP_CMD_SEND_RR_DATA 0x006F
#define EIP_HEADER_SIZE 24
#define EIP_CPF_ITEM_COUNT 2
#define EIP_CPF_NULL_ADDR 0x0000
#define EIP_CPF_UNCONNECTED_MSG 0x00B2

/**
 * @brief Constructs an EIP packet with a nested CIP payload.
 *
 * Structure:
 * [ EIP Header (24 bytes) ]
 * [ Interface Handle (4 bytes) ]
 * [ Timeout (2 bytes) ]
 * [ CPF Item Count (2 bytes) ]
 * [ CPF Item 1: Address (4 bytes) ]
 * [ CPF Item 2: Data Header (4 bytes) ]
 * [ CIP Payload (Variable) ]
 */
void construct_eip_packet(uint8_t *buffer, size_t capacity, uint32_t session_handle) {
    /* 1. Initialize the main view covering the raw buffer */
    buf_view_t packet = buf_view_init(buffer, capacity);

    /* 2. Reserve space for the EIP Encapsulation Header */
    /* 'eip_hdr' is now a window into the first 24 bytes. */
    /* 'packet' cursor advances past these 24 bytes. */
    buf_view_t eip_hdr = buf_view_slice(&packet, EIP_HEADER_SIZE);

    /* 3. Write Command Specific Data (Interface Handle + Timeout) */
    /* These are written directly to 'packet' which is now positioned after the header */
    buf_view_write_u32_le(&packet, 0);  // Interface Handle (0)
    buf_view_write_u16_le(&packet, 0);  // Timeout (0)

    /* 4. Write CPF (Common Packet Format) Start */
    buf_view_write_u16_le(&packet, EIP_CPF_ITEM_COUNT);  // Item Count

    /* 5. CPF Item 1: Null Address Item */
    buf_view_write_u16_le(&packet, EIP_CPF_NULL_ADDR);  // Type
    buf_view_write_u16_le(&packet, 0);                  // Length (0)

    /* 6. CPF Item 2: Unconnected Data Item */
    buf_view_write_u16_le(&packet, EIP_CPF_UNCONNECTED_MSG);  // Type

    /* Reserve space for the Data Item Length field (2 bytes) */
    /* We will fill this in later once we know the CIP payload size */
    buf_view_t data_len_view = buf_view_slice(&packet, 2);

    /* 7. Write the CIP Payload */
    /* Capture the start position of the payload relative to the packet start */
    size_t payload_start = buf_view_pos(&packet);

    /* Simulate writing a CIP service request (e.g., Get Attribute Single) */
    buf_view_write_u8(&packet, 0x0E);  // Service: Get Attribute Single
    buf_view_write_u8(&packet, 0x03);  // Path Size (words)
    buf_view_write_u8(&packet, 0x20);  // Class Segment
    buf_view_write_u8(&packet, 0x01);  // Class ID (Identity)
    buf_view_write_u8(&packet, 0x24);  // Instance Segment
    buf_view_write_u8(&packet, 0x01);  // Instance ID (1)
    buf_view_write_u8(&packet, 0x30);  // Attribute Segment
    buf_view_write_u8(&packet, 0x01);  // Attribute ID (Vendor)

    /* Calculate payload size */
    size_t payload_end = buf_view_pos(&packet);
    size_t payload_size = payload_end - payload_start;

    /* 8. Backfill the Data Item Length */
    /* We write into the reserved slice 'data_len_view' */
    buf_view_write_u16_le(&data_len_view, (uint16_t)payload_size);

    /* 9. Fill the EIP Encapsulation Header */
    /* We write into the reserved slice 'eip_hdr' */

    /* Calculate EIP Encapsulation Length: Total bytes written minus header size */
    /* buf_view_pos(&packet) returns the total bytes consumed in the main view */
    uint16_t encap_length = (uint16_t)(buf_view_pos(&packet) - EIP_HEADER_SIZE);

    buf_view_write_u16_le(&eip_hdr, EIP_CMD_SEND_RR_DATA);  // Command
    buf_view_write_u16_le(&eip_hdr, encap_length);          // Length
    buf_view_write_u32_le(&eip_hdr, session_handle);        // Session Handle
    buf_view_write_u32_le(&eip_hdr, 0);                     // Status
    buf_view_write_u64_le(&eip_hdr, 0);                     // Sender Context
    buf_view_write_u32_le(&eip_hdr, 0);                     // Options

    /* Final Check */
    if(buf_view_get_error(&packet) != UTIL_OK) {
        printf("Error constructing packet: %s\n", util_err_str(buf_view_get_error(&packet)));
    } else {
        printf("Packet constructed successfully. Total size: %zu bytes\n", buf_view_pos(&packet));
        printf("EIP Encap Length Field: %u\n", encap_length);
        printf("CPF Data Item Length Field: %zu\n", payload_size);
    }
}

int main(void) {
    uint8_t buffer[1024];
    construct_eip_packet(buffer, sizeof(buffer), 0x12345678);
    return 0;
}