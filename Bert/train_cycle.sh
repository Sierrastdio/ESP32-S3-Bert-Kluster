#!/usr/bin/env bash
set -euo pipefail

WIN_PROJECT="${WIN_PROJECT:-/mnt/c/Users/sierra/Documents/ESP32-S3-Bert-Kluster}"
WSL_PROJECT="${WSL_PROJECT:-$HOME/projects/ESP32-S3-Bert-Kluster}"
MODE="${1:-}"

if [[ -n "$MODE" && "$MODE" != "--sync-only" ]]; then
  echo "Usage: bash train_cycle.sh [--sync-only]" >&2
  exit 2
fi
if [[ ! -d "$WIN_PROJECT" ]]; then
  echo "Windows project folder not found: $WIN_PROJECT" >&2
  exit 1
fi

# Source scripts are always launched from Windows; they copy only project source into WSL.
bash "$WIN_PROJECT/Bert/sync_to_wsl.sh"
WSL_BERT="$WSL_PROJECT/Bert"
cd "$WSL_BERT"

if [[ "$MODE" == "--sync-only" ]]; then
  bash "$WIN_PROJECT/Bert/sync_from_wsl.sh"
  exit 0
fi

if [[ ! -x .venv/bin/python ]]; then
  echo "Python environment missing. Run once: bash setup_arch.sh cu132" >&2
  exit 1
fi
if [[ ! -f pruned/model_finetuned.pt ]]; then
  echo "Current trained checkpoint missing: pruned/model_finetuned.pt" >&2
  echo "Run the initial training first with: bash run_train.sh" >&2
  exit 1
fi

.venv/bin/python -c 'import torch; print("Compute:", torch.cuda.get_device_name(0) if torch.cuda.is_available() else "CUDA unavailable"); raise SystemExit(0 if torch.cuda.is_available() else 1)'

SIGNATURE_FILE="pruned/.last_train_cycle_signature"
INPUT_SIGNATURE="$(sha256sum medium_train_v2.py hard_examples.tsv config.json vocab.txt run_train.sh setup_arch.sh train_cycle.sh sync_to_wsl.sh sync_from_wsl.sh | sha256sum | cut -d' ' -f1)"
if [[ -f "$SIGNATURE_FILE" && "$(<"$SIGNATURE_FILE")" == "$INPUT_SIGNATURE" ]]; then
  echo "입력 코드와 hard_examples.tsv가 이전 회차와 같습니다. 학습을 건너뜁니다."
  bash "$WIN_PROJECT/Bert/sync_from_wsl.sh"
  exit 0
fi

if ! bash run_train.sh --iterate --no-interactive 2>&1 | tee pruned/last_train_cycle.log; then
  bash "$WIN_PROJECT/Bert/sync_from_wsl.sh" || true
  exit 1
fi
printf '%s\n' "$INPUT_SIGNATURE" > "$SIGNATURE_FILE.tmp"
mv "$SIGNATURE_FILE.tmp" "$SIGNATURE_FILE"
bash "$WIN_PROJECT/Bert/sync_from_wsl.sh"
