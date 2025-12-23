#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "../tests/utils/buf3.h"

/* Constants */
#define EIP_CMD_SEND_RR_DATA 0x006F
#define EIP_HEADER_SIZE 24
#define CPF_ITEM_COUNT 2
#define CPF_NULL_ADDR 0x0000
#define CPF_UNCONNECTED_MSG 0x00B2

/*
 * Layer 3: CIP (Application Layer)
 * Writes the actual command data into the buffer.
 * This layer doesn't know about EIP or CPF headers.
 */
void encode_cip_get_attribute_single(slice_t *resp_buf, uint16_t class_id, uint16_t instance_id, uint16_t attr_id) {
    /* Service: Get Attribute Single */
    slice_write_u8(resp_buf, 0x0E);

    /* Request Path Size (words) */
    slice_write_u8(resp_buf, 0x03);

    /* Request Path: Class, Instance, Attribute */
    slice_write_u8(resp_buf, 0x20); /* Class segment */
    slice_write_u8(resp_buf, (uint8_t)class_id);
    slice_write_u8(resp_buf, 0x24); /* Instance segment */
    slice_write_u8(resp_buf, (uint8_t)instance_id);
    slice_write_u8(resp_buf, 0x30); /* Attribute segment */
    slice_write_u8(resp_buf, (uint8_t)attr_id);
}

/*
 * Layer 2: CPF (Encapsulation Layer)
 * Wraps the CIP message in a Common Packet Format structure.
 * Reserves space for its headers, calls the upper layer, then fills headers.
 */
void encode_cpf_message(slice_t *buf) {
    /* 1. Reserve space for CPF Item Count (2 bytes) */
    slice_t count_view = slice_reserve(buf, 2);

    /* 2. Reserve space for Address Item (Type + Length + Value) */
    /* Null Address is Type(2) + Len(2) + Value(0) = 4 bytes */
    slice_t addr_view = slice_reserve(buf, 4);

    /* 3. Reserve space for Data Item Header (Type + Length) */
    slice_t data_hdr_view = slice_reserve(buf, 4);

    /* 4. Encode Payload (CIP) into the remaining buffer */
    /* Capture cursor to calculate CIP payload length later */
    size_t cip_start = buf->cursor;

    encode_cip_get_attribute_single(buf, 0x01, 0x01, 0x01);

    size_t cip_len = buf->cursor - cip_start;

    /* 5. Fill in Headers (Back-to-Front logic relative to stack return) */

    /* Fill Data Item Header */
    slice_write_u16_le(&data_hdr_view, CPF_UNCONNECTED_MSG);
    slice_write_u16_le(&data_hdr_view, (uint16_t)cip_len);

    /* Fill Address Item (Null Address) */
    slice_write_u16_le(&addr_view, CPF_NULL_ADDR);
    slice_write_u16_le(&addr_view, 0);

    /* Fill Item Count */
    slice_write_u16_le(&count_view, CPF_ITEM_COUNT);
}

/*
 * Layer 1: EtherNet/IP (Session Layer)
 * Wraps the CPF message in an EIP Encapsulation Header.
 */
void encode_eip_send_rr_data(slice_t *buf, uint32_t session_handle) {
    /* 1. Reserve space for EIP Header (24 bytes) */
    slice_t header_view = slice_reserve(buf, EIP_HEADER_SIZE);

    /* 2. Reserve space for Command Specific Data (Interface Handle + Timeout) */
    slice_t cmd_data_view = slice_reserve(buf, 6);

    /* 3. Encode Payload (CPF) */
    size_t cpf_start = buf->cursor;
    encode_cpf_message(buf);
    size_t cpf_len = buf->cursor - cpf_start;

    /* 4. Fill Command Specific Data */
    slice_write_u32_le(&cmd_data_view, 0); /* Interface Handle */
    slice_write_u16_le(&cmd_data_view, 0); /* Timeout */

    /* 5. Fill EIP Header */
    slice_write_u16_le(&header_view, EIP_CMD_SEND_RR_DATA);
    slice_write_u16_le(&header_view, (uint16_t)(6 + cpf_len)); /* Length of data following header */
    slice_write_u32_le(&header_view, session_handle);
    slice_write_u32_le(&header_view, 0); /* Status */
    slice_write_u64_le(&header_view, 0); /* Sender Context */
    slice_write_u32_le(&header_view, 0); /* Options */
}

int main(void) {
    uint8_t raw_buffer[1024];
    slice_t packet = slice_init(raw_buffer, sizeof(raw_buffer));

    printf("Constructing EtherNet/IP packet...\n");
    encode_eip_send_rr_data(&packet, 0x12345678);

    if(slice_get_error(&packet) != UTIL_OK) {
        printf("Error constructing packet!\n");
        return 1;
    }

    printf("Packet constructed successfully. Total length: %zu bytes\n", packet.cursor);

    /* Hex dump */
    for(size_t i = 0; i < packet.cursor; i++) {
        printf("%02X ", raw_buffer[i]);
        if((i + 1) % 16 == 0) { printf("\n"); }
    }
    printf("\n");

    return 0;
}