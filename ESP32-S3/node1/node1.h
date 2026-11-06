#pragma once
// ===== 노드 번호: 1..4 (idf.py -DNODE_INDEX=n 으로 지정) =====
#ifndef NODE_INDEX
#define NODE_INDEX 1
#endif
#define NODE_FIRST (NODE_INDEX == 1)
#define NODE_LAST  (NODE_INDEX == 4)

// ===== Pico의 pico_config.h와 반드시 같은 값 =====
#define CONFIG_MAX_TOKENS      64
#define CONFIG_HIDDEN_SIZE     512
#define CONFIG_BYTES_PER_VALUE 2

// ===== UART (Pico TX는 4개 노드 RX에 공통 연결, ESP4 TX는 Pico RX로) =====
#define NODE_UART_NUM      UART_NUM_1
#define NODE_UART_BAUD     115200
#define NODE_UART_RX_GPIO  18      // TODO: 실제 배선에 맞게 수정
#define NODE_UART_TX_GPIO  17      // ESP4만 사용 (TODO: 실제 배선)

// ===== 수신 SPI 슬레이브 (이전 노드 또는 Pico -> 이 노드): FSPI 기본 핀 =====
#define NODE_RX_HOST       SPI2_HOST
#define NODE_RX_CS         10
#define NODE_RX_MOSI       11
#define NODE_RX_SCLK       12

// ===== 송신 SPI 마스터 (이 노드 -> 다음 노드) =====
#define NODE_TX_HOST       SPI3_HOST
#define NODE_TX_SCLK       4
#define NODE_TX_MOSI       5
#define NODE_TX_CS         6
#define NODE_TX_HZ         10000000   // 처음엔 1MHz로 시작해서 올려 가며 측정할 것

// ===== READY 체인 =====
// RDY_UP  : 이 노드가 출력. Pico(ESP1일 때) 또는 이전 노드가 읽는다.
// RDY_DOWN: 이 노드가 입력. 다음 노드의 RDY_UP.
#define NODE_RDY_UP_GPIO   (NODE_INDEX == 1 ? 13 : 16)
#define NODE_RDY_DOWN_GPIO 15

// ===== SPI 전송 분할 =====
// 한 번의 SPI 트랜잭션은 한계가 있어서 큰 패킷은 청크로 나눠 보낸다. 64의 배수.
#define NODE_SPI_CHUNK_BYTES 4096
#define NODE_CHUNK_GAP_US    100
#define NODE_MAX_FRAME_BYTES (20 + CONFIG_MAX_TOKENS * CONFIG_HIDDEN_SIZE * CONFIG_BYTES_PER_VALUE)
#define NODE_MAX_CHUNKS      ((NODE_MAX_FRAME_BYTES + NODE_SPI_CHUNK_BYTES - 1) / NODE_SPI_CHUNK_BYTES)

// ===== 타임아웃 =====
#define NODE_READY_TIMEOUT_MS    5000
#define NODE_UPSTREAM_TIMEOUT_MS 120000   // 앞 노드 연산이 끝나길 기다리는 최대 시간
#define NODE_CHUNK_TIMEOUT_MS    2000
