# Arch WSL 최초 환경 준비

이 문서는 BERT 학습 환경을 **처음 한 번** 준비할 때 사용합니다. 매 학습 회차의 실행 순서는 [`README.md`](README.md)를 따르세요.

## 준비 내용

- Windows 프로젝트를 Arch WSL 사용자 홈으로 복사
- 원본 BERT 설정·어휘·가중치 파일 준비
- RTX 3060용 CUDA PyTorch 및 Python 가상환경 설치
- CUDA 사용 가능 여부 확인

모든 명령은 **Arch WSL 터미널**에서 실행합니다.

## 1. Windows 저장소를 WSL로 복사

```bash
WIN_PROJECT=/mnt/c/Users/sierra/Documents/ESP32-S3-Bert-Kluster
WSL_PROJECT="$HOME/projects/ESP32-S3-Bert-Kluster"
bash "$WIN_PROJECT/Bert/sync_to_wsl.sh"
cd "$WSL_PROJECT/Bert"
```

동기화는 같은 경로의 대상 파일을 덮어씁니다. `.git`, WSL 전용 `Bert/.venv`, Pico 빌드 결과만 제외하고 `Bert/pruned/`와 모델 파일을 포함한 나머지 저장소 파일을 복사합니다. 대상 쪽에만 있는 파일은 삭제하지 않습니다.

## 2. 원본 BERT 파일 확인

필요한 파일은 `config.json`, `vocab.txt`, 그리고 원본 가중치 하나입니다: `pytorch_model.bin`, `model.safetensors`, `pytorch_model.safetensors`.

Windows 저장소에 원본 가중치가 없고 기존 clone에만 있을 때는 WSL의 Bert 폴더에서 복사합니다.

```bash
if [[ ! -f pytorch_model.bin ]]; then
  cp ~/clone/ESP32-S3-Bert-Kluster/Bert/pytorch_model.bin .
fi
ls -lh config.json vocab.txt pytorch_model.bin
```

다른 safetensors 가중치 파일을 사용하는 경우 위 `cp`와 `ls` 명령의 파일명을 실제 파일명으로 바꾸세요. 최초 학습 후 WSL→Windows 전체 저장소 동기화를 실행하면 clone에서 복사한 원본 파일도 Windows 프로젝트로 들어옵니다.

## 3. RTX 3060용 Python 가상환경 설치

최초 한 번 실행합니다.

```bash
bash setup_arch.sh cu132
```

이 스크립트가 `Bert/.venv`를 만들고 CUDA 13.2용 PyTorch와 Transformers를 설치합니다. WSL의 Linux 가상환경이므로 Windows Python 환경을 재사용하지 않습니다.

## 4. 가상환경 활성화 및 CUDA 확인

```bash
source .venv/bin/activate
python -c "import torch; print(torch.__version__); print(torch.cuda.is_available()); print(torch.cuda.get_device_name(0) if torch.cuda.is_available() else 'CUDA unavailable')"
```

RTX 3060 환경에서는 `2.15.0+cu132`, `True`, `NVIDIA GeForce RTX 3060 ...`이 표시되어야 합니다. 새 WSL 터미널을 열 때는 프로젝트를 동기화하고 `cd "$WSL_PROJECT/Bert"` 한 뒤 가상환경을 다시 활성화합니다. `run_train.sh`는 `.venv` Python을 직접 호출하므로 활성화는 편의를 위한 절차입니다.

환경이 준비되면 최초 학습과 보강 학습은 [`README.md`](README.md)의 단계별 절차를 따르세요.
