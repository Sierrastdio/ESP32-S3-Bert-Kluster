#pragma once
#include <stdint.h>

#define PROTO_MAGIC 0x31504342u /* BCP1 */
#define PROTO_VERSION 1u
#define MSG_CONFIG 1u
#define MSG_TOKENS 2u
#define DTYPE_FP16 1u

typedef struct __attribute__((packed)) {
  uint32_t magic;
  uint8_t version, type;
  uint16_t sentence_id, token_count;
  uint32_t data_packet_bytes;
  uint8_t dtype, reserved;
  uint32_t crc32;
} config_message_t;

typedef struct __attribute__((packed)) {
  uint32_t magic;
  uint8_t version, type;
  uint16_t sentence_id, token_count;
  uint32_t crc32;
  // uint16_t token_ids[token_count] follows
} tokens_header_t;

uint32_t protocol_crc32(const void *data, uint32_t length);
