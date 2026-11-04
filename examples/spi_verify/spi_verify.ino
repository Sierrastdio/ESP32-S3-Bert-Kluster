// ESP32-S3 SPI 링크 로직 검증 (보드 A=SENDER, 보드 B=RECEIVER)
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/spi_slave.h"
#include "esp_heap_caps.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include <Arduino.h>

////////////////////////////////////////////////////////////////////////////////
#define ROLE_SENDER 1
#define ROLE_RECEIVER 2
#define NODE_ROLE ROLE_RECEIVER // 보드 A: ROLE_SENDER / 보드 B: ROLE_RECEIVER
////////////////////////////////////////////////////////////////////////////////

// ---- 핀맵 ----
#define PIN_TX_SCLK GPIO_NUM_4
#define PIN_TX_MOSI GPIO_NUM_5
#define PIN_TX_CS GPIO_NUM_6
#define PIN_TX_RDY_IN GPIO_NUM_16
#define PIN_RX_CS GPIO_NUM_10
#define PIN_RX_MOSI GPIO_NUM_11
#define PIN_RX_SCLK GPIO_NUM_12
#define PIN_RX_RDY_OUT GPIO_NUM_15

// ---- 테스트 설정 ----
#define SPI_CLK_HZ 1000000 // 1MHz에서 시작, 단계적으로 올릴 것
#define HIDDEN 512
#define BYTES_PER_VAL 2 // fp16 크기 가정 (4=fp32, 1=int8)
#define MAX_TOKENS 24
#define HDR_LEN 8
#define MAX_PAYLOAD (MAX_TOKENS * HIDDEN * BYTES_PER_VAL) // 24576B
#define GUARD_US 200
#define READY_TIMEOUT_US 50000

typedef struct __attribute__((packed)) {
  uint8_t type;
  uint8_t seq;
  uint8_t token_count;
  uint8_t flags;
  uint16_t payload_len;
  uint16_t crc; // 앞 6바이트의 CRC-16/CCITT-FALSE
} frame_hdr_t;
static_assert(sizeof(frame_hdr_t) == HDR_LEN, "header must be 8 bytes");

static inline uint32_t pad4(uint32_t n) { return (n + 3u) & ~3u; }
static inline uint8_t pat(uint8_t seq, uint32_t i) {
  return (uint8_t)(seq * 31u + i * 7u);
}

static uint16_t crc16(const uint8_t *d, size_t n) {
  uint16_t crc = 0xFFFF;
  while (n--) {
    crc ^= (uint16_t)(*d++) << 8;
    for (int i = 0; i < 8; i++)
      crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021)
                           : (uint16_t)(crc << 1);
  }
  return crc;
}

#if NODE_ROLE == ROLE_SENDER
// ====================== 송신 (마스터, SPI3) ======================
static spi_device_handle_t spi;
static uint8_t *hdrBuf, *payBuf;
static uint8_t seqNo = 0;
static int okCnt = 0, failCnt = 0;
static const uint8_t tokList[] = {1, 2, 4, 8, 12, 16, 24};
static size_t tokIdx = 0;

static bool waitReadyHigh() {
  int64_t t0 = esp_timer_get_time();
  while (!gpio_get_level(PIN_TX_RDY_IN)) {
    if (esp_timer_get_time() - t0 > READY_TIMEOUT_US)
      return false;
    esp_rom_delay_us(2);
  }
  return true;
}

void setup() {
  Serial.begin(115200);
  delay(500);

  gpio_config_t io = {};
  io.pin_bit_mask = 1ULL << PIN_TX_RDY_IN;
  io.mode = GPIO_MODE_INPUT;
  io.pull_down_en = GPIO_PULLDOWN_ENABLE;
  io.pull_up_en = GPIO_PULLUP_DISABLE;
  io.intr_type = GPIO_INTR_DISABLE;
  gpio_config(&io);

  spi_bus_config_t bus = {};
  bus.mosi_io_num = PIN_TX_MOSI;
  bus.miso_io_num = -1;
  bus.sclk_io_num = PIN_TX_SCLK;
  bus.quadwp_io_num = -1;
  bus.quadhd_io_num = -1;
  bus.max_transfer_sz = MAX_PAYLOAD;
  ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &bus, SPI_DMA_CH_AUTO));

  spi_device_interface_config_t dev = {};
  dev.clock_speed_hz = SPI_CLK_HZ;
  dev.mode = 0;
  dev.spics_io_num = PIN_TX_CS;
  dev.queue_size = 1;
  ESP_ERROR_CHECK(spi_bus_add_device(SPI3_HOST, &dev, &spi));

  hdrBuf = (uint8_t *)heap_caps_malloc(HDR_LEN, MALLOC_CAP_DMA);
  payBuf = (uint8_t *)heap_caps_malloc(pad4(MAX_PAYLOAD), MALLOC_CAP_DMA);
  Serial.println("sender ready");
}

void loop() {
  uint8_t tokens = tokList[tokIdx];
  tokIdx = (tokIdx + 1) % sizeof(tokList);
  uint32_t len = (uint32_t)tokens * HIDDEN * BYTES_PER_VAL;

  frame_hdr_t h = {};
  h.type = 1;
  h.seq = seqNo;
  h.token_count = tokens;
  h.flags = 0;
  h.payload_len = (uint16_t)len;
  h.crc = crc16((const uint8_t *)&h, 6);
  memcpy(hdrBuf, &h, HDR_LEN);
  for (uint32_t i = 0; i < pad4(len); i++)
    payBuf[i] = (i < len) ? pat(seqNo, i) : 0;

  int64_t t0 = esp_timer_get_time();
  bool good = false;
  if (waitReadyHigh()) {
    spi_transaction_t th = {};
    th.length = HDR_LEN * 8;
    th.tx_buffer = hdrBuf;
    spi_device_transmit(spi, &th);
    esp_rom_delay_us(GUARD_US); // 수신측이 READY를 내렸다가 재장전할 시간
    if (waitReadyHigh()) {
      spi_transaction_t tp = {};
      tp.length = pad4(len) * 8;
      tp.tx_buffer = payBuf;
      spi_device_transmit(spi, &tp);
      good = true;
    }
  }
  int64_t us = esp_timer_get_time() - t0;
  if (good)
    okCnt++;
  else
    failCnt++;
  Serial.printf("seq=%u tokens=%u len=%luB %s total=%lldus (ok=%d fail=%d)\n",
                seqNo, tokens, (unsigned long)len, good ? "SENT" : "TIMEOUT",
                (long long)us, okCnt, failCnt);
  seqNo++;
  delay(200);
}

#else
// ====================== 수신 (슬레이브, SPI2) ======================
// 콜백은 ISR에서 호출됨: DMA 장전 후 READY 상승, 전송 완료 직후 하강
static void IRAM_ATTR onSetup(spi_slave_transaction_t *) {
  gpio_set_level(PIN_RX_RDY_OUT, 1);
}
static void IRAM_ATTR onTrans(spi_slave_transaction_t *) {
  gpio_set_level(PIN_RX_RDY_OUT, 0);
}

static uint8_t *rxh, *rxp;
static uint8_t lastSeq = 0;
static bool firstFrame = true;
static int okCnt = 0, badHdr = 0, badPay = 0, seqGap = 0;

void setup() {
  Serial.begin(115200);
  delay(500);

  gpio_config_t out = {};
  out.pin_bit_mask = 1ULL << PIN_RX_RDY_OUT;
  out.mode = GPIO_MODE_OUTPUT;
  out.pull_up_en = GPIO_PULLUP_DISABLE;
  out.pull_down_en = GPIO_PULLDOWN_DISABLE;
  out.intr_type = GPIO_INTR_DISABLE;
  gpio_config(&out);
  gpio_set_level(PIN_RX_RDY_OUT, 0);

  gpio_set_pull_mode(PIN_RX_MOSI, GPIO_PULLUP_ONLY);
  gpio_set_pull_mode(PIN_RX_SCLK, GPIO_PULLUP_ONLY);
  gpio_set_pull_mode(PIN_RX_CS, GPIO_PULLUP_ONLY);

  spi_bus_config_t bus = {};
  bus.mosi_io_num = PIN_RX_MOSI;
  bus.miso_io_num = -1; // 초기화 오류가 나면 GPIO_NUM_13 지정(배선은 하지 않음)
  bus.sclk_io_num = PIN_RX_SCLK;
  bus.quadwp_io_num = -1;
  bus.quadhd_io_num = -1;
  bus.max_transfer_sz = pad4(MAX_PAYLOAD);

  spi_slave_interface_config_t slv = {};
  slv.mode = 0;
  slv.spics_io_num = PIN_RX_CS;
  slv.queue_size = 2;
  slv.flags = 0;
  slv.post_setup_cb = onSetup;
  slv.post_trans_cb = onTrans;
  ESP_ERROR_CHECK(spi_slave_initialize(SPI2_HOST, &bus, &slv, SPI_DMA_CH_AUTO));

  rxh = (uint8_t *)heap_caps_malloc(HDR_LEN, MALLOC_CAP_DMA);
  rxp = (uint8_t *)heap_caps_malloc(pad4(MAX_PAYLOAD), MALLOC_CAP_DMA);
  Serial.println("receiver ready");
}

void loop() {
  // 1단계: 헤더 8바이트 수신
  memset(rxh, 0, HDR_LEN);
  spi_slave_transaction_t th = {};
  th.length = HDR_LEN * 8;
  th.rx_buffer = rxh;
  spi_slave_transmit(SPI2_HOST, &th, portMAX_DELAY);

  frame_hdr_t h;
  memcpy(&h, rxh, HDR_LEN);
  bool hdrOk =
      (th.trans_len == HDR_LEN * 8) && (crc16(rxh, 6) == h.crc) &&
      (h.token_count >= 1 && h.token_count <= MAX_TOKENS) &&
      (h.payload_len == (uint32_t)h.token_count * HIDDEN * BYTES_PER_VAL);
  if (!hdrOk) {
    badHdr++;
    Serial.printf("BAD HEADER (bits=%d) badHdr=%d  (재동기화 미구현)\n",
                  (int)th.trans_len, badHdr);
    return;
  }

  // 2단계: 헤더 길이에 맞춰 DMA 재장전 후 페이로드 수신
  spi_slave_transaction_t tp = {};
  tp.length = pad4(h.payload_len) * 8;
  tp.rx_buffer = rxp;
  spi_slave_transmit(SPI2_HOST, &tp, portMAX_DELAY);

  uint32_t mism = 0;
  for (uint32_t i = 0; i < h.payload_len; i++)
    if (rxp[i] != pat(h.seq, i))
      mism++;
  bool lenOk = (tp.trans_len >= (size_t)h.payload_len * 8);

  if (!firstFrame && h.seq != (uint8_t)(lastSeq + 1))
    seqGap++;
  firstFrame = false;
  lastSeq = h.seq;

  if (mism == 0 && lenOk)
    okCnt++;
  else
    badPay++;
  Serial.printf("seq=%u tokens=%u len=%uB mismatch=%lu lenOk=%d | ok=%d "
                "badHdr=%d badPay=%d seqGap=%d\n",
                h.seq, h.token_count, h.payload_len, (unsigned long)mism,
                (int)lenOk, okCnt, badHdr, badPay, seqGap);
}
#endif
