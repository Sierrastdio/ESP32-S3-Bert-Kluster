#pragma once
#include <stdint.h>

// Pico 1과 ESP 노드 4개가 같은 파일을 공유한다. 양쪽 프로젝트에 똑같이 복사할 것.
// 모든 정수는 little-endian, 구조체는 packed 순서 그대로 전송한다.

#define PROTO_MAGIC   0x31504342u /* BCP1 */
#define PROTO_VERSION 1u
#define MSG_CONFIG    1u   // Pico -> 전 노드 (UART 브로드캐스트)
#define MSG_TOKENS    2u   // Pico -> ESP1 (SPI)
#define MSG_RESULT    3u   // ESP4 -> Pico (UART)
#define MSG_DATA      4u   // 노드 -> 다음 노드 (SPI, 은닉 상태 패킷)
#define DTYPE_FP16    1u

// CRC32 = 표준 CRC-32 (reflected, poly 0xEDB88320, init 0xFFFFFFFF, final xor).
// 범위 규칙:
//   config / data / result : crc32 필드 앞까지의 헤더 바이트 + payload 전체
//   tokens                 : crc32 필드를 0으로 둔 헤더 전체 + 토큰 ID 전체

typedef struct __attribute__((packed)) {
	uint32_t magic;
	uint8_t version, type;
	uint16_t sentence_id, token_count;
	uint32_t data_packet_bytes;   // = token_count * HIDDEN_SIZE * BYTES_PER_VALUE
	uint8_t dtype, reserved;
	uint32_t crc32;
} config_message_t;               // 20 bytes

typedef struct __attribute__((packed)) {
	uint32_t magic;
	uint8_t version, type;
	uint16_t sentence_id, token_count;
	uint32_t crc32;
	// uint16_t token_ids[token_count] follows
} tokens_header_t;                // 14 bytes

typedef struct __attribute__((packed)) {
	uint32_t magic;
	uint8_t version, type;
	uint16_t sentence_id, token_count;
	uint32_t data_bytes;          // payload 바이트 수 (= config의 data_packet_bytes)
	uint8_t dtype, reserved;
	uint32_t crc32;
	// uint8_t payload[data_bytes] follows
} data_header_t;                  // 20 bytes

#ifndef CONFIG_MAX_RESULT_BYTES
#define CONFIG_MAX_RESULT_BYTES 1024   // 결과 payload 최대 크기 (실제 결과 크기에 맞게 조정)
#endif

typedef struct __attribute__((packed)) {
	uint32_t magic;
	uint8_t version, type;
	uint16_t sentence_id;
	uint8_t dtype, flags;
	uint16_t payload_len;
	uint32_t crc32;
	// uint8_t payload[payload_len] follows
} result_header_t;                // 16 bytes

_Static_assert(sizeof(config_message_t) == 20, "config_message_t size");
_Static_assert(sizeof(tokens_header_t) == 14, "tokens_header_t size");
_Static_assert(sizeof(data_header_t) == 20, "data_header_t size");
_Static_assert(sizeof(result_header_t) == 16, "result_header_t size");

uint32_t protocol_crc32(const void *data, uint32_t length);

