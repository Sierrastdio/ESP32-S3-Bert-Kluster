# BERT-Medium 개발·학습 환경

현재 학습 스크립트는 `medium_train_v2.py`입니다. Claude/Codex와 Git 작업은 Windows 프로젝트에서 하고, 모델 학습은 Arch Linux WSL의 GPU에서 실행합니다.

```text
Windows project (source code, TSV, Git)
        │  sync_to_wsl.sh
        ▼
WSL project (Python .venv, base weights, pruned, GPU training)
        │  sync_from_wsl.sh
        ▼
Windows project/Bert/pruned (학습 결과 검토 및 필요 시 Git 작업)
```

동기화는 병합이 아니라 덮어쓰기 복사입니다. Windows가 코드와 학습 TSV의 기준본이며, WSL만 `.venv`와 학습 중인 `pruned/`를 보유합니다. 기존 WSL Git 메타데이터는 `.git.disabled.날짜시간`으로 보관해 GitHub 작업이 Windows 저장소에서만 이뤄지게 합니다.

## 최초 설정: 시간순

1. Windows Terminal에서 **Arch WSL**을 엽니다. 이후 모든 명령은 WSL 터미널에서 실행합니다.

2. Windows 프로젝트를 WSL 사용자 홈으로 복사합니다. 동기화 스크립트가 `.git`, `.venv`, `Bert/pruned`, Pico 빌드 폴더를 복사에서 제외하므로 WSL 학습 결과와 환경을 보존합니다.

   ```bash
   WIN_PROJECT=/mnt/c/Users/sierra/Documents/ESP32-S3-Bert-Kluster
   WSL_PROJECT="$HOME/projects/ESP32-S3-Bert-Kluster"
   bash "$WIN_PROJECT/Bert/sync_to_wsl.sh"
   cd "$WSL_PROJECT/Bert"
   ```

3. 원본 BERT 체크포인트 가중치가 WSL `Bert` 폴더에 있는지 확인합니다. Windows 프로젝트에 없고 기존 clone에만 있을 때 한 번 복사합니다.

   ```bash
   if [[ ! -f pytorch_model.bin ]]; then
     cp ~/clone/ESP32-S3-Bert-Kluster/Bert/pytorch_model.bin .
   fi
   ls -lh config.json vocab.txt pytorch_model.bin
   ```

   기존 clone은 이 체크포인트를 가져오는 용도로만 필요합니다. WSL 작업 폴더에 `pytorch_model.bin`이 있고 다른 프로젝트에서 clone을 쓰지 않는다면 clone은 학습 실행에 필요하지 않습니다.

4. RTX 3060용 Python 가상환경과 패키지를 설치합니다. 최초 한 번만 실행합니다.

   ```bash
   bash setup_arch.sh cu132
   ```

5. 가상환경을 활성화하고 CUDA가 보이는지 확인합니다.

   ```bash
   source .venv/bin/activate
   python -c "import torch; print(torch.__version__); print(torch.cuda.is_available()); print(torch.cuda.get_device_name(0) if torch.cuda.is_available() else 'CUDA unavailable')"
   ```

   `2.15.0+cu132`, `True`, `NVIDIA GeForce RTX 3060 ...`이 나와야 합니다. 새 WSL 터미널을 열 때마다 작업 폴더로 이동한 뒤 `source .venv/bin/activate`를 실행합니다. 학습 셸 스크립트는 `.venv` Python을 직접 지정하므로 활성화가 없어도 학습은 해당 가상환경으로 실행됩니다.

6. 처음 학습하고 결과를 Windows로 가져옵니다.

   ```bash
   bash run_train.sh
   bash "$WIN_PROJECT/Bert/sync_from_wsl.sh"
   ```

   `[4단계]`와 epoch 로그가 나오면 학습 중입니다. `학습된 모델을 불러왔습니다`만 나오면 저장 모델 평가입니다. 평가 후 문장 입력 프롬프트가 나오면 빈 줄로 종료합니다. 학습 결과 `pruned/`는 WSL에 생성되고, 두 번째 명령이 Windows `Bert/pruned/`로 복사합니다.

## 이후 작업: 시간순 자동 회차

1. Windows의 `Bert/hard_examples.tsv`에서 새 오답 예시와 정답 라벨을 추가합니다. TSV 규칙은 아래를 참고합니다.
2. Windows에서 코드와 데이터 파일을 저장합니다.
3. Arch WSL을 열고 아래를 실행합니다. WSL 작업 폴더에서 복사본 스크립트를 부르는 대신 Windows 기준본의 회차 스크립트를 실행합니다.

   ```bash
   cd "$HOME/projects/ESP32-S3-Bert-Kluster/Bert"
   source .venv/bin/activate
   bash /mnt/c/Users/sierra/Documents/ESP32-S3-Bert-Kluster/Bert/train_cycle.sh
   ```

한 회차 스크립트가 Windows 코드/TSV를 WSL로 동기화하고, GPU를 확인한 뒤 현재 체크포인트에서 보강 학습·평가·자동 채택/보류를 진행합니다. 마지막에 `pruned/` 결과를 Windows로 복사합니다. 동일한 입력으로 다시 실행하면 학습을 건너뜁니다. `.venv`와 WSL의 `pruned/`는 Windows→WSL 동기화에서 제외되어 유지됩니다.

동기화만 하고 학습하지 않으려면:

```bash
bash /mnt/c/Users/sierra/Documents/ESP32-S3-Bert-Kluster/Bert/train_cycle.sh --sync-only
```

수동으로 `bash run_train.sh`를 실행한 경우 결과 복사는 별도로 합니다.

```bash
bash /mnt/c/Users/sierra/Documents/ESP32-S3-Bert-Kluster/Bert/sync_from_wsl.sh
```

Git 작업과 GitHub push는 Windows 프로젝트에서만 합니다. `sync_from_wsl.sh`는 WSL의 `pruned/` 결과만 Windows로 복사하며 WSL 소스 파일이나 `.git`은 가져오지 않습니다.

## 학습과 자동 채택 기준

`bash run_train.sh`는 저장 모델이 있으면 평가만 합니다. 자동 보강은 `train_cycle.sh`가 `run_train.sh --iterate --no-interactive`로 실행합니다. 기존 학습 예시와 TSV 예시를 섞어 2 epoch 학습하고, 고정 평가셋으로 이전 모델과 비교합니다. `명령 아님` 점수가 내려가거나 `직접 명령 + 애매한 표현 + 2 × 명령 아님` 점수가 개선되지 않으면 기존 체크포인트를 유지합니다. 채택 시 이전 체크포인트는 `pruned/model_finetuned_before_날짜시간.pt`로 백업됩니다.

평가 오답은 `pruned/last_unseen_errors.tsv`, 회차 로그는 `pruned/last_train_cycle.log`에 기록됩니다. 이 고정 평가셋은 반복 튜닝용이므로 최종 성능 확인에는 학습과 튜닝에 사용하지 않은 별도 문장도 준비하세요. 평가 문장을 그대로 학습 TSV에 복사하면 안 됩니다. 의미가 비슷한 새 문장을 만들고 사람이 정답 라벨을 정합니다.

## `hard_examples.tsv` 작성법

열 순서는 `text`, `device`, `action`, `amount`이며 탭 문자로 나눕니다. 헤더는 쓰지 않습니다. 빈 줄과 `#`으로 시작하는 주석은 무시됩니다.

| 열 | 허용 값 | 뜻 |
| --- | --- | --- |
| `text` | 영어 문장 | 입력 문장 |
| `device` | `led`, `motor`, `fan`, `-` | 장치. `none`이면 `-` |
| `action` | `on`, `off`, `change`, `none` | 의도한 동작 |
| `amount` | `0`~`7`, `-` | 목표 세기. `none`이면 `-` |

예를 들어 팬의 바람을 줄여야 한다는 뜻은 `fan/change/2`입니다. `2`는 두 단계 내리라는 뜻이 아니라 목표 세기를 2로 맞추라는 뜻입니다.

```text
the fan is blowing things off the table<TAB>fan<TAB>change<TAB>2
should i turn off the fan<TAB>-<TAB>none<TAB>-
```

`<TAB>`은 설명용 표기입니다. 실제 파일에는 그 자리에 탭 문자를 입력합니다. `on`은 켠 뒤 목표 세기(일반적으로 7), `off`는 0, `change`는 변경 후 목표 레벨을 적습니다. 학습 문장을 `vocab.txt`에 넣지 마세요. 토큰 어휘와 임베딩 행은 학습 스크립트가 함께 구성합니다.
