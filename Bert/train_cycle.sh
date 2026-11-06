#!/usr/bin/env bash
set -euo pipefail

WIN_PROJECT="${WIN_PROJECT:-/mnt/c/Users/sierra/Documents/ESP32-S3-Bert-Kluster}"
WSL_PROJECT="${WSL_PROJECT:-$HOME/projects/ESP32-S3-Bert-Kluster}"

if [[ ! -d "$WIN_PROJECT" ]]; then
  echo "Windows project folder not found: $WIN_PROJECT" >&2
  exit 1
fi

mkdir -p "$WSL_PROJECT"
WSL_BERT="$WSL_PROJECT/Bert"
mkdir -p "$WSL_BERT"
for file in medium_train_v2.py hard_examples.tsv train_cycle.sh run_train.sh setup_arch.sh config.json vocab.txt README.md; do
  if [[ ! -f "$WIN_PROJECT/Bert/$file" ]]; then
    echo "Required Windows training file missing: $WIN_PROJECT/Bert/$file" >&2
    exit 1
  fi
  cp "$WIN_PROJECT/Bert/$file" "$WSL_BERT/$file"
  chmod u+rw "$WSL_BERT/$file"
done
chmod u+rwx "$WSL_PROJECT" "$WSL_BERT"
cd "$WSL_BERT"

if [[ ! -x .venv/bin/python ]]; then
  echo "Python environment missing. Run: bash setup_arch.sh cu132" >&2
  exit 1
fi
if [[ ! -f pruned/model_finetuned.pt ]]; then
  echo "Current trained checkpoint missing: pruned/model_finetuned.pt" >&2
  echo "Run the initial training first with: bash run_train.sh" >&2
  exit 1
fi

.venv/bin/python -c 'import torch; print("Compute:", torch.cuda.get_device_name(0) if torch.cuda.is_available() else "CUDA unavailable"); raise SystemExit(0 if torch.cuda.is_available() else 1)'

SIGNATURE_FILE="pruned/.last_train_cycle_signature"
INPUT_SIGNATURE="$(sha256sum medium_train_v2.py hard_examples.tsv train_cycle.sh run_train.sh setup_arch.sh config.json vocab.txt | sha256sum | cut -d' ' -f1)"
if [[ -f "$SIGNATURE_FILE" && "$(<"$SIGNATURE_FILE")" == "$INPUT_SIGNATURE" ]]; then
  echo "코드와 hard_examples.tsv가 지난 학습 이후 바뀌지 않았습니다. 새 보강 데이터가 없어 학습을 건너뜁니다."
  echo "같은 데이터로 다시 시도하려면 $SIGNATURE_FILE 을 삭제하세요."
  exit 0
fi

bash run_train.sh --iterate --no-interactive
printf '%s\n' "$INPUT_SIGNATURE" > "$SIGNATURE_FILE.tmp"
mv "$SIGNATURE_FILE.tmp" "$SIGNATURE_FILE"
