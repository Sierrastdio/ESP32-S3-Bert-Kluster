#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$(realpath "$0")")"

if [[ ! -x .venv/bin/python ]]; then
  echo "Environment not found. Run ./setup_arch.sh first." >&2
  exit 1
fi

if [[ ! -f config.json || ! -f vocab.txt ]]; then
  echo "config.json and vocab.txt must be in the Bert folder." >&2
  exit 1
fi

if [[ ! -f pytorch_model.bin && ! -f model.safetensors && ! -f pytorch_model.safetensors ]]; then
  echo "Pretrained model weights are missing from: $(pwd)" >&2
  echo "Copy the matching BERT-Medium checkpoint files here (including its config and vocabulary)." >&2
  echo "The v2 script needs weights to preserve the pretrained knowledge when pruning the vocabulary." >&2
  exit 2
fi

exec .venv/bin/python medium_train_v2.py "$@"
