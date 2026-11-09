# ESP32-S3 BERT Cluster 펌웨어 및 가중치 플래싱 가이드

> [!TIP]
> 단계별 상세 업로드 및 보드 설정 과정은 **[상세 업로드 가이드 (UPLOAD_GUIDE.md)](UPLOAD_GUIDE.md)**를 참고하세요.

모든 ESP32-S3 보드는 **N16R8 (16MB Quad/Octal SPI Flash, 8MB Octal PSRAM)** 버전을 기준으로 구성되었습니다.

---

## 1. 메모리 및 플래시 파티션 레이아웃 (16MB)

- **총 플래시 용량:** 16MB (`0x1000000` = 16,777,216 Bytes)
- **가중치 파티션:** `0x200000`부터 14MB 할당 (Node 1 기준 약 13.83MB 소요)

[`esp_node/partitions.csv`](esp_node/partitions.csv) 구성:
```csv
# Name,   Type, SubType, Offset,   Size,     Flags
nvs,      data, nvs,     0x9000,   0x7000,
app0,     app,  factory, 0x10000,  0x1F0000,
model,    data, 0x40,    0x200000, 0xE00000,
```

- `nvs`: 시스템 설정 영역 (28KB)
- `app0`: 단일 펌웨어 바이너리 영역 (1.93MB)
- `model`: 노드별 가중치 바이너리(`nodeN.bin`) 영역 (14MB)

---

## 2. 1단계: 가중치 바이너리 플래싱 (esptool)

`.pt` 모델 가중치를 각 노드의 14MB 플래시 파티션(`0x200000`)에 직접 굽습니다.

### 간편 플래싱 도구 사용 (`flash_weights.py`)

1. **대화형 모드 (권장):**
   ```bash
   python ESP32-S3/flash_weights.py
   ```
   - 연결된 시리얼 포트(COMx) 자동 검색
   - 노드 번호 (1, 2, 3, 4) 또는 순차 플래시(A) 선택
   - `.pt`가 최신일 경우 자동으로 `Bert/export_weights.py`를 실행하여 `node1.bin` ~ `node4.bin` 생성 후 전송

2. **명령줄 지정 모드:**
   ```bash
   # 예: Node 1을 COM3에 플래시
   python ESP32-S3/flash_weights.py --port COM3 --node 1

   # 예: Node 2를 COM4에 플래시
   python ESP32-S3/flash_weights.py --port COM4 --node 2
   ```

---

## 3. 2단계: 노드 펌웨어 업로드 (Arduino IDE 2.x)

모든 노드(Node 1~4)는 동일한 통합 펌웨어([`esp_node/esp_node.ino`](esp_node/esp_node.ino))를 사용합니다. 플래시된 가중치 헤더를 통해 노드 번호 및 역할(임베딩, 트랜스포머, 헤드)을 자동 인식합니다.

### Arduino IDE 설정 (ESP32S3 Dev Module 기준)
1. **Board:** `ESP32S3 Dev Module`
2. **Flash Size:** `16MB (128Mb)`
3. **PSRAM:** `OPI PSRAM` (※ N16R8 모델 필수)
4. **Flash Mode:** `QIO 80MHz` (보드 사양에 따라 OPI 선택)
5. **Partition Scheme:** Custom (스케치 폴더 내 `partitions.csv` 자동 적용)
6. **Core Debug Level:** `Info`
7. **USB CDC On Boot:** `Enabled` (내장 USB JTAG/Serial 사용 시)

설정 완료 후 각 보드에 `esp_node.ino`를 업로드합니다.
부팅 후 시리얼 모니터(115200 bps)에 다음과 같은 로그가 출력되면 정상입니다:
```text
[I][BERT_NODE] Weight binary loaded from [model]: Node 1 (Layers 0-1, emb=1, head=0)
[I][BERT_NODE] ESP32-S3 BERT Node 1 Initialized
```
