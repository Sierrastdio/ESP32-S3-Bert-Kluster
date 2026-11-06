// ESP32-S3 노드 공통 펌웨어 (ESP-IDF). NODE_INDEX = 1..4
//
// 한 문장 처리 흐름:
//   1) UART로 설정 메시지 수신 (Pico 브로드캐스트, 모든 노드 동시)
//   2) SPI 슬레이브 수신 준비 -> 다음 노드 READY 확인 -> 내 READY 올림
//   3) 앞 노드(ESP1은 Pico)에서 프레임 수신 -> READY 내림
//   4) 검증 -> 연산(node_compute) -> 다음 노드로 SPI 전송 (ESP4는 Pico로 UART 결과 전송)
//
// 연산은 아직 없다. 지금은 ESP1이 토큰 ID를 payload 앞에 심고, 중간 노드는 그대로 전달하고,
// ESP4가 그 ID를 결과로 되돌려 보낸다. (Pico의 CONFIG_LOOPBACK_TEST와 짝)

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "driver/spi_master.h"
#include "driver/spi_slave.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_rom_crc.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"

#include "node_config.h"
#include "protocol.h"

static const char *TAG = "node";

static uint8_t *rx_buf;   // 수신 프레임 (PSRAM)
static uint8_t *tx_buf;   // 송신 프레임 (PSRAM)
static spi_device_handle_t tx_dev;
static spi_slave_transaction_t rx_trans[NODE_MAX_CHUNKS];

// ---------------------------------------------------------------- 유틸
static inline size_t round_up(size_t v, size_t a) { return (v + a - 1) / a * a; }

uint32_t protocol_crc32(const void *data, uint32_t length) {
  return esp_rom_crc32_le(0, (const uint8_t *)data, length);   // 표준 CRC-32
}

static uint32_t crc_prefix_payload(const void *hdr, size_t prefix, const void *payload, size_t len) {
  uint32_t c = esp_rom_crc32_le(0, (const uint8_t *)hdr, prefix);
  return esp_rom_crc32_le(c, (const uint8_t *)payload, len);
}

static bool wait_level(int gpio, int level, uint32_t timeout_ms) {
  int64_t deadline = esp_timer_get_time() + (int64_t)timeout_ms * 1000;
  while (gpio_get_level(gpio) != level) {
    if (esp_timer_get_time() > deadline) return false;
    vTaskDelay(1);
  }
  return true;
}

static size_t chunk_count(size_t total) { return (total + NODE_SPI_CHUNK_BYTES - 1) / NODE_SPI_CHUNK_BYTES; }
static size_t chunk_len(size_t total, size_t i) {
  size_t rem = total - i * NODE_SPI_CHUNK_BYTES;
  return rem < NODE_SPI_CHUNK_BYTES ? rem : NODE_SPI_CHUNK_BYTES;
}

// ---------------------------------------------------------------- UART
static void uart_setup(void) {
  uart_config_t uc = {
    .baud_rate = NODE_UART_BAUD,
    .data_bits = UART_DATA_8_BITS,
    .parity = UART_PARITY_DISABLE,
    .stop_bits = UART_STOP_BITS_1,
    .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    .source_clk = UART_SCLK_DEFAULT,
  };
  ESP_ERROR_CHECK(uart_driver_install(NODE_UART_NUM, 2048, 0, 0, NULL, 0));
  ESP_ERROR_CHECK(uart_param_config(NODE_UART_NUM, &uc));
  // ESP1~3은 TX를 쓰지 않는다 (Pico TX 공통선만 받음)
  ESP_ERROR_CHECK(uart_set_pin(NODE_UART_NUM, NODE_LAST ? NODE_UART_TX_GPIO : UART_PIN_NO_CHANGE,
                               NODE_UART_RX_GPIO, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
}

// 설정 메시지가 올 때까지 블록. 바이트 스트림에서 MAGIC으로 프레임을 찾는다.
static void wait_config(config_message_t *out) {
  uint8_t win[sizeof(config_message_t)];
  size_t have = 0;
  while (true) {
    uint8_t b;
    if (uart_read_bytes(NODE_UART_NUM, &b, 1, portMAX_DELAY) != 1) continue;
    if (have == sizeof(win)) { memmove(win, win + 1, sizeof(win) - 1); have--; }
    win[have++] = b;
    if (have < sizeof(win)) continue;

    config_message_t m;
    memcpy(&m, win, sizeof(m));
    if (m.magic != PROTO_MAGIC || m.version != PROTO_VERSION || m.type != MSG_CONFIG) continue;
    if (protocol_crc32(&m, offsetof(config_message_t, crc32)) != m.crc32) { ESP_LOGW(TAG, "config CRC 불일치"); continue; }
    have = 0;
    if (m.token_count < 2 || m.token_count > CONFIG_MAX_TOKENS ||
        m.data_packet_bytes != (uint32_t)m.token_count * CONFIG_HIDDEN_SIZE * CONFIG_BYTES_PER_VALUE) {
      ESP_LOGW(TAG, "config 값 이상: tokens=%u bytes=%lu", m.token_count, (unsigned long)m.data_packet_bytes);
      continue;
    }
    *out = m;
    return;
  }
}

static void send_result(uint16_t sid, const uint8_t *payload, uint16_t len) {
  result_header_t h = {
    .magic = PROTO_MAGIC, .version = PROTO_VERSION, .type = MSG_RESULT,
    .sentence_id = sid, .dtype = 0, .flags = 0, .payload_len = len,
  };
  h.crc32 = crc_prefix_payload(&h, offsetof(result_header_t, crc32), payload, len);
  uart_write_bytes(NODE_UART_NUM, &h, sizeof(h));
  uart_write_bytes(NODE_UART_NUM, payload, len);
  uart_wait_tx_done(NODE_UART_NUM, pdMS_TO_TICKS(1000));
}

// ---------------------------------------------------------------- SPI 수신 (슬레이브)
// 문장마다 초기화/해제해서 이전 문장의 미완료 트랜잭션이 남지 않게 한다.
static bool rx_open(uint8_t *buf, size_t total) {
  size_t n = chunk_count(total);
  spi_bus_config_t bus = {
    .mosi_io_num = NODE_RX_MOSI, .miso_io_num = -1, .sclk_io_num = NODE_RX_SCLK,
    .quadwp_io_num = -1, .quadhd_io_num = -1, .max_transfer_sz = NODE_SPI_CHUNK_BYTES,
  };
  spi_slave_interface_config_t sl = {
    .mode = 0, .spics_io_num = NODE_RX_CS, .queue_size = (int)n, .flags = 0,
  };
  if (spi_slave_initialize(NODE_RX_HOST, &bus, &sl, SPI_DMA_CH_AUTO) != ESP_OK) return false;
  for (size_t i = 0; i < n; i++) {
    memset(&rx_trans[i], 0, sizeof(rx_trans[i]));
    rx_trans[i].length = round_up(chunk_len(total, i), 4) * 8;   // DMA 수신 길이는 4바이트 배수
    rx_trans[i].rx_buffer = buf + i * NODE_SPI_CHUNK_BYTES;
    if (spi_slave_queue_trans(NODE_RX_HOST, &rx_trans[i], portMAX_DELAY) != ESP_OK) {
      spi_slave_free(NODE_RX_HOST);
      return false;
    }
  }
  return true;
}

static bool rx_collect(size_t total) {
  size_t n = chunk_count(total);
  for (size_t i = 0; i < n; i++) {
    spi_slave_transaction_t *r;
    uint32_t to = (i == 0) ? NODE_UPSTREAM_TIMEOUT_MS : NODE_CHUNK_TIMEOUT_MS;
    if (spi_slave_get_trans_result(NODE_RX_HOST, &r, pdMS_TO_TICKS(to)) != ESP_OK) {
      ESP_LOGW(TAG, "SPI 수신 타임아웃 (chunk %u/%u)", (unsigned)i, (unsigned)n);
      return false;
    }
    if (r->trans_len != chunk_len(total, i) * 8) {
      ESP_LOGW(TAG, "SPI 청크 길이 불일치 (chunk %u: %u bits)", (unsigned)i, (unsigned)r->trans_len);
      return false;
    }
  }
  return true;
}

static void rx_close(void) { spi_slave_free(NODE_RX_HOST); }

// ---------------------------------------------------------------- SPI 송신 (마스터)
static void tx_setup(void) {
  spi_bus_config_t bus = {
    .mosi_io_num = NODE_TX_MOSI, .miso_io_num = -1, .sclk_io_num = NODE_TX_SCLK,
    .quadwp_io_num = -1, .quadhd_io_num = -1, .max_transfer_sz = NODE_SPI_CHUNK_BYTES,
  };
  spi_device_interface_config_t dev = {
    .clock_speed_hz = NODE_TX_HZ, .mode = 0, .spics_io_num = NODE_TX_CS, .queue_size = 1,
  };
  ESP_ERROR_CHECK(spi_bus_initialize(NODE_TX_HOST, &bus, SPI_DMA_CH_AUTO));
  ESP_ERROR_CHECK(spi_bus_add_device(NODE_TX_HOST, &dev, &tx_dev));
}

static bool tx_send(const uint8_t *buf, size_t total) {
  size_t n = chunk_count(total);
  for (size_t i = 0; i < n; i++) {
    spi_transaction_t t = { .length = chunk_len(total, i) * 8, .tx_buffer = buf + i * NODE_SPI_CHUNK_BYTES };
    if (spi_device_transmit(tx_dev, &t) != ESP_OK) return false;
    esp_rom_delay_us(NODE_CHUNK_GAP_US);   // 슬레이브가 다음 청크를 장전할 시간
  }
  return true;
}

// ---------------------------------------------------------------- 연산 자리
// in : 앞 노드 payload (ESP1은 토큰 ID 배열, count*2 바이트)
// out: 다음 노드로 보낼 payload (data_bytes 바이트)
static void node_compute(uint16_t count, const uint8_t *in, uint8_t *out, size_t data_bytes) {
  (void)count;
#if NODE_FIRST
  memset(out, 0, data_bytes);
  memcpy(out, in, (size_t)count * 2);      // TODO: 임베딩 + 담당 레이어
#else
  memcpy(out, in, data_bytes);             // TODO: 담당 레이어
#endif
}

// ---------------------------------------------------------------- 한 문장 처리
static void handle_sentence(const config_message_t *cfg) {
  const uint16_t sid = cfg->sentence_id, count = cfg->token_count;
  const size_t data_bytes = cfg->data_packet_bytes;
  const size_t rx_total = NODE_FIRST ? sizeof(tokens_header_t) + (size_t)count * 2
                                     : sizeof(data_header_t) + data_bytes;
  const size_t tx_total = sizeof(data_header_t) + data_bytes;

  if (!rx_open(rx_buf, rx_total)) { ESP_LOGE(TAG, "SPI 슬레이브 초기화 실패"); return; }

  // 내 READY = (설정 수락) AND (수신 준비) AND (다음 노드 READY)
  if (!NODE_LAST && !wait_level(NODE_RDY_DOWN_GPIO, 1, NODE_READY_TIMEOUT_MS)) {
    ESP_LOGW(TAG, "다음 노드 READY 타임아웃");
    rx_close();
    return;
  }
  gpio_set_level(NODE_RDY_UP_GPIO, 1);
  bool ok = rx_collect(rx_total);
  gpio_set_level(NODE_RDY_UP_GPIO, 0);
  rx_close();
  if (!ok) return;

  // ---- 검증
  const uint8_t *in_payload;
  if (NODE_FIRST) {
    tokens_header_t h;
    memcpy(&h, rx_buf, sizeof(h));
    if (h.magic != PROTO_MAGIC || h.version != PROTO_VERSION || h.type != MSG_TOKENS ||
        h.sentence_id != sid || h.token_count != count) { ESP_LOGW(TAG, "tokens 헤더 불일치"); return; }
    in_payload = rx_buf + sizeof(tokens_header_t);
    uint32_t want = h.crc32;
    h.crc32 = 0;                                    // 토큰 프레임: crc 필드를 0으로 두고 계산
    if (crc_prefix_payload(&h, sizeof(h), in_payload, (size_t)count * 2) != want) { ESP_LOGW(TAG, "tokens CRC 불일치"); return; }
  } else {
    data_header_t h;
    memcpy(&h, rx_buf, sizeof(h));
    if (h.magic != PROTO_MAGIC || h.version != PROTO_VERSION || h.type != MSG_DATA ||
        h.sentence_id != sid || h.token_count != count || h.data_bytes != data_bytes) { ESP_LOGW(TAG, "data 헤더 불일치"); return; }
    in_payload = rx_buf + sizeof(data_header_t);
    if (crc_prefix_payload(&h, offsetof(data_header_t, crc32), in_payload, data_bytes) != h.crc32) { ESP_LOGW(TAG, "data CRC 불일치"); return; }
  }

  // ---- 연산
  if (NODE_LAST) {
    // 최종 결과: 지금은 되돌려받은 토큰 ID
    send_result(sid, in_payload, (uint16_t)(count * 2));
    return;
  }
  uint8_t *out_payload = tx_buf + sizeof(data_header_t);
  node_compute(count, in_payload, out_payload, data_bytes);

  data_header_t oh = {
    .magic = PROTO_MAGIC, .version = PROTO_VERSION, .type = MSG_DATA,
    .sentence_id = sid, .token_count = count, .data_bytes = (uint32_t)data_bytes,
    .dtype = DTYPE_FP16, .reserved = 0,
  };
  oh.crc32 = crc_prefix_payload(&oh, offsetof(data_header_t, crc32), out_payload, data_bytes);
  memcpy(tx_buf, &oh, sizeof(oh));

  // ---- 다음 노드로 전송 (다음 노드 READY는 아직 high여야 한다)
  if (!wait_level(NODE_RDY_DOWN_GPIO, 1, NODE_READY_TIMEOUT_MS)) { ESP_LOGW(TAG, "전송 전 다음 노드 READY 없음"); return; }
  if (!tx_send(tx_buf, tx_total)) ESP_LOGW(TAG, "SPI 전송 실패");
}

// ---------------------------------------------------------------- 진입점
void app_main(void) {
  gpio_config_t out = { .pin_bit_mask = 1ULL << NODE_RDY_UP_GPIO, .mode = GPIO_MODE_OUTPUT };
  ESP_ERROR_CHECK(gpio_config(&out));
  gpio_set_level(NODE_RDY_UP_GPIO, 0);
  if (!NODE_LAST) {
    // 풀다운: 다음 노드가 꺼져 있어도 low로 읽히게
    gpio_config_t in = { .pin_bit_mask = 1ULL << NODE_RDY_DOWN_GPIO, .mode = GPIO_MODE_INPUT, .pull_down_en = GPIO_PULLDOWN_ENABLE };
    ESP_ERROR_CHECK(gpio_config(&in));
  }

  uart_setup();

  const size_t cap = round_up(NODE_MAX_FRAME_BYTES, 64) + 64;
  rx_buf = heap_caps_aligned_alloc(64, cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
  tx_buf = heap_caps_aligned_alloc(64, cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
  if (!rx_buf || !tx_buf) { ESP_LOGE(TAG, "PSRAM 할당 실패 (sdkconfig의 SPIRAM 설정 확인)"); abort(); }
  if (!NODE_LAST) tx_setup();

  ESP_LOGI(TAG, "node %d ready", NODE_INDEX);
  while (true) {
    config_message_t cfg;
    wait_config(&cfg);
    handle_sentence(&cfg);
  }
}
