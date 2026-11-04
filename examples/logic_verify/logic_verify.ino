// ESP32-S3 한 대: BERT-Medium FFN 첫 행렬(512->2048, fp16 가중치)급 연산 검증
#include "esp_partition.h"
#include "esp_timer.h"
#include <Arduino.h>

#define IN_DIM 512
#define OUT_MAX 2048
#define MAX_T 8       // 한 번에 계산할 토큰 수 상한 (SRAM 사정에 맞춰 조정)
#define BENCH_ROWS 64 // 플래시/SRAM 비교용 행 수
#define SEED_W 0xC0FFEE01u
#define SEED_X 0x12345678u

static uint32_t rngState;
static inline uint32_t rnd() {
  uint32_t x = rngState;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  rngState = x;
  return x;
}
// fp16 비트를 직접 생성: 지수 12~14(정규수), 크기 약 0.125~1
static inline uint16_t genHalf() {
  uint32_t r = rnd();
  uint32_t s = (r >> 31) & 1u;
  uint32_t e = 12u + ((r >> 20) % 3u);
  uint32_t m = r & 0x3FFu;
  return (uint16_t)((s << 15) | (e << 10) | m);
}
// fp16 -> fp32 (정규수만 처리하는 빠른 경로. 0/서브노멀/inf/NaN은 가정하지
// 않음)
static inline float h2f(uint16_t h) {
  uint32_t u = ((uint32_t)(h & 0x8000u) << 16) |
               ((((uint32_t)(h >> 10) & 0x1Fu) + 112u) << 23) |
               ((uint32_t)(h & 0x3FFu) << 13);
  float f;
  memcpy(&f, &u, 4);
  return f;
}

// y[t*ld + j] = sum_k x[t*IN+k] * W[j*IN+k]   (가중치 한 행을 fp32로 풀어 모든
// 토큰에 재사용)
__attribute__((optimize("O3"))) static void linearFp16(const uint16_t *W,
                                                       int rows, const float *x,
                                                       int T, float *y, int ld,
                                                       float *rowf) {
  for (int j = 0; j < rows; j++) {
    const uint16_t *wr = W + (size_t)j * IN_DIM;
    for (int k = 0; k < IN_DIM; k++)
      rowf[k] = h2f(wr[k]);
    for (int t = 0; t < T; t++) {
      const float *xt = x + (size_t)t * IN_DIM;
      float a0 = 0, a1 = 0, a2 = 0, a3 = 0;
      for (int k = 0; k < IN_DIM; k += 4) {
        a0 += rowf[k] * xt[k];
        a1 += rowf[k + 1] * xt[k + 1];
        a2 += rowf[k + 2] * xt[k + 2];
        a3 += rowf[k + 3] * xt[k + 3];
      }
      y[(size_t)t * ld + j] = (a0 + a1) + (a2 + a3);
    }
  }
}

static bool partitionMatches(const esp_partition_t *p, size_t nHalf) {
  static uint16_t buf[2048];
  rngState = SEED_W;
  for (size_t i = 0; i < nHalf; i += 2048) {
    size_t cnt = min((size_t)2048, nHalf - i);
    if (esp_partition_read(p, i * 2, buf, cnt * 2) != ESP_OK)
      return false;
    for (size_t k = 0; k < cnt; k++)
      if (buf[k] != genHalf())
        return false;
  }
  return true;
}

static void writePartition(const esp_partition_t *p, size_t nHalf) {
  static uint16_t buf[2048];
  size_t eraseBytes = ((nHalf * 2 + 4095) / 4096) * 4096;
  ESP_ERROR_CHECK(esp_partition_erase_range(p, 0, eraseBytes));
  rngState = SEED_W;
  for (size_t i = 0; i < nHalf; i += 2048) {
    size_t cnt = min((size_t)2048, nHalf - i);
    for (size_t k = 0; k < cnt; k++)
      buf[k] = genHalf();
    ESP_ERROR_CHECK(esp_partition_write(p, i * 2, buf, cnt * 2));
  }
}

static void printStats(const char *tag, int T, int outDim, const float *y,
                       int64_t us) {
  double sum = 0;
  for (size_t i = 0; i < (size_t)T * outDim; i++)
    sum += y[i];
  const float *yl = y + (size_t)(T - 1) * outDim + outDim - 4;
  Serial.printf("%s T=%d %lldus (%.1f ns/weight) sum=%.6f y0[0..3]=%.6f %.6f "
                "%.6f %.6f yLast[-4..]=%.6f %.6f %.6f %.6f\n",
                tag, T, (long long)us,
                (double)us * 1000.0 / ((double)IN_DIM * outDim), sum, y[0],
                y[1], y[2], y[3], yl[0], yl[1], yl[2], yl[3]);
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.printf("heap free=%u maxAlloc=%u\n", (unsigned)ESP.getFreeHeap(),
                (unsigned)ESP.getMaxAllocHeap());

  const esp_partition_t *part = esp_partition_find_first(
      ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "spiffs");
  if (!part)
    part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                    ESP_PARTITION_SUBTYPE_ANY, "ffat");
  if (!part) {
    Serial.println("spiffs/ffat 파티션 없음: Partition Scheme 확인");
    while (1)
      delay(1000);
  }

  uint32_t maxRows = part->size / (IN_DIM * 2);
  int outDim = (maxRows < (uint32_t)OUT_MAX) ? (int)maxRows : OUT_MAX;
  outDim -= outDim % 4;
  size_t nHalf = (size_t)IN_DIM * outDim;
  size_t bytes = nHalf * 2;
  Serial.printf("partition '%s' size=%u -> OUT=%d (가중치 %u바이트)\n",
                part->label, (unsigned)part->size, outDim, (unsigned)bytes);
  if (outDim < 64) {
    Serial.println("파티션이 너무 작음");
    while (1)
      delay(1000);
  }

  if (!partitionMatches(part, nHalf)) {
    Serial.println("가중치 생성/기록 중...");
    writePartition(part, nHalf);
    if (!partitionMatches(part, nHalf)) {
      Serial.println("기록 검증 실패");
      while (1)
        delay(1000);
    }
  }
  Serial.println("가중치 준비 완료");

  const void *ptr = nullptr;
  esp_partition_mmap_handle_t handle;
  ESP_ERROR_CHECK(esp_partition_mmap(part, 0, bytes, ESP_PARTITION_MMAP_DATA,
                                     &ptr, &handle));
  const uint16_t *W = (const uint16_t *)ptr;

  float *x = (float *)malloc(sizeof(float) * MAX_T * IN_DIM);
  float *y = (float *)malloc(sizeof(float) * MAX_T * (size_t)outDim);
  float *rowf = (float *)malloc(sizeof(float) * IN_DIM);
  uint16_t *sramW = (uint16_t *)malloc(sizeof(uint16_t) * BENCH_ROWS * IN_DIM);
  if (!x || !y || !rowf || !sramW) {
    Serial.println("메모리 할당 실패: MAX_T/BENCH_ROWS를 줄이세요");
    while (1)
      delay(1000);
  }

  rngState = SEED_X;
  for (int i = 0; i < MAX_T * IN_DIM; i++)
    x[i] = ((int32_t)(rnd() >> 8) - 8388608) / 8388608.0f;

  // (1) 순수 플래시 순차 읽기 속도
  {
    const uint32_t *p32 = (const uint32_t *)W;
    size_t nw = bytes / 4;
    uint32_t s = 0;
    int64_t t0 = esp_timer_get_time();
    for (size_t i = 0; i < nw; i++)
      s += p32[i];
    int64_t us = esp_timer_get_time() - t0;
    Serial.printf("[flash read] %u바이트 %lldus = %.1f MB/s (chk=%u)\n",
                  (unsigned)bytes, (long long)us, (double)bytes / us,
                  (unsigned)s);
  }

  // (2) 전체 행렬 연산: 토큰 수별 시간과 PC 비교용 통계
  static const int tList[] = {1, 2, 4, 8};
  int64_t usT8 = 0;
  for (int ti = 0; ti < 4; ti++) {
    int T = tList[ti];
    if (T > MAX_T)
      break;
    int64_t t0 = esp_timer_get_time();
    linearFp16(W, outDim, x, T, y, outDim, rowf);
    int64_t us = esp_timer_get_time() - t0;
    printStats("[compute]", T, outDim, y, us);
    if (T == 8)
      usT8 = us;
  }

  // (3) 플래시 vs SRAM: 같은 연산을 같은 행 수로 비교 (연산이 병목인지 플래시가
  // 병목인지)
  {
    int T = (MAX_T < 8) ? MAX_T : 8;
    memcpy(sramW, W, sizeof(uint16_t) * BENCH_ROWS * IN_DIM);
    int64_t t0 = esp_timer_get_time();
    linearFp16(W, BENCH_ROWS, x, T, y, outDim, rowf);
    int64_t usF = esp_timer_get_time() - t0;
    t0 = esp_timer_get_time();
    linearFp16(sramW, BENCH_ROWS, x, T, y, outDim, rowf);
    int64_t usS = esp_timer_get_time() - t0;
    Serial.printf(
        "[flash vs sram] T=%d rows=%d  flash=%.1fus/row  sram=%.1fus/row\n", T,
        BENCH_ROWS, (double)usF / BENCH_ROWS, (double)usS / BENCH_ROWS);
  }

  if (usT8 > 0)
    Serial.printf("[환산] 레이어 1개(약 이 연산의 3배) @T=8 ≈ %.0f ms\n",
                  usT8 * 3.0 / 1000.0);
  Serial.println("완료");
}

void loop() { delay(1000); }