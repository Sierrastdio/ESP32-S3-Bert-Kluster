#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/spi.h"
#include "pico/binary_info.h"
#include "pico_config.h"
#include "protocol.h"
#include "tokenizer.h"
#include "usb_kbd.h"
#include "hardware/clocks.h"
#include "vocab_data.h"
#include <stddef.h> // offsetof 용 (원래 빠져 있었음)
#include <stdio.h>
#include <string.h>

#define UART_ID uart0
#define SPI_ID  spi0

#define LED_PIN PICO_DEFAULT_LED_PIN

static void led_blink(int times, int on_ms, int off_ms) {
	for (int i = 0; i < times; i++) {
		gpio_put(LED_PIN, 1);
		sleep_ms(on_ms);
		gpio_put(LED_PIN, 0);
		sleep_ms(off_ms);
	}
}
#define LED_READY()     led_blink(1, 80, 0)
#define LED_OK()        led_blink(2, 80, 120)
#define LED_INPUT_ERR() led_blink(5, 50, 50)
#define LED_TIMEOUT()   led_blink(3, 400, 200)
#define LED_FATAL()                                                                                \
	while (1)                                                                                      \
	led_blink(1, 50, 50)

static uint16_t sentence_id;

uint32_t protocol_crc32(const void *data, uint32_t length) {
	const uint8_t *p = data;
	uint32_t crc = 0xffffffffu;
	while (length--) {
		crc ^= *p++;
		for (int b = 0; b < 8; b++)
			crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
	}
	return ~crc;
}

static bool read_line(char *buf, size_t cap) {
	size_t n = 0;
	while (true) {
		int c = usb_kbd_getchar();
		if (c == PICO_ERROR_TIMEOUT) {
			tight_loop_contents();
			continue;
		}
		// Debug: blink once for every character event delivered by the USB keyboard.
		gpio_put(LED_PIN, 1);
		sleep_ms(25);
		gpio_put(LED_PIN, 0);
		if (c == '\n') {
			if (!n)
				continue;
			buf[n] = 0;
			return true;
		}
		if (c == 8) {
			if (n)
				n--;
			continue;
		}
		if (c < 32 || c > 126)
			continue; // 무시
		if (n + 1 >= cap) {
			while (usb_kbd_getchar() != '\n')
				tight_loop_contents();
			return false;
		}
		buf[n++] = (char)c;
	}
}

static bool uart_broadcast(const void *data, size_t len) {
	uart_write_blocking(UART_ID, (const uint8_t *)data, len);
	return true;
}

static bool spi_send_tokens(const uint16_t *ids, uint16_t count) {
	tokens_header_t h = {PROTO_MAGIC, PROTO_VERSION, MSG_TOKENS, sentence_id, count, 0};
	uint8_t packet[sizeof(h) + CONFIG_MAX_TOKENS * 2];
	memcpy(packet, &h, sizeof(h));
	memcpy(packet + sizeof(h), ids, count * 2);
	// CRC is calculated over the complete header (crc32 field zeroed) and IDs.
	uint32_t crc = protocol_crc32(packet, sizeof(h) + count * 2);
	memcpy(packet + offsetof(tokens_header_t, crc32), &crc, sizeof(crc));
	gpio_put(CONFIG_SPI_TX_CS_PIN, 0);
	spi_write_blocking(SPI_ID, packet, sizeof(h) + count * 2);
	gpio_put(CONFIG_SPI_TX_CS_PIN, 1);
	return true;
}

int main(void) {
	set_sys_clock_khz(120000, true);
	gpio_init(LED_PIN);
	gpio_set_dir(LED_PIN, GPIO_OUT);
	usb_kbd_start();

	uart_init(UART_ID, CONFIG_UART_BAUD);
	gpio_set_function(CONFIG_UART_TX_PIN, GPIO_FUNC_UART);
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

	while (true) {
		if (!read_line(line, sizeof(line))) {
			LED_INPUT_ERR();
			continue;
		}
		size_t count = 0;
		if (!tokenizer_encode(line, ids, CONFIG_MAX_TOKENS, &count)) {
			LED_INPUT_ERR();
			continue;
		}

		// Keyboard/tokenizer bring-up mode: show the result before involving nodes.
		printf("tokens=%u :", (unsigned)count);
		for (size_t i = 0; i < count; i++) {
			printf(" %u=\"%s\"", ids[i], vocab_words[ids[i]]);
		}
		printf("\n");
		LED_OK();
		sentence_id++;

		// Node communication is deliberately paused while validating keyboard input
		// and tokenization. Re-enable config broadcast, READY wait, and SPI send later.
		continue;

		/*
		uint32_t payload = (uint32_t)count * CONFIG_HIDDEN_SIZE * CONFIG_BYTES_PER_VALUE;
		config_message_t cfg = {PROTO_MAGIC, PROTO_VERSION, MSG_CONFIG, sentence_id,
		(uint16_t)count, payload, DTYPE_FP16, 0, 0}; cfg.crc32 = protocol_crc32(&cfg,
		offsetof(config_message_t, crc32)); uart_broadcast(&cfg, sizeof(cfg));

		absolute_time_t deadline = make_timeout_time_ms(5000);
		while (!gpio_get(CONFIG_SPI_READY_PIN) && !time_reached(deadline))
		tight_loop_contents(); if (!gpio_get(CONFIG_SPI_READY_PIN)) { LED_TIMEOUT();
		continue; }

		spi_send_tokens(ids, (uint16_t)count);
		sentence_id++;
		LED_OK();
		*/
	}
}
