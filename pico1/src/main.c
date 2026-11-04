#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/spi.h"
#include "pico/binary_info.h"
#include "pico_config.h"
#include "protocol.h"
#include "tokenizer.h"
#include <stdio.h>
#include <string.h>

#define UART_ID uart0
#define SPI_ID spi0
static uint16_t sentence_id;

uint32_t protocol_crc32(const void *data, uint32_t length) {
  const uint8_t *p = data; uint32_t crc = 0xffffffffu;
  while (length--) { crc ^= *p++; for (int b=0;b<8;b++) crc=(crc>>1)^(0xedb88320u & (0u-(crc&1u))); }
  return ~crc;
}

static bool read_line(char *buf, size_t cap) {
  printf("\n문장 입력> "); fflush(stdout); size_t n=0;
  while (true) {
    int c=getchar_timeout_us(0);
    if (c==PICO_ERROR_TIMEOUT) { tight_loop_contents(); continue; }
    if (c=='\r' || c=='\n') { if (!n) continue; buf[n]=0; puts(""); return true; }
    if (c==8 || c==127) { if (n) n--; continue; }
    if (c<32 || c>126) { puts("ASCII 영어 입력만 지원합니다."); while (getchar_timeout_us(0)!='\n') tight_loop_contents(); return false; }
    if (n+1>=cap) { puts("입력이 너무 깁니다."); while (getchar_timeout_us(0)!='\n') tight_loop_contents(); return false; }
    buf[n++]=(char)c; putchar(c); fflush(stdout);
  }
}

static bool uart_broadcast(const void *data, size_t len) {
  uart_write_blocking(UART_ID, (const uint8_t *)data, len);
  return true;
}

static bool spi_send_tokens(const uint16_t *ids, uint16_t count) {
  tokens_header_t h={PROTO_MAGIC,PROTO_VERSION,MSG_TOKENS,sentence_id,count,0};
  uint8_t packet[sizeof(h)+CONFIG_MAX_TOKENS*2];
  memcpy(packet,&h,sizeof(h)); memcpy(packet+sizeof(h),ids,count*2);
  // CRC is calculated over the complete header (crc32 field zeroed) and IDs.
  uint32_t crc=protocol_crc32(packet,sizeof(h)+count*2);
  memcpy(packet+offsetof(tokens_header_t,crc32),&crc,sizeof(crc));
  gpio_put(CONFIG_SPI_TX_CS_PIN,0);
  spi_write_blocking(SPI_ID,packet,sizeof(h)+count*2);
  gpio_put(CONFIG_SPI_TX_CS_PIN,1);
  return true;
}

int main(void) {
  stdio_init_all(); sleep_ms(1500);
  uart_init(UART_ID,CONFIG_UART_BAUD);
  gpio_set_function(CONFIG_UART_TX_PIN,GPIO_FUNC_UART);
  spi_init(SPI_ID,CONFIG_SPI_BAUD);
  gpio_set_function(CONFIG_SPI_TX_SCK_PIN,GPIO_FUNC_SPI);
  gpio_set_function(CONFIG_SPI_TX_MOSI_PIN,GPIO_FUNC_SPI);
  gpio_init(CONFIG_SPI_TX_CS_PIN); gpio_set_dir(CONFIG_SPI_TX_CS_PIN,GPIO_OUT); gpio_put(CONFIG_SPI_TX_CS_PIN,1);
  gpio_init(CONFIG_SPI_READY_PIN); gpio_set_dir(CONFIG_SPI_READY_PIN,GPIO_IN); gpio_pull_down(CONFIG_SPI_READY_PIN);
  bi_decl(bi_1pin_with_name(CONFIG_UART_TX_PIN,"Node config UART TX"));
  puts("Pico 1 controller starting");
  if (!tokenizer_load_vocab(CONFIG_VOCAB_PATH)) { puts("vocab.txt missing or lacks [UNK]/[CLS]/[SEP]"); while(1) sleep_ms(1000); }
  char line[256]; uint16_t ids[CONFIG_MAX_TOKENS];
  while (true) {
    if (!read_line(line,sizeof(line))) continue;
    size_t count=0;
    if (!tokenizer_encode(line,ids,CONFIG_MAX_TOKENS,&count)) { puts("토큰화 실패: 비ASCII, 토큰 초과 또는 어휘 문제"); continue; }
    uint32_t payload=(uint32_t)count*CONFIG_HIDDEN_SIZE*CONFIG_BYTES_PER_VALUE;
    config_message_t cfg={PROTO_MAGIC,PROTO_VERSION,MSG_CONFIG,sentence_id,(uint16_t)count,payload,DTYPE_FP16,0,0};
    cfg.crc32=protocol_crc32(&cfg,offsetof(config_message_t,crc32));
    uart_broadcast(&cfg,sizeof(cfg));
    printf("sentence=%u tokens=%u data-packet=%lu bytes\n",sentence_id,count,(unsigned long)payload);
    // READY is expected high only after all four nodes accept this sentence's config.
    absolute_time_t deadline=make_timeout_time_ms(5000);
    while (!gpio_get(CONFIG_SPI_READY_PIN) && !time_reached(deadline)) tight_loop_contents();
    if (!gpio_get(CONFIG_SPI_READY_PIN)) { puts("노드 READY timeout"); continue; }
    spi_send_tokens(ids,(uint16_t)count);
    sentence_id++;
  }
}
