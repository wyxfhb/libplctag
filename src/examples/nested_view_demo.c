#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "../tests/utils/buf_view.h"

/* Mock protocol constants */
#define HEADER_SIZE 4

/**
 * @brief Function that fills the payload.
 *
 * It doesn't need to know about the header or the absolute position.
 * It just writes to the view it is given.
 */
void fill_payload_data(buf_view_t *v) {
    const char *msg = "Hello, World!";
    buf_view_write_bytes(v, (const uint8_t *)msg, strlen(msg));

    /* Add some more data */
    buf_view_write_u8(v, 0); /* Null terminator */
}

/**
 * @brief Constructs a packet with a header and payload.
 */
void construct_packet_with_subfunction(uint8_t *buffer, size_t capacity) {
    /* 1. Initialize the main view */
    buf_view_t packet = buf_view_init(buffer, capacity);

    /* 2. Reserve space for the Header */
    /* This creates a view 'hdr' for the first 4 bytes */
    /* AND advances 'packet' cursor past those 4 bytes. */
    buf_view_t hdr = buf_view_slice(&packet, HEADER_SIZE);

    /* 3. Capture the state before calling the sub-function */
    size_t payload_start_pos = buf_view_pos(&packet);

    /* 4. Pass the view to the sub-function to fill the payload */
    /* The sub-function writes starting at the current cursor of 'packet' */
    fill_payload_data(&packet);

    /* 5. Calculate the payload size */
    size_t payload_end_pos = buf_view_pos(&packet);
    size_t payload_len = payload_end_pos - payload_start_pos;

    /* 6. Fill in the header with the calculated length */
    /* We write to the 'hdr' view we reserved earlier */
    printf("Filling header with payload length: %zu\n", payload_len);
    buf_view_write_u32_le(&hdr, (uint32_t)payload_len);

    /* Check for errors */
    if(buf_view_get_error(&packet) != UTIL_OK) {
        printf("Error constructing packet: %s\n", util_err_str(buf_view_get_error(&packet)));
    } else {
        printf("Packet constructed successfully. Total size: %zu\n", buf_view_pos(&packet));
    }
}

int main(void) {
    uint8_t buffer[128];
    construct_packet_with_subfunction(buffer, sizeof(buffer));
    return 0;
}