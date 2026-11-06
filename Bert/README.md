# BERT-Medium 학습 순서

현재 학습 스크립트는 `medium_train_v2.py`입니다. RTX 3060이 있는 Sierrastdio의 Arch Linux WSL에서 실행하는 순서입니다.

## 최초 1회: WSL 작업 폴더와 GPU 환경 준비

1. Windows Terminal에서 **Arch WSL**을 엽니다. 프롬프트가 `sierrastdio@...`로 시작하는지 확인합니다.

2. Windows 프로젝트를 WSL 홈의 쓰기 가능한 작업 폴더로 복사합니다. 이 전체 복사는 최초 설정 때 한 번만 합니다.

   ```bash
   WIN_PROJECT=/mnt/c/Users/sierra/Documents/ESP32-S3-Bert-Kluster
   WSL_PROJECT="$HOME/projects/ESP32-S3-Bert-Kluster"
   mkdir -p "$WSL_PROJECT"
   cp -r "$WIN_PROJECT/." "$WSL_PROJECT/"
   chmod -R u+rwX "$WSL_PROJECT"
   cd "$WSL_PROJECT/Bert"
   ```

   `~/clone`은 읽기 전용일 수 있으므로 학습에는 `~/projects/...` 복사본을 사용합니다.

3. 같은 체크포인트의 `config.json`, `vocab.txt`, `pytorch_model.bin`이 `Bert` 폴더에 있는지 확인합니다. 가중치가 이전 WSL clone에만 있다면 복사합니다.

   ```bash
   cp ~/clone/ESP32-S3-Bert-Kluster/Bert/pytorch_model.bin .
   ls -lh config.json vocab.txt pytorch_model.bin
   ```

4. GPU용 가상환경과 패키지를 설치합니다. RTX 3060 환경은 CUDA 13.2 태그를 사용합니다.

   ```bash
   bash setup_arch.sh cu132
   ```

5. 학습할 때마다 새 WSL 터미널에서는 작업 폴더로 이동해 가상환경을 활성화합니다.

   ```bash
   cd "$HOME/projects/ESP32-S3-Bert-Kluster/Bert"
   source .venv/bin/activate
   ```

   프롬프트 앞에 `(.venv)`가 붙는지 확인하세요. GPU 설정도 확인합니다.

   ```bash
   python -c "import torch; print(torch.__version__); print(torch.cuda.is_available()); print(torch.cuda.get_device_name(0) if torch.cuda.is_available() else 'CUDA unavailable')"
   ```

   `2.15.0+cu132`, `True`, `NVIDIA GeForce RTX 3060 ...`이 나오면 GPU 준비가 끝난 것입니다. `bash run_train.sh`와 자동 회차 스크립트는 `.venv`를 직접 지정하므로 활성화가 빠져 있어도 해당 환경을 사용하지만, 위 절차를 따르면 `python`/`pip` 명령도 같은 환경을 가리킵니다.

## 최초 학습

체크포인트가 준비된 상태에서 가상환경이 활성화된 WSL 터미널에서 실행합니다.

```bash
bash run_train.sh
```

`[4단계]`와 epoch별 loss가 나오면 학습 중입니다. `학습된 모델을 불러왔습니다`만 나오면 저장 모델 평가만 실행한 것입니다. 결과는 `pruned/`에 저장됩니다. 평가 후 문장을 직접 입력하는 프롬프트가 나오며, 빈 줄을 입력하면 종료합니다.

## 이후 매 학습 회차: 보강 예시 추가 → 자동 학습

1. Windows 본 프로젝트의 [`hard_examples.tsv`](hard_examples.tsv)에 새 오답을 검토해 라벨을 붙입니다. 파일 형식과 라벨 기준은 아래 설명을 따릅니다.
2. WSL의 Arch 터미널을 열고 가상환경에 진입합니다.

   ```bash
   cd "$HOME/projects/ESP32-S3-Bert-Kluster/Bert"
   source .venv/bin/activate
   ```

3. 자동 회차 명령을 한 번 실행합니다.

   ```bash
   bash train_cycle.sh
   ```

   스크립트가 Windows에서 필요한 BERT 학습 파일만 동기화하고, GPU를 확인한 뒤 현재 모델에서 보강 학습합니다. `.venv`와 WSL 체크포인트는 복사 과정에서 보존합니다. 동일한 코드와 TSV로 다시 실행하면 중복 학습을 건너뜁니다. 새 라벨 예시가 있을 때 다음 회차가 진행됩니다.

각 회차는 원래 학습 예시와 `hard_examples.tsv` 예시를 섞어 2 epoch 학습합니다. 학습 전후에 고정 평가셋 점수를 비교합니다. 명령 아님 점수가 낮아지거나 `직접 명령 + 애매한 표현 + 2 × 명령 아님` 점수가 오르지 않으면 새 모델을 채택하지 않고 기존 체크포인트를 유지합니다. 채택할 때 이전 모델은 `pruned/model_finetuned_before_날짜시간.pt`로 백업합니다. 오답 보고서는 `pruned/last_unseen_errors.tsv`에 저장됩니다.

고정 평가 문장 자체를 학습 파일에 복사하면 안 됩니다. 오답과 비슷하지만 다른 문장을 만들어 라벨링하세요. 문장 의미에 대한 정답 라벨은 사람이 정해야 합니다. 고정 평가셋은 반복 회차의 튜닝용이므로 최종 성능 확인에는 별도의 미사용 문장도 준비해야 합니다.

## 보강 TSV 작성법

열 순서는 `text`, `device`, `action`, `amount`이며 **탭 문자**로 나눕니다. 헤더는 넣지 않습니다. 빈 줄과 `#`으로 시작하는 주석은 무시됩니다.

| 열 | 허용 값 | 뜻 |
|---|---|---|
| `text` | 영어 문장 | 학습할 입력 |
| `device` | `led`, `motor`, `fan`, `-` | 대상 장치. `none`이면 `-` |
| `action` | `on`, `off`, `change`, `none` | 의도한 동작 |
| `amount` | `0`~`7`, `-` | 목표 세기. `none`이면 `-` |

예를 들어 팬 바람을 줄여야 한다는 라벨은 `change`와 목표 레벨 `2`입니다. `2`는 현재 세기에서 두 단계 내리라는 뜻이 아니라, 목표를 레벨 2로 설정한다는 뜻입니다.

```text
the fan is blowing things off the table<TAB>fan<TAB>change<TAB>2
the fan is blowing everything around<TAB>fan<TAB>change<TAB>2
should i turn off the fan<TAB>-<TAB>none<TAB>-
```

위 예시에서 `<TAB>`은 설명용 표시입니다. 파일에는 그 자리에 실제 탭을 입력하세요. `on`은 켠 뒤 목표 세기(보통 일반 켜기는 `7`), `off`는 `0`, `change`는 목표 레벨을 씁니다. 학습 문장을 `vocab.txt`에 넣지 마세요. `vocab.txt`는 토큰 ID와 임베딩 행을 맞추는 모델 어휘이며, 스크립트가 학습 데이터에서 축소 어휘를 생성합니다.

## 평가만 실행

현재 저장된 모델을 불러와 고정 문장을 평가하고 직접 입력해 보려면:

```bash
bash run_train.sh
```

자동 회차 스크립트는 평가 후 입력을 기다리지 않고 종료합니다. 같은 데이터로 다시 시도해야 하는 특별한 경우에만 `rm pruned/.last_train_cycle_signature`로 중복 실행 기록을 지우세요.
