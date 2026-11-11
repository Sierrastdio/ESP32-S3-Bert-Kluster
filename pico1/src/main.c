#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/spi.h"
#include "hardware/clocks.h"
#include "pico_config.h"
#include "protocol.h"
#include "tokenizer.h"
#include "usb_kbd.h"
#include "vocab_data.h"
#include <stddef.h>
#include <string.h>

#define UART_ID uart0
#define SPI_ID  spi0
#define LED_PIN PICO_DEFAULT_LED_PIN

// ---- pico_config.h에 없으면 쓰는 기본값 ----
#ifndef CONFIG_UART_RX_PIN
#define CONFIG_UART_RX_PIN 1 // uart0 RX = GP1 (ESP4 TX 연결)
#endif
#ifndef CONFIG_READY_TIMEOUT_MS
#define CONFIG_READY_TIMEOUT_MS 5000
#endif
#ifndef CONFIG_RESULT_TIMEOUT_MS
#define CONFIG_RESULT_TIMEOUT_MS 30000 // SPI 전송 후 ESP4 결과를 기다리는 최대 시간
#endif
#ifndef CONFIG_REQUIRE_READY_LOW
#define CONFIG_REQUIRE_READY_LOW 1 // 전송 전에 READY가 low(유휴)인지 확인
#endif
#ifndef CONFIG_LOOPBACK_TEST
#define CONFIG_LOOPBACK_TEST 1 // 1: ESP4가 되돌려준 결과가 보낸 토큰 ID와 같은지 검사 (노드 연산 붙이면 0)
#endif
#ifndef CONFIG_KEY_DEBUG_BLINK
#define CONFIG_KEY_DEBUG_BLINK 0 // 1: 키 입력마다 LED 깜빡임
#endif

// ---- LED 상태 표시 ----
static void led_blink(int times, int on_ms, int off_ms)
{
  for (int i = 0; i < times; i++)
  {
    gpio_put(LED_PIN, 1);
    sleep_ms(on_ms);
    gpio_put(LED_PIN, 0);
    sleep_ms(off_ms);
  }
}
#define LED_READY()          led_blink(1, 80, 0)    // 부팅 완료: 짧게 1회
#define LED_OK()             led_blink(2, 80, 120)  // 결과 수신까지 성공: 짧게 2회
#define LED_INPUT_ERR()      led_blink(5, 50, 50)   // 입력/토큰화 오류: 빠르게 5회
#define LED_TIMEOUT()        led_blink(3, 400, 200) // 노드 READY 타임아웃: 길게 3회
#define LED_RESULT_TIMEOUT() led_blink(4, 250, 150) // 결과 수신 타임아웃: 중간 4회
#define LED_RESULT_BAD()    \
  do                        \
  {                         \
    led_blink(1, 800, 200); \
    led_blink(3, 60, 60);   \
  } while (0) // 결과 CRC/형식 오류
#define LED_FATAL() \
  while (1)         \
  led_blink(1, 50, 50) // vocab 로드 실패: 무한 빠른 점멸

static uint16_t sentence_id;

// ---- CRC32 ----
static uint32_t crc32_update(uint32_t crc, const void *data, size_t len)
{
  const uint8_t *p = (const uint8_t *)data;
  while (len--)
  {
    crc ^= *p++;
    for (int b = 0; b < 8; b++)
      crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
  }
  return crc;
}

uint32_t protocol_crc32(const void *data, uint32_t length) { return ~crc32_update(0xffffffffu, data, length); }

// ---- 키보드 입력 ----
static bool read_line(char *buf, size_t cap)
{
  size_t n = 0;
  while (true)
  {
    int c = usb_kbd_getchar();
    if (c == PICO_ERROR_TIMEOUT)
    {
      tight_loop_contents();
      continue;
    }
#if CONFIG_KEY_DEBUG_BLINK
    gpio_put(LED_PIN, 1);
    sleep_ms(25);
    gpio_put(LED_PIN, 0);
#endif
    if (c == '\n')
    {
      if (!n)
        continue;
      buf[n] = 0;
      return true;
    }
    if (c == 8)
    {
      if (n)
        n--;
      continue;
    }
    if (c < 32 || c > 126)
      continue;
    if (n + 1 >= cap)
    { // 너무 긴 입력: Enter까지 버림
      while (usb_kbd_getchar() != '\n')
        tight_loop_contents();
      return false;
    }
    buf[n++] = (char)c;
  }
}

// ---- 노드 통신 ----
static void uart_broadcast(const void *data, size_t len) { uart_write_blocking(UART_ID, (const uint8_t *)data, len); }

static void uart_drain_rx(void)
{
  while (uart_is_readable(UART_ID))
    (void)uart_getc(UART_ID);
}

static bool wait_ready(bool level, uint32_t timeout_ms)
{
  absolute_time_t deadline = make_timeout_time_ms(timeout_ms);
  while ((bool)gpio_get(CONFIG_SPI_READY_PIN) != level)
  {
    if (time_reached(deadline))
      return false;
    tight_loop_contents();
  }
  return true;
}

static void spi_send_tokens(const uint16_t *ids, uint16_t count, uint16_t sid)
{
  tokens_header_t h = {PROTO_MAGIC, PROTO_VERSION, MSG_TOKENS, sid, count, 0};
  uint8_t packet[sizeof(h) + CONFIG_MAX_TOKENS * 2];
  memcpy(packet, &h, sizeof(h));
  memcpy(packet + sizeof(h), ids, (size_t)count * 2);
  // CRC는 crc32 필드를 0으로 둔 헤더 전체 + 토큰 ID 전체에 대해 계산한다.
  uint32_t crc = protocol_crc32(packet, sizeof(h) + (size_t)count * 2);
  memcpy(packet + offsetof(tokens_header_t, crc32), &crc, sizeof(crc));
  gpio_put(CONFIG_SPI_TX_CS_PIN, 0);
  spi_write_blocking(SPI_ID, packet, sizeof(h) + (size_t)count * 2);
  gpio_put(CONFIG_SPI_TX_CS_PIN, 1);
}

// ---- ESP4 결과 수신 (UART RX) ----
typedef enum
{
  RX_OK,
  RX_TIMEOUT,
  RX_BAD
} rx_status_t;

static result_header_t result_hdr;
static uint8_t result_payload[CONFIG_MAX_RESULT_BYTES];

static bool rx_byte(uint8_t *b, absolute_time_t deadline)
{
  while (!uart_is_readable(UART_ID))
  {
    if (time_reached(deadline))
      return false;
    tight_loop_contents();
  }
  *b = (uint8_t)uart_getc(UART_ID);
  return true;
}

// 바이트 스트림에서 MAGIC으로 프레임 시작을 찾는다. 잡음(ESP 부팅 로그 등)은 건너뛴다.
// sentence_id가 다른 프레임(이전 문장의 늦은 응답)은 버리고 계속 기다린다.
static rx_status_t receive_result(uint16_t expect_sid, uint32_t timeout_ms)
{
  absolute_time_t deadline = make_timeout_time_ms(timeout_ms);
  uint8_t win[sizeof(result_header_t)];
  size_t have = 0;
  bool seen_bad = false;

  while (true)
  {
    uint8_t b;
    if (!rx_byte(&b, deadline))
      return seen_bad ? RX_BAD : RX_TIMEOUT;
    if (have == sizeof(win))
    {
      memmove(win, win + 1, sizeof(win) - 1);
      have--;
    }
    win[have++] = b;
    if (have < sizeof(win))
      continue;

    memcpy(&result_hdr, win, sizeof(win));
    if (result_hdr.magic != (uint32_t)PROTO_MAGIC)
      continue;
    if (result_hdr.version != (uint8_t)PROTO_VERSION)
      continue;
    if (result_hdr.type != MSG_RESULT)
      continue;
    if (result_hdr.payload_len > CONFIG_MAX_RESULT_BYTES)
    {
      seen_bad = true;
      continue;
    }

    for (size_t i = 0; i < result_hdr.payload_len; i++)
      if (!rx_byte(&result_payload[i], deadline))
        return RX_BAD; // 프레임 도중 끊김
    have = 0;

    uint32_t crc = crc32_update(0xffffffffu, &result_hdr, offsetof(result_header_t, crc32));
    crc = ~crc32_update(crc, result_payload, result_hdr.payload_len);
    if (crc != result_hdr.crc32)
    {
      seen_bad = true;
      continue;
    }
    if (result_hdr.sentence_id != expect_sid)
      continue; // 이전 문장의 응답
    return RX_OK;
  }
}

// 결과가 도착했을 때 호출됨. TFT 출력은 여기에 붙이면 된다. (후순위)
static void on_result(const result_header_t *h, const uint8_t *payload)
{
  (void)h;
  (void)payload;
}

int main(void)
{
  set_sys_clock_khz(120000, true); // PIO USB 때문에 120MHz, 다른 초기화보다 먼저
  gpio_init(LED_PIN);
  gpio_set_dir(LED_PIN, GPIO_OUT);
  usb_kbd_start();

  uart_init(UART_ID, CONFIG_UART_BAUD);
  gpio_set_function(CONFIG_UART_TX_PIN, GPIO_FUNC_UART);
  gpio_set_function(CONFIG_UART_RX_PIN, GPIO_FUNC_UART);
  uart_set_fifo_enabled(UART_ID, true);

  spi_init(SPI_ID, CONFIG_SPI_BAUD);
  gpio_set_function(CONFIG_SPI_TX_SCK_PIN, GPIO_FUNC_SPI);
  gpio_set_function(CONFIG_SPI_TX_MOSI_PIN, GPIO_FUNC_SPI);
  gpio_init(CONFIG_SPI_TX_CS_PIN);
  gpio_set_dir(CONFIG_SPI_TX_CS_PIN, GPIO_OUT);
  gpio_put(CONFIG_SPI_TX_CS_PIN, 1);
  gpio_init(CONFIG_SPI_READY_PIN);
  gpio_set_dir(CONFIG_SPI_READY_PIN, GPIO_IN);
  gpio_pull_down(CONFIG_SPI_READY_PIN);

  if (!tokenizer_load_vocab_mem(vocab_words, vocab_word_count))
    LED_FATAL();
  char line[256];
  uint16_t ids[CONFIG_MAX_TOKENS];
  LED_READY();

  while (true)
  {
    if (!read_line(line, sizeof(line)))
    {
      LED_INPUT_ERR();
      continue;
    }
    size_t count = 0;
    if (!tokenizer_encode(line, ids, CONFIG_MAX_TOKENS, &count))
    {
      LED_INPUT_ERR();
      continue;
    }

    const uint16_t sid = sentence_id++; // 이후 단계가 실패해도 ID는 소비한다

#if CONFIG_REQUIRE_READY_LOW
    // 이전 문장의 READY가 high로 남아 있으면 이번 설정이 확인되지 않은 채 통과해 버린다.
    if (!wait_ready(false, CONFIG_READY_TIMEOUT_MS))
    {
      LED_TIMEOUT();
      continue;
    }
#endif

    // 1) UART로 설정 브로드캐스트: 모든 노드가 같은 SPI 패킷 크기로 설정한다.
    uint32_t payload = (uint32_t)count * CONFIG_HIDDEN_SIZE * CONFIG_BYTES_PER_VALUE;
    config_message_t cfg = {PROTO_MAGIC, PROTO_VERSION, MSG_CONFIG, sid, (uint16_t)count, payload, DTYPE_FP16, 0, 0};
    cfg.crc32 = protocol_crc32(&cfg, offsetof(config_message_t, crc32));
    uart_drain_rx(); // 이전 문장의 잔여 바이트 제거
    uart_broadcast(&cfg, sizeof(cfg));

    // 2) 4개 노드가 모두 설정을 받으면 READY가 high가 된다.
    if (!wait_ready(true, CONFIG_READY_TIMEOUT_MS))
    {
      LED_TIMEOUT();
      continue;
    }

    // 3) SPI로 토큰 전송 (ESP1부터 체인으로 전달됨)
    spi_send_tokens(ids, (uint16_t)count, sid);

    // 4) ESP4의 최종 결과를 UART로 수신
    rx_status_t st = receive_result(sid, CONFIG_RESULT_TIMEOUT_MS);
    if (st == RX_OK)
    {
#if CONFIG_LOOPBACK_TEST
      if (result_hdr.payload_len != count * 2 || memcmp(result_payload, ids, count * 2) != 0)
      {
        LED_RESULT_BAD();
        continue;
      }
#endif
      on_result(&result_hdr, result_payload);
      LED_OK();
    }
    else if (st == RX_TIMEOUT)
    {
      LED_RESULT_TIMEOUT();
    }
    else
    {
      LED_RESULT_BAD();
    }
  }
}
