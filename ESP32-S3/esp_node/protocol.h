#pragma once
#include <stdint.h>

// Pico 1과 ESP 노드가 공유하는 바이너리 통신 프로토콜 정의 (BCP1)
// 모든 정수는 little-endian입니다.

#define PROTO_MAGIC   0x31504342u /* 'BCP1' */
#define PROTO_VERSION 1u

#define MSG_CONFIG    1u // Pico -> 전 노드 (UART 브로드캐스트)
#define MSG_TOKENS    2u // Pico -> ESP1 (SPI)
#define MSG_RESULT    3u // ESP4 -> Pico (UART)
#define MSG_DATA      4u // 노드 -> 다음 노드 (SPI 은닉 상태)

#define DTYPE_FP16    1u

typedef struct __attribute__((packed)) {
	uint32_t magic;
	uint8_t version, type;
	uint16_t sentence_id, token_count;
	uint32_t data_packet_bytes; // token_count * HIDDEN * 2
	uint8_t dtype, reserved;
	uint32_t crc32;
} config_message_t; // 20 bytes

typedef struct __attribute__((packed)) {
	uint32_t magic;
	uint8_t version, type;
	uint16_t sentence_id, token_count;
	uint32_t crc32;
	// uint16_t token_ids[token_count] follows
} tokens_header_t; // 14 bytes

typedef struct __attribute__((packed)) {
	uint32_t magic;
	uint8_t version, type;
	uint16_t sentence_id, token_count;
	uint32_t data_bytes; // payload 바이트 수
	uint8_t dtype, reserved;
	uint32_t crc32;
	// uint8_t payload[data_bytes] follows
} data_header_t; // 20 bytes

typedef struct __attribute__((packed)) {
	uint32_t magic;
	uint8_t version, type;
	uint16_t sentence_id;
	uint8_t dtype, flags;
	uint16_t payload_len;
	uint32_t crc32;
	// uint8_t payload[payload_len] follows
} result_header_t; // 16 bytes

_Static_assert(sizeof(config_message_t) == 20, "config_message_t size error");
_Static_assert(sizeof(tokens_header_t) == 14, "tokens_header_t size error");
_Static_assert(sizeof(data_header_t) == 20, "data_header_t size error");
_Static_assert(sizeof(result_header_t) == 16, "result_header_t size error");
