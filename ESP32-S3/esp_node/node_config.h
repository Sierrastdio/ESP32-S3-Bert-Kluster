#pragma once

// ── 모델 및 프레임 크기 상수 (Pico의 pico_config.h와 일치) ───────────────────
#define CONFIG_MAX_TOKENS    64
#define CONFIG_HIDDEN_SIZE   512
#define CONFIG_BYTES_PER_VAL 2 // FP16 기준 (2바이트)

#define NODE_MAX_PAYLOAD     (CONFIG_MAX_TOKENS * CONFIG_HIDDEN_SIZE * CONFIG_BYTES_PER_VAL) // 64KB
#define NODE_MAX_FRAME_BYTES (20 + NODE_MAX_PAYLOAD)
#define NODE_SPI_CHUNK_BYTES 4096
#define NODE_CHUNK_GAP_US    100
#define NODE_MAX_CHUNKS ((NODE_MAX_FRAME_BYTES + NODE_SPI_CHUNK_BYTES - 1) / NODE_SPI_CHUNK_BYTES)

// ── 핀맵 (pinMap.md 및 회로도 REV1.0 기준) ──────────────────────────────────
// UART: Pico TX(D0) -> 전 노드 RXD(GPIO 44), 노드4 TXD(GPIO 43) -> Pico RX(D1)
// ※ 보드 핀헤더의 RX/TX 실크스크린 핀입니다. (기존 임시값 18, 17에서 수정)
#define PIN_UART_RX 44 // Pico TX -> 전 노드 RX (RXD 핀)
#define PIN_UART_TX 43 // 노드 4 TX -> Pico RX (TXD 핀)

// 수신 SPI 슬레이브 핀 (FSPI: 이전 노드 또는 Pico로부터 수신)
#define PIN_SPI_RX_CS   10 // CS: D10
#define PIN_SPI_RX_MOSI 11 // MOSI: D11
#define PIN_SPI_RX_SCLK 12 // SCLK: D12

// 송신 SPI 마스터 핀 (HSPI: 다음 노드로 송신)
#define PIN_SPI_TX_SCLK 4 // SCLK: D4
#define PIN_SPI_TX_MOSI 5 // MOSI: D5
#define PIN_SPI_TX_CS   6 // CS: D6
#define NODE_SPI_TX_HZ  10000000

// READY 제어선 (pinMap.md 기준)
#define PIN_RDY_DOWN 15 // 다음 노드의 READY 상태 입력 (D15)
// RDY_UP: 노드 1은 Pico D6으로(D13), 노드 2~4는 이전 노드 D15로(D16)
#define PIN_RDY_UP(node_id) ((node_id) == 1 ? 13 : 16)

// ── 통신 타임아웃 (ms) ───────────────────────────────────────────────────────
#define TIMEOUT_READY_MS    5000
#define TIMEOUT_UPSTREAM_MS 120000
#define TIMEOUT_CHUNK_MS    2000
