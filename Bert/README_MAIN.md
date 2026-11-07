# BERT-Medium 개발·학습 환경

현재 학습 스크립트는 `medium_train_v2.py`입니다. Claude/Codex와 Git 작업은 Windows 프로젝트에서 하고, 모델 학습은 Arch Linux WSL의 GPU에서 실행합니다.

```text
Windows project (source code, TSV, Git)
        │  sync_to_wsl.sh
        ▼
WSL project (Python .venv, base weights, pruned, GPU training)
        │  sync_from_wsl.sh
        ▼
Windows project (학습 결과 검토 및 Git 작업)
```

두 명령은 각각 저장소 파일을 한 방향으로 복사합니다. 같은 경로의 파일은 덮어쓰지만, 대상 쪽에만 있는 파일을 삭제하지는 않습니다. `.git`, WSL 전용 `Bert/.venv`, Pico 빌드 결과만 복사에서 제외하고, `Bert/pruned/`와 원본 모델 가중치를 포함한 나머지 파일은 복사합니다. Git에서는 `.gitignore`에 따라 `Bert/pruned/*.pt` 체크포인트만 제외하고 학습 로그·오답 TSV·설정·어휘는 공유합니다. Git 작업은 Windows 저장소에서 합니다.

## 개발·학습 절차 (최대 5단계)

최초 PyTorch/CUDA 설치, 원본 모델 파일 준비, 가상환경 설정은 별도 문서인 [`README_WSL_SETUP.md`](README_WSL_SETUP.md)에 있습니다. 아래 학습 절차는 그 환경이 이미 준비됐다고 가정합니다. 모든 학습 명령은 **Arch WSL 터미널**에서 실행합니다.

1. **Arch WSL을 열고 매번 학습 전에 Windows 저장소를 WSL로 동기화한 뒤 가상환경에 들어갑니다.**

   ```bash
   WIN_PROJECT=/mnt/c/Users/sierra/Documents/ESP32-S3-Bert-Kluster
   WSL_PROJECT="$HOME/projects/ESP32-S3-Bert-Kluster"
   bash "$WIN_PROJECT/Bert/sync_to_wsl.sh"
   cd "$WSL_PROJECT/Bert"
   source .venv/bin/activate
   python -c "import torch; print(torch.cuda.is_available()); print(torch.cuda.get_device_name(0) if torch.cuda.is_available() else 'CUDA unavailable')"
   ```

   GPU 확인에서 `True`와 RTX 3060 이름이 나와야 합니다. 동기화는 저장소 파일을 복사하고 같은 경로의 WSL 파일을 덮어씁니다. `.git`, WSL `.venv`, Pico 빌드 결과만 제외합니다.

2. **최초 학습은 제어 모델 체크포인트가 아직 없을 때 실행합니다.** 원본 BERT 가중치에서 시작해 새 제어 모델을 학습합니다.

   ```bash
   bash run_train.sh --no-interactive
   ```

   학습이 실제로 돌면 `[4단계] 학습 데이터 ...`와 `epoch ... loss ...`가 출력되고 `pruned/model_finetuned.pt`가 생성됩니다. 끝의 `학습된 모델을 불러왔습니다: ...`는 학습 시작 문구가 아니라 평가 전 모델 로드 문구입니다. **이 문구만 나오면 아래 보강 학습도 아니고 최초 학습도 아니며, 저장 모델 평가만 한 것**입니다. 그 경우 `pruned/model_finetuned.pt`가 이미 있어서 코드가 평가 분기로 들어간 것입니다.

   체크포인트가 이미 있지만 원본 가중치부터 새로 학습하려는 경우에만 다음 명령으로 백업한 뒤 제거합니다. 백업본은 `pruned/` 안에 보존됩니다.

   ```bash
   cp -a pruned/model_finetuned.pt "pruned/model_finetuned_before_fresh_$(date +%Y%m%d_%H%M%S).pt"
   rm pruned/model_finetuned.pt
   bash run_train.sh --no-interactive
   ```

   최초 학습이 끝나면 WSL 저장소와 새 모델 결과를 Windows로 가져옵니다.

   ```bash
   bash "$WIN_PROJECT/Bert/sync_from_wsl.sh"
   ```

3. **보강 학습 전에 TSV를 편집합니다.** Windows 프로젝트의 `Bert/hard_examples.tsv`를 열어 새 예시를 입력하고 저장합니다. 편집 위치, 탭 입력 방법, 라벨 기준, 작성 예시는 별도 문서 [`README_HARD_EXAMPLES_GUIDE.md`](README_HARD_EXAMPLES_GUIDE.md)에 있습니다.

4. **WSL에서 자동 보강 회차를 실행하고 Windows에서 결과를 확인합니다.** 이 스크립트가 Windows 저장소를 WSL에 동기화하고, 보강 학습·비교·채택/보류 후 WSL 저장소를 Windows에 동기화합니다.

   ```bash
   WIN_PROJECT=/mnt/c/Users/sierra/Documents/ESP32-S3-Bert-Kluster
   bash "$WIN_PROJECT/Bert/train_cycle.sh"
   ```

   출력에 `입력 코드와 hard_examples.tsv가 이전 회차와 같습니다. 학습을 건너뜁니다.`가 나오면 Windows TSV가 저장됐는지, 올바른 Windows 파일을 수정했는지 확인합니다. 입력이 바뀐 회차만 학습합니다.

   Windows `Bert/pruned/last_train_cycle.log`에서 보강 전후 점수와 `결과 채택` 또는 `결과 보류`를 확인하고, `Bert/latest_unseen_errors.tsv`에서 남은 고정 평가 오답을 확인합니다. `결과 보류`면 기존 체크포인트가 유지된 것입니다.

### 저장소 동기화 명령

Windows 프로젝트에서 WSL 프로젝트로 저장소 파일을 복사합니다. Windows 파일이 같은 경로의 WSL 파일을 덮어씁니다.

```bash
bash "$WIN_PROJECT/Bert/sync_to_wsl.sh"
```

WSL에서 Windows 프로젝트로 저장소 파일과 학습 결과를 복사합니다. WSL 파일이 같은 경로의 Windows 파일을 덮어씁니다.

```bash
bash /mnt/c/Users/sierra/Documents/ESP32-S3-Bert-Kluster/Bert/sync_from_wsl.sh
```

WSL에서 코드를 수정한 경우도 `sync_from_wsl.sh`가 Windows 쪽 같은 경로를 덮어쓰므로, 저장소 전체 동기화 전에 원하는 변경본이 있는 방향을 확인합니다. `.git`, `.venv`, Pico 빌드 결과는 두 방향 모두 제외됩니다. Git 작업과 GitHub push는 Windows 프로젝트에서 합니다.

## 학습과 자동 채택 기준

`bash run_train.sh`는 저장 모델이 있으면 옵션 없이 실행할 때 평가만 합니다. 최초 학습은 저장 모델이 없을 때 `bash run_train.sh --no-interactive`로 실행합니다. 보강 학습은 `train_cycle.sh`가 `run_train.sh --iterate --no-interactive`로 실행합니다. 기존 학습 예시와 TSV 예시를 섞어 보강 학습하고, 고정 평가셋으로 이전 모델과 비교합니다. `명령 아님` 점수가 내려가거나 `직접 명령 + 애매한 표현 + 2 × 명령 아님` 점수가 개선되지 않으면 기존 체크포인트를 유지합니다. 채택 시 이전 체크포인트는 `pruned/model_finetuned_before_날짜시간.pt`로 백업됩니다. 자동 회차는 완료 시 저장소 전체 동기화도 수행합니다.

평가 오답은 `pruned/last_unseen_errors.tsv`, 회차 로그는 `pruned/last_train_cycle.log`에 기록됩니다. 이 고정 평가셋은 반복 튜닝용이므로 최종 성능 확인에는 학습과 튜닝에 사용하지 않은 별도 문장도 준비하세요. 평가 문장을 그대로 학습 TSV에 복사하면 안 됩니다. 의미가 비슷한 새 문장을 만들고 사람이 정답 라벨을 정합니다.

### epoch 로그 읽는 법

이 한 줄은 [`medium_train_v2.py`](medium_train_v2.py)의 `train()` 함수가 각 epoch 끝에서 출력합니다. 계산 기준은 다음과 같습니다.

| 출력 | 실제 의미 |
| --- | --- |
| `epoch 1/4` | 현재 몇 번째 학습 회차(epoch)인지. `4`는 전체 epoch 수이며 설정과 실행 모드에 따라 달라집니다. |
| `loss 0.800` | 그 epoch의 학습 배치마다 계산한 세 분류기(device/action/amount)의 교차 엔트로피 손실 합계를 배치 수로 나눈 값입니다. 낮아지면 학습 예시에 더 맞아간다는 뜻이지, 정답률 그 자체는 아닙니다. |
| `test acc 90.0%` | 자동 생성 데이터 중 학습에 쓰지 않은 약 15% 분할에서의 정확도입니다. `accuracy()` 기준으로 명령은 장치·동작·양이 전부 정확해야 맞고, `none`은 동작이 `none`이면 맞습니다. |
| `처음 보는 문장 acc 65.0%` | 코드에 고정해 둔 `UNSEEN` 84문장에 대한 정확도입니다. 직접 명령 34개, 애매한 표현 25개, 명령 아님 25개입니다. 이 epoch 로그에서는 명령의 장치·동작·양이 모두 정확해야 맞습니다. |

위 표의 숫자는 각 항목의 뜻을 보여주기 위한 형식 예시입니다. 로그에 실제로 표시된 값을 확인하세요. 특히 epoch 로그의 `처음 보는 문장 acc`와 학습 종료 뒤 요약되는 `처음 보는 문장 정답률`은 채점 기준이 다릅니다. 종료 요약은 양이 정답에서 ±1 이내면 맞는 것으로 보고(`off`는 양을 무시), 명령 아님은 `none` 동작만 확인합니다. 그래서 둘의 퍼센트가 달라도 모순은 아닙니다.

`test acc`는 생성된 문장 분할의 점수라 유사한 문장 패턴을 학습 데이터와 공유할 수 있습니다. 실제 새로운 표현에 대한 성능 판단은 고정 `UNSEEN` 결과와 별도로 보유한 새 문장 평가를 함께 보세요. 보강 학습의 채택/보류 판단은 epoch의 `test acc`만으로 하지 않고, 위의 종료 요약 점수에 기반합니다.

## TSV 편집

`hard_examples.tsv`의 입력 규칙은 [`README_HARD_EXAMPLES_GUIDE.md`](README_HARD_EXAMPLES_GUIDE.md)를 참고하세요. 학습 예문을 `vocab.txt`에 추가하지 않습니다. 학습 코드가 학습 문장에 필요한 토큰을 어휘에 반영합니다.
