# ESP32-S3 BERT 클러스터 업로드 & 플래싱 상세 가이드

이 문서는 **ESP32-S3 N16R8 (16MB Flash / 8MB Octal PSRAM)** 보드 4개에 가중치 바이너리와 펌웨어를 업로드하는 전체 과정을 처음부터 끝까지 단계별로 안내합니다.

---

## 0. 작업 개요 및 핵심 원리

```text
[PC] .pt 체크포인트 (105MB)
  │
  ├─ export_weights.py ──> node1.bin (13.8MB), node2.bin (12.0MB), node3.bin (12.0MB), node4.bin (12.0MB)
  │
  ├─ Step 1: esptool 로 각 보드의 플래시 오프셋 0x200000 (14MB 모델 파티션)에 각 노드 바이너리 기록
  │
  └─ Step 2: Arduino IDE 로 동일한 통합 펌웨어(esp_node.ino)를 4개 보드 모두에 업로드 (0x10000)
```

> [!IMPORTANT]
> **단일 통합 펌웨어 원리:**
> 노드 1, 2, 3, 4용 코드를 따로 수정할 필요가 없습니다. 펌웨어(`esp_node.ino`)는 4개 보드에 **동일하게 그대로** 업로드하며, 부팅 시 플래시(`0x200000`)에 기록된 가중치 헤더를 읽어 자신이 몇 번 노드인지, 임베딩/헤드 역할을 맡는지 스스로 판단합니다.

---

## 1. 사전 준비물 및 환경 점검

### 준비물
1. **ESP32-S3 N16R8 개발 보드 4개**
   - 보드 헷갈림 방지를 위해 포스트잇이나 견출지로 **Node 1, Node 2, Node 3, Node 4** 라벨을 붙여두는 것을 권장합니다.
2. **USB-C 데이터 케이블** (단순 충전 전용이 아닌 데이터 통신 가능한 케이블)
3. **학습 완료된 모델 파일**: `Bert/pruned/model_finetuned.pt`

### PC 환경 점검
PowerShell 또는 터미널을 열고 필요한 패키지가 준비되어 있는지 확인합니다:
```powershell
python -c "import torch, esptool, serial; print('Environment OK!')"
```
*(에러 없이 `Environment OK!`가 출력되면 준비 완료된 상태입니다.)*

---

## 2. [1단계] 각 노드별 가중치 플래싱 (esptool)

각 ESP32-S3 보드의 플래시 14MB 파티션(`0x200000`)에 `node1.bin` ~ `node4.bin`을 구워줍니다.

### 방법 A. 대화형 플래셔 실행 (가장 추천)

1. **Node 1로 사용할 첫 번째 보드**를 PC의 USB 포트에 연결합니다.
2. 아래 명령을 실행합니다:
   ```powershell
   python ESP32-S3/flash_weights.py
   ```
3. 화면 안내에 따라 진행합니다:
   ```text
   ============================================================
     ESP32-S3 BERT 클러스터 가중치 플래셔 (N16R8 16MB Flash)
   ============================================================

   연결된 시리얼 포트 목록:
     [1] COM3 - USB-Enhanced-SERIAL CH343 (COM3)
     [2] 직접 입력

   포트를 선택하세요 [1-2]: 1

   플래시할 노드를 선택하세요:
     [1] Node 1 (임베딩 + 레이어 0~1)
     [2] Node 2 (레이어 2~3)
     [3] Node 3 (레이어 4~5)
     [4] Node 4 (레이어 6~7 + 분류 헤드)
     [A] All (1 -> 2 -> 3 -> 4 차례대로 보드 교체 안내 받으며 플래시)

   노드 선택 [1/2/3/4/A]: 1
   ```
4. 가중치 변환 및 플래싱이 자동으로 진행됩니다.
   - 처음 실행 시 `.pt` 파일로부터 `node1.bin` ~ `node4.bin`을 자동 생성합니다.
   - 전송 속도 921600 bps로 약 15~25초 내에 플래싱이 완료되며 `[v] Node 1 플래싱 성공!`이 뜹니다.
5. 첫 번째 보드를 분리하고, **Node 2, Node 3, Node 4 보드**도 케이블을 연결해가며 위 과정을 반복합니다 (노드 번호만 2, 3, 4로 변경).
   *(메뉴에서 `A`를 선택하면 보드를 바꿀 때마다 엔터 키를 눌러 4개를 연속으로 플래시할 수 있습니다.)*

### 방법 B. 명령줄로 직접 지정하여 플래시하는 경우
포트 번호(예: COM3)를 알고 있다면 명령어로 즉시 플래시할 수 있습니다:
```powershell
# Node 1 플래시
python ESP32-S3/flash_weights.py --port COM3 --node 1

# Node 2 플래시 (보드 교체 후)
python ESP32-S3/flash_weights.py --port COM3 --node 2

# Node 3 플래시 (보드 교체 후)
python ESP32-S3/flash_weights.py --port COM3 --node 3

# Node 4 플래시 (보드 교체 후)
python ESP32-S3/flash_weights.py --port COM3 --node 4
```

---

## 3. [2단계] Arduino IDE 2.x 설정 및 펌웨어 업로드

가중치를 플래시한 4개 보드에 단일 통합 펌웨어를 업로드합니다.

### 1) 프로젝트 열기
- Arduino IDE 2.x를 실행합니다.
- `파일(File)` > `열기(Open)`를 클릭하고 아래 파일을 선택합니다:
  - 경로: `C:\Users\sierra\Documents\ESP32-S3-Bert-Kluster\ESP32-S3\esp_node\esp_node.ino`
- `esp_node` 스케치 폴더 안에 [`partitions.csv`](esp_node/partitions.csv) 파일이 함께 위치하고 있는지 탐색기에서 확인합니다. (Arduino IDE가 스케치 폴더의 `partitions.csv`를 자동으로 커스텀 파티션으로 적용합니다.)

### 2) 보드 및 환경 설정 (Tools 메뉴)
Arduino IDE 상단 메뉴의 **`도구(Tools)`** 항목을 아래 표와 정확히 일치하도록 설정합니다:

| 메뉴 항목 (Tools) | 권장 설정값 | 설명 |
| :--- | :--- | :--- |
| **Board (보드)** | `ESP32S3 Dev Module` | ESP32-S3 공통 개발 모듈 |
| **Port (포트)** | 연결된 `COMx` 포트 | 현재 꽂혀 있는 보드의 포트 |
| **Flash Size** | **`16MB (128Mb)`** | **반드시 16MB 선택 (N16R8)** |
| **PSRAM** | **`OPI PSRAM`** | **N16R8의 8MB Octal PSRAM 활성화 (필수)** |
| **Partition Scheme** | **`Custom`** 또는 `Default` | 스케치 폴더의 `partitions.csv` 자동 적용 |
| **Flash Mode** | `QIO 80MHz` | 쿼드 SPI 80MHz 모드 |
| **Upload Speed** | `921600` | 고속 업로드 |
| **USB CDC On Boot** | `Enabled` | 보드 내장 USB CDC 포트 활성화 |
| **Core Debug Level** | `Info` | 부팅 로그 확인용 |

> [!CAUTION]
> **PSRAM 설정 주의:**
> N16R8 보드는 Octal PSRAM을 사용하므로, `PSRAM` 설정이 `QSPI PSRAM`으로 되어 있으면 부팅 시 충돌(Guru Meditation Error / Crash Dump)이 발생합니다. 반드시 **`OPI PSRAM`**으로 설정해 주세요.

### 3) 펌웨어 업로드 실행
1. 첫 번째 보드를 연결한 상태에서 좌측 상단의 **업로드(`→` 버튼)**를 클릭합니다.
2. 컴파일 후 자동으로 보드에 업로드됩니다.
3. 업로드가 완료되면 다음 보드를 연결하고 포트 번호 확인 후 다시 **업로드 버튼**을 누릅니다.
4. **4개 보드(Node 1~4) 모두 동일하게 업로드를 진행합니다.**

---

## 4. [3단계] 정상 동작 검증 (시리얼 모니터 확인)

각 보드에 가중치와 펌웨어가 정상적으로 기록되었는지 검증합니다.

1. Arduino IDE에서 **`시리얼 모니터`** (또는 PuTTY, TeraTerm 등)를 엽니다.
2. 통신 속도를 **`115200 baud`**로 설정합니다.
3. 보드의 **`RST` (또는 `EN`) 버튼**을 눌러 보드를 재부팅합니다.
4. 아래와 같이 자신이 플래시된 가중치에 맞춰 올바른 노드 번호로 부팅되는지 확인합니다:

#### [Node 1 정상 부팅 로그]
```text
[I][BERT_NODE] Weight binary loaded from [model]: Node 1 (Layers 0-1, emb=1, head=0)
[I][BERT_NODE] ESP32-S3 BERT Node 1 Initialized
```

#### [Node 2 정상 부팅 로그]
```text
[I][BERT_NODE] Weight binary loaded from [model]: Node 2 (Layers 2-3, emb=0, head=0)
[I][BERT_NODE] ESP32-S3 BERT Node 2 Initialized
```

#### [Node 3 정상 부팅 로그]
```text
[I][BERT_NODE] Weight binary loaded from [model]: Node 3 (Layers 4-5, emb=0, head=0)
[I][BERT_NODE] ESP32-S3 BERT Node 3 Initialized
```

#### [Node 4 정상 부팅 로그]
```text
[I][BERT_NODE] Weight binary loaded from [model]: Node 4 (Layers 6-7, emb=0, head=1)
[I][BERT_NODE] ESP32-S3 BERT Node 4 Initialized
```

4개 보드 모두 위와 같이 각각 `Node 1`, `Node 2`, `Node 3`, `Node 4`로 정상 인식되면 하드웨어 업로드 준비가 완벽히 끝난 상태입니다!

---

## 5. 문제 해결 (FAQ & Troubleshooting)

### Q1. `No weight binary found. Defaulting to Node 1` 경고가 뜹니다.
- **원인:** 플래시 메모리 `0x200000` 주소에 `nodeN.bin` 파일이 구워지지 않았거나 다른 주소에 플래시된 경우입니다.
- **해결:** `flash_weights.py` 스크립트를 사용해 해당 노드의 가중치를 다시 플래시하세요.

### Q2. 업로드 중 `Failed to connect to ESP32-S3: Timed out waiting for packet header` 에러가 발생합니다.
- **원인:** 보드가 자동으로 다운로드(부트로더) 모드로 진입하지 못한 경우입니다.
- **해결:** 보드의 **`BOOT` 버튼을 누르고 있는 상태에서 `RST(EN)` 버튼을 한 번 눌렀다 뗀 후, BOOT 버튼을 놓습니다.** 그 후 다시 업로드를 시도하세요.

### Q3. 시리얼 포트 접근 권한 에러 (`Access is denied` / `Could not open port`)
- **원인:** 시리얼 모니터나 다른 프로그램(Cura, 다른 IDE 등)이 해당 COM 포트를 점유하고 있는 상태입니다.
- **해결:** 열려 있는 시리얼 모니터 창을 닫거나 연결을 해제한 후 다시 시도하세요.
