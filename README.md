# ESP32-S3 BERT Cluster

ai: <https://1drv.ms/u/c/09a71dc2aa7bfa58/IQCsVi3dmqDqQ5wyhk7g-aeCAb0ddoUg3yvbtwGJx5Z_ym8?e=rWUAFq>
Project notes and firmware examples for a Pico 1 controller and ESP32-S3
inference nodes. See [the project brief](multi_esp_project_brief.md) for the
system design and maturity labels.

---

## Windows Arch WSL training env

Claude/Codex와 Git은 Windows 본 프로젝트에서 사용하고, 학습은 Arch WSL의 Linux 복사본에서 합니다. **PyTorch/CUDA 설치, 원본 BERT 파일 준비 등 최초 환경 설정은** [`Bert/README_WSL_SETUP.md`](Bert/README_WSL_SETUP.md)에 따릅니다. `hard_examples.tsv` 작성법은 [`Bert/README_HARD_EXAMPLES_GUIDE.md`](Bert/README_HARD_EXAMPLES_GUIDE.md)에 있습니다. 아래는 환경 설치 이후 학습에 사용하는 명령입니다.

```bash
WIN_PROJECT=/mnt/c/Users/sierra/Documents/ESP32-S3-Bert-Kluster
WSL_PROJECT="$HOME/projects/ESP32-S3-Bert-Kluster"
bash "$WIN_PROJECT/Bert/sync_to_wsl.sh"
cd "$WSL_PROJECT/Bert"
source .venv/bin/activate
python -c "import torch; print(torch.cuda.is_available(), torch.cuda.get_device_name(0) if torch.cuda.is_available() else 'CUDA unavailable')"
bash run_train.sh --no-interactive # 최초 학습: pruned 체크포인트가 없을 때
bash "$WIN_PROJECT/Bert/sync_from_wsl.sh" # WSL의 Bert/pruned 결과만 Windows로 가져오기
```

`pruned/model_finetuned.pt`가 이미 있으면 `bash run_train.sh`는 기본적으로 학습하지 않고 모델을 불러와 평가합니다. 보강 학습은 아래 자동 회차 명령을 사용합니다.

최초 설정 후에는 Windows에서 코드와 `Bert/hard_examples.tsv`를 수정·저장하고, WSL에서 아래를 실행해 한 학습 회차를 돌립니다.

```bash
cd "$HOME/projects/ESP32-S3-Bert-Kluster/Bert"
source .venv/bin/activate
bash /mnt/c/Users/sierra/Documents/ESP32-S3-Bert-Kluster/Bert/train_cycle.sh
```

`sync_to_wsl.sh`는 Windows 저장소를 WSL로 복사하고, `sync_from_wsl.sh`는 WSL의 `Bert/pruned/`만 Windows로 가져옵니다. 따라서 결과 복사가 Windows의 README, TSV, 소스 코드를 덮어쓰지 않습니다. Git에서는 `Bert/pruned/*.pt` 체크포인트만 제외하고 학습 로그와 평가 자료는 공유합니다. GitHub push는 Windows 저장소에서만 합니다. 상세 절차는 [`Bert/README_MAIN.md`](Bert/README_MAIN.md)를 참고하세요.
