// ESP32-S3 BERT 클러스터 — 단일 통합 노드 펌웨어
// 플래시 데이터 파티션의 가중치 헤더(64B)를 읽어 노드 번호 및 역할을 자동 인식합니다.

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "driver/spi_master.h"
#include "driver/spi_slave.h"
#include "esp_partition.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_rom_crc.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"

#include "node_config.h"
#include "protocol.h"

static const char *TAG = "BERT_NODE";

// ── 64바이트 가중치 바이너리 헤더 (export_weights.py 와 일치) ─────────────────
typedef struct __attribute__((packed)) {
	uint32_t magic;        // 0: 0x42455254 ('BERT')
	uint16_t version;      // 4: 파일 버전 (1)
	uint8_t  node_id;      // 6: 노드 번호 (1..4)
	uint8_t  flags;        // 7: bit0=has_emb, bit1=has_head
	uint16_t hidden_size;  // 8: 히든 차원 (512)
	uint16_t num_layers;   // 10: 총 레이어 수 (8)
	uint8_t  layer_start;  // 12: 시작 레이어 (0-based)
	uint8_t  layer_end;    // 13: 종료 레이어 (inclusive)
	uint8_t  num_heads;    // 14: 어텐션 헤드 수 (8)
	uint16_t head_dim;     // 15: 헤드 차원 (64)
	uint16_t ffn_size;     // 17: FFN 중간 크기 (2048)
	uint8_t  dev_classes;  // 19: 장치 분류 수 (3)
	uint8_t  act_classes;  // 20: 동작 분류 수 (3~4)
	uint8_t  amt_classes;  // 21: 양 분류 수 (8)
	uint8_t  weight_dtype; // 22: 가중치 정밀도 (0=FP32, 1=FP16)
	uint16_t n_tensors;    // 23: 텐서 개수
	uint16_t vocab_size;   // 25: 어휘 크기
	uint16_t max_pos;      // 27: 최대 위치 임베딩 크기
	uint8_t  reserved[35]; // 29: 64바이트 정렬 패딩
} bert_weight_header_t;

_Static_assert(sizeof(bert_weight_header_t) == 64, "Header must be 64 bytes");

// ── 노드 런타임 상태 ─────────────────────────────────────────────────────────
static uint8_t g_node_id = 1;
static bool    g_has_emb = true;
static bool    g_has_head = false;
static uint8_t g_layer_start = 0;
static uint8_t g_layer_end = 1;

static uint8_t                *rx_buf = NULL;
static uint8_t                *tx_buf = NULL;
static spi_device_handle_t     tx_dev = NULL;
static spi_slave_transaction_t rx_trans[NODE_MAX_CHUNKS];

// ── 유틸리티 함수 ─────────────────────────────────────────────────────────────
static inline size_t align4(size_t v) { return (v + 3) & ~(size_t)3; }
static inline size_t nchunks(size_t total) {
	return (total + NODE_SPI_CHUNK_BYTES - 1) / NODE_SPI_CHUNK_BYTES;
}
static inline size_t chunklen(size_t total, size_t i) {
	size_t rem = total - i * NODE_SPI_CHUNK_BYTES;
	return (rem < NODE_SPI_CHUNK_BYTES) ? rem : NODE_SPI_CHUNK_BYTES;
}

static inline uint32_t crc32_data(const void *data, size_t len) {
	return esp_rom_crc32_le(0, (const uint8_t *)data, len);
}

static inline uint32_t crc32_split(const void *hdr, size_t hdr_len, const void *payload,
                                   size_t payload_len) {
	uint32_t c = esp_rom_crc32_le(0, (const uint8_t *)hdr, hdr_len);
	return esp_rom_crc32_le(c, (const uint8_t *)payload, payload_len);
}

static bool wait_level(int gpio, int level, uint32_t timeout_ms) {
	int64_t deadline = esp_timer_get_time() + (int64_t)timeout_ms * 1000;
	while (gpio_get_level((gpio_num_t)gpio) != level) {
		if (esp_timer_get_time() > deadline)
			return false;
		vTaskDelay(pdMS_TO_TICKS(1));
	}
	return true;
}

// ── 노드 역할 자동 감지 (플래시 가중치 파티션) ───────────────────────────────
static void detect_node_role(void) {
	const esp_partition_t *part =
	    esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "model");

	if (!part) {
		// "model" 레이블이 없는 경우 모든 DATA 파티션을 순회하여 BERT 매직(0x42455254) 탐색
		esp_partition_iterator_t it =
		    esp_partition_find(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, NULL);
		while (it != NULL) {
			const esp_partition_t *p = esp_partition_get(it);
			uint32_t magic = 0;
			if (esp_partition_read(p, 0, &magic, sizeof(magic)) == ESP_OK && magic == 0x42455254) {
				part = p;
				break;
			}
			it = esp_partition_next(it);
		}
		if (it != NULL) {
			esp_partition_iterator_release(it);
		}
	}

	bert_weight_header_t hdr;

	if (part && esp_partition_read(part, 0, &hdr, sizeof(hdr)) == ESP_OK &&
	    hdr.magic == 0x42455254) {
		g_node_id = hdr.node_id;
		g_has_emb = (hdr.flags & 1) != 0;
		g_has_head = (hdr.flags & 2) != 0;
		g_layer_start = hdr.layer_start;
		g_layer_end = hdr.layer_end;
		Serial.printf("\n[v] 가중치 정상 로드: [파티션 %s] -> Node %u (Layers %u-%u, emb=%d, head=%d)\n",
		              part->label, g_node_id, g_layer_start, g_layer_end, g_has_emb, g_has_head);
		Serial.flush();
	} else {
		// 가중치 미플래싱 시 기본값 (노드 1)
		g_node_id = 1;
		g_has_emb = true;
		g_has_head = false;
		g_layer_start = 0;
		g_layer_end = 1;
		Serial.printf("\n[!] 경고: 0x200000 가중치 파티션을 찾지 못했습니다. 기본 Node %u로 동작합니다.\n", g_node_id);
		Serial.flush();
	}
}

// ── UART 통신 ─────────────────────────────────────────────────────────────────
static void uart_setup(void) {
	uart_config_t uc;
	memset(&uc, 0, sizeof(uc));
	uc.baud_rate = 115200;
	uc.data_bits = UART_DATA_8_BITS;
	uc.parity = UART_PARITY_DISABLE;
	uc.stop_bits = UART_STOP_BITS_1;
	uc.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
	uc.source_clk = UART_SCLK_DEFAULT;

	ESP_ERROR_CHECK(uart_driver_install(UART_NUM_1, 2048, 0, 0, NULL, 0));
	ESP_ERROR_CHECK(uart_param_config(UART_NUM_1, &uc));
	ESP_ERROR_CHECK(
	    uart_set_pin(UART_NUM_1,
		             g_has_head ? PIN_UART_TX : UART_PIN_NO_CHANGE, // 노드 4만 TX 활성화
		             PIN_UART_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
}

static void wait_config(config_message_t *out) {
	uint8_t win[sizeof(config_message_t)];
	size_t  have = 0;
	while (true) {
		uint8_t b;
		if (uart_read_bytes(UART_NUM_1, &b, 1, portMAX_DELAY) != 1)
			continue;
		if (have == sizeof(win)) {
			memmove(win, win + 1, sizeof(win) - 1);
			have--;
		}
		win[have++] = b;
		if (have < sizeof(win))
			continue;

		config_message_t m;
		memcpy(&m, win, sizeof(m));
		if (m.magic == PROTO_MAGIC && m.version == PROTO_VERSION && m.type == MSG_CONFIG &&
		    crc32_data(&m, offsetof(config_message_t, crc32)) == m.crc32) {
			// 유효성 방어 검사 (버퍼 오버플로우 방지)
			if (m.token_count < 2 || m.token_count > CONFIG_MAX_TOKENS ||
			    m.data_packet_bytes !=
			        (uint32_t)m.token_count * CONFIG_HIDDEN_SIZE * CONFIG_BYTES_PER_VAL) {
				ESP_LOGW(TAG, "Invalid config payload: tokens=%u, bytes=%lu", m.token_count,
				         (unsigned long)m.data_packet_bytes);
				continue;
			}
			*out = m;
			return;
		}
	}
}

static void send_result(uint16_t sid, const uint8_t *payload, uint16_t len) {
	result_header_t h;
	memset(&h, 0, sizeof(h));
	h.magic = PROTO_MAGIC;
	h.version = PROTO_VERSION;
	h.type = MSG_RESULT;
	h.sentence_id = sid;
	h.dtype = 0;
	h.flags = 0;
	h.payload_len = len;
	h.crc32 = crc32_split(&h, offsetof(result_header_t, crc32), payload, len);

	uart_write_bytes(UART_NUM_1, &h, sizeof(h));
	uart_write_bytes(UART_NUM_1, payload, len);
	uart_wait_tx_done(UART_NUM_1, pdMS_TO_TICKS(1000));
}

// ── SPI 슬레이브 수신 (이전 노드 / Pico) ──────────────────────────────────────
static bool rx_open(uint8_t *buf, size_t total) {
	size_t nc = nchunks(total);

	spi_bus_config_t bus;
	memset(&bus, 0, sizeof(bus));
	bus.mosi_io_num = PIN_SPI_RX_MOSI;
	bus.miso_io_num = -1;
	bus.sclk_io_num = PIN_SPI_RX_SCLK;
	bus.quadwp_io_num = -1;
	bus.quadhd_io_num = -1;
	bus.max_transfer_sz = NODE_SPI_CHUNK_BYTES;

	spi_slave_interface_config_t sl;
	memset(&sl, 0, sizeof(sl));
	sl.spics_io_num = PIN_SPI_RX_CS;
	sl.queue_size = (int)nc;
	sl.mode = 0;

	if (spi_slave_initialize(SPI2_HOST, &bus, &sl, SPI_DMA_CH_AUTO) != ESP_OK)
		return false;

	for (size_t i = 0; i < nc; i++) {
		memset(&rx_trans[i], 0, sizeof(rx_trans[i]));
		rx_trans[i].length = align4(chunklen(total, i)) * 8;
		rx_trans[i].rx_buffer = buf + i * NODE_SPI_CHUNK_BYTES;
		if (spi_slave_queue_trans(SPI2_HOST, &rx_trans[i], portMAX_DELAY) != ESP_OK) {
			spi_slave_free(SPI2_HOST);
			return false;
		}
	}
	return true;
}

static bool rx_wait(size_t total) {
	size_t nc = nchunks(total);
	for (size_t i = 0; i < nc; i++) {
		spi_slave_transaction_t *r;
		uint32_t                 to = (i == 0) ? TIMEOUT_UPSTREAM_MS : TIMEOUT_CHUNK_MS;
		if (spi_slave_get_trans_result(SPI2_HOST, &r, pdMS_TO_TICKS(to)) != ESP_OK) {
			ESP_LOGW(TAG, "SPI rx timeout at chunk %u/%u", (unsigned)i, (unsigned)nc);
			return false;
		}
	}
	return true;
}

// ── SPI 마스터 송신 (다음 노드) ──────────────────────────────────────────────
static void spi_tx_setup(void) {
	spi_bus_config_t bus;
	memset(&bus, 0, sizeof(bus));
	bus.mosi_io_num = PIN_SPI_TX_MOSI;
	bus.miso_io_num = -1;
	bus.sclk_io_num = PIN_SPI_TX_SCLK;
	bus.quadwp_io_num = -1;
	bus.quadhd_io_num = -1;
	bus.max_transfer_sz = NODE_SPI_CHUNK_BYTES;

	spi_device_interface_config_t dev;
	memset(&dev, 0, sizeof(dev));
	dev.mode = 0;
	dev.clock_speed_hz = NODE_SPI_TX_HZ;
	dev.spics_io_num = PIN_SPI_TX_CS;
	dev.queue_size = 1;

	ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &bus, SPI_DMA_CH_AUTO));
	ESP_ERROR_CHECK(spi_bus_add_device(SPI3_HOST, &dev, &tx_dev));
}

static bool tx_send(const uint8_t *buf, size_t total) {
	size_t nc = nchunks(total);
	for (size_t i = 0; i < nc; i++) {
		spi_transaction_t t;
		memset(&t, 0, sizeof(t));
		t.length = chunklen(total, i) * 8;
		t.tx_buffer = buf + i * NODE_SPI_CHUNK_BYTES;
		if (spi_device_transmit(tx_dev, &t) != ESP_OK)
			return false;
		esp_rom_delay_us(NODE_CHUNK_GAP_US);
	}
	return true;
}

// ── 모델 연산 자리 ───────────────────────────────────────────────────────────
static void node_compute(uint16_t count, const uint8_t *in, uint8_t *out, size_t data_bytes) {
	if (g_has_emb) {
		// [노드 1] in: uint16_t token_ids[count] -> out: FP16 hidden states
		memset(out, 0, data_bytes);
		memcpy(out, in, (size_t)count * 2); // 통신 테스트용 복사
		// TODO: Word/Pos/Type 임베딩 + 레이어 0~1 연산
	} else {
		// [노드 2, 3] in: FP16 hidden states -> out: FP16 hidden states
		memcpy(out, in, data_bytes);
		// TODO: 담당 트랜스포머 레이어 연산
	}
}

// ── 한 문장 처리 파이프라인 ──────────────────────────────────────────────────
static void handle_sentence(const config_message_t *cfg) {
	const uint16_t sid = cfg->sentence_id, count = cfg->token_count;
	const size_t   data_bytes = cfg->data_packet_bytes;
	const size_t   rx_total = g_has_emb ? (sizeof(tokens_header_t) + (size_t)count * 2)
	                                    : (sizeof(data_header_t) + data_bytes);
	const size_t   tx_total = sizeof(data_header_t) + data_bytes;

	Serial.printf("[Node %u] 문장 #%u 처리 시작 (토큰수: %u, RX바이트: %u)\n", g_node_id, sid, count, (unsigned)rx_total);

	if (!rx_open(rx_buf, rx_total)) {
		Serial.printf("[Node %u] SPI 슬레이브 RX 열기 실패!\n", g_node_id);
		return;
	}

	// 1. 다음 노드 준비 확인 후 내 READY HIGH
	if (!g_has_head) {
		Serial.printf("[Node %u] 다음 노드 READY(D15) 대기 중...\n", g_node_id);
		if (!wait_level(PIN_RDY_DOWN, 1, TIMEOUT_READY_MS)) {
			Serial.printf("[Node %u] 다음 노드 READY 타임아웃 (5초 경과)! D15 핀 배선을 확인하세요.\n", g_node_id);
			spi_slave_free(SPI2_HOST);
			return;
		}
		Serial.printf("[Node %u] 다음 노드 READY 확인됨!\n", g_node_id);
	}

	Serial.printf("[Node %u] 내 READY(D%u) HIGH 출력 -> 이전 노드/Pico로 신호 전송\n", g_node_id, PIN_RDY_UP(g_node_id));
	gpio_set_level((gpio_num_t)PIN_RDY_UP(g_node_id), 1);
	bool ok = rx_wait(rx_total);
	gpio_set_level((gpio_num_t)PIN_RDY_UP(g_node_id), 0);
	spi_slave_free(SPI2_HOST);
	if (!ok) {
		Serial.printf("[Node %u] SPI 수신 타임아웃!\n", g_node_id);
		return;
	}
	Serial.printf("[Node %u] SPI 데이터 수신 완료!\n", g_node_id);

	// 2. 수신 프레임 헤더 및 CRC 무결성 검증
	const uint8_t *in_payload = NULL;
	if (g_has_emb) {
		// [노드 1] MSG_TOKENS 검증
		tokens_header_t h;
		memcpy(&h, rx_buf, sizeof(h));
		if (h.magic != PROTO_MAGIC || h.version != PROTO_VERSION || h.type != MSG_TOKENS ||
		    h.sentence_id != sid || h.token_count != count) {
			ESP_LOGW(TAG, "Tokens header mismatch");
			return;
		}
		in_payload = rx_buf + sizeof(tokens_header_t);
		uint32_t expected_crc = h.crc32;
		h.crc32 = 0; // 토큰 프레임: crc 필드를 0으로 두고 계산
		if (crc32_split(&h, sizeof(h), in_payload, (size_t)count * 2) != expected_crc) {
			ESP_LOGW(TAG, "Tokens CRC mismatch");
			return;
		}
	} else {
		// [노드 2, 3, 4] MSG_DATA 검증
		data_header_t h;
		memcpy(&h, rx_buf, sizeof(h));
		if (h.magic != PROTO_MAGIC || h.version != PROTO_VERSION || h.type != MSG_DATA ||
		    h.sentence_id != sid || h.token_count != count || h.data_bytes != data_bytes) {
			ESP_LOGW(TAG, "Data header mismatch");
			return;
		}
		in_payload = rx_buf + sizeof(data_header_t);
		if (crc32_split(&h, offsetof(data_header_t, crc32), in_payload, data_bytes) != h.crc32) {
			ESP_LOGW(TAG, "Data CRC mismatch");
			return;
		}
	}

	// 3. 마지막 노드(Node 4): 최종 결과 출력 및 Pico로 UART 회신
	// ※ g_has_head는 오직 노드 4에서만 true이므로, 다른 노드(1, 2, 3)에는 전혀 영향이 없습니다.
	if (g_has_head) {
		// 1) Pico로 바이너리 결과 먼저 전송 (Pico가 수신 후 즉시 성공 LED_OK 점멸)
		send_result(sid, in_payload, (uint16_t)(count * 2));
		vTaskDelay(pdMS_TO_TICKS(10)); // Pico의 패킷 수신 완료 대기

		// 2) PC 시리얼 모니터(UART1 및 Serial)에 사람이 읽을 수 있는 텍스트 출력
		const uint16_t *tokens = (const uint16_t *)in_payload;
		char msg[256];

		snprintf(msg, sizeof(msg),
		         "\r\n==========================================\r\n"
		         "[Node 4] 클러스터 완주! 최종 결과 도착 (문장 #%u, 토큰 %u개)\r\n"
		         "  [루프백 토큰 ID]: [ ",
		         sid, count);
		uart_write_bytes(UART_NUM_1, msg, strlen(msg));
		Serial.print(msg);

		for (size_t i = 0; i < count; i++) {
			snprintf(msg, sizeof(msg), "%u%s", tokens[i], (i + 1 < count) ? ", " : " ");
			uart_write_bytes(UART_NUM_1, msg, strlen(msg));
			Serial.print(msg);
		}

		snprintf(msg, sizeof(msg), "]\r\n==========================================\r\n\r\n");
		uart_write_bytes(UART_NUM_1, msg, strlen(msg));
		Serial.print(msg);
		return;
	}

	// 4. 노드 1~3: 연산 수행 후 다음 노드로 SPI 전송
	uint8_t *out_payload = tx_buf + sizeof(data_header_t);
	node_compute(count, in_payload, out_payload, data_bytes);

	data_header_t oh;
	memset(&oh, 0, sizeof(oh));
	oh.magic = PROTO_MAGIC;
	oh.version = PROTO_VERSION;
	oh.type = MSG_DATA;
	oh.sentence_id = sid;
	oh.token_count = count;
	oh.data_bytes = (uint32_t)data_bytes;
	oh.dtype = DTYPE_FP16;
	oh.reserved = 0;
	oh.crc32 = crc32_split(&oh, offsetof(data_header_t, crc32), out_payload, data_bytes);
	memcpy(tx_buf, &oh, sizeof(oh));

	if (wait_level(PIN_RDY_DOWN, 1, TIMEOUT_READY_MS)) {
		tx_send(tx_buf, tx_total);
	}
}

// ── 아두이노 진입점 ───────────────────────────────────────────────────────────
void setup(void) {
	Serial.begin(115200);
	delay(300); // USB 시리얼 포트 연결 대기
	Serial.println("\n\n========================================");
	Serial.println("  ESP32-S3 BERT Cluster Node Starting");
	Serial.println("========================================");
	Serial.flush();

	detect_node_role();
	Serial.flush();

	// READY 출력 핀 설정
	gpio_config_t out_cfg;
	memset(&out_cfg, 0, sizeof(out_cfg));
	out_cfg.pin_bit_mask = 1ULL << PIN_RDY_UP(g_node_id);
	out_cfg.mode = GPIO_MODE_OUTPUT;
	gpio_config(&out_cfg);
	gpio_set_level((gpio_num_t)PIN_RDY_UP(g_node_id), 0);

	// 노드 4를 제외한 노드는 다음 노드 READY 감시 & SPI 마스터 초기화
	if (!g_has_head) {
		gpio_config_t in_cfg;
		memset(&in_cfg, 0, sizeof(in_cfg));
		in_cfg.pin_bit_mask = 1ULL << PIN_RDY_DOWN;
		in_cfg.mode = GPIO_MODE_INPUT;
		in_cfg.pull_down_en = GPIO_PULLDOWN_ENABLE;
		gpio_config(&in_cfg);
		spi_tx_setup();
	}

	// DMA 버퍼 할당 (※ ESP32-S3 SPI 슬레이브 DMA는 반드시 내부 SRAM이어야 함!)
	size_t cap = align4(NODE_MAX_FRAME_BYTES) + 64;
	rx_buf = (uint8_t *)heap_caps_aligned_alloc(64, cap, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
	tx_buf = (uint8_t *)heap_caps_aligned_alloc(64, cap, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);

	if (!rx_buf || !tx_buf) {
		Serial.println("[!] DMA 버퍼 할당 실패! 내부 SRAM 메모리 부족.");
	} else {
		Serial.println("[v] 내부 SRAM DMA 버퍼 할당 성공 (64KB x 2)");
	}

	Serial.printf("[v] ESP32-S3 BERT Node %u 초기화 완료! (UART/SPI 대기 중)\n", g_node_id);
	Serial.flush();

	// UART1 초기화: 노드 4는 GPIO 43(TXD)을 Pico 송신용으로 점유하므로 맨 마지막에 초기화
	uart_setup();
}

void loop(void) {
	config_message_t cfg;
	wait_config(&cfg);
	handle_sentence(&cfg);
}
