#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$(realpath "$0")")"

TORCH_CHANNEL="${1:-cpu}"
if [[ "$TORCH_CHANNEL" == "cpu" ]]; then
  TORCH_INDEX_URL="https://download.pytorch.org/whl/cpu"
elif [[ "$TORCH_CHANNEL" =~ ^cu[0-9]+$ ]]; then
  TORCH_INDEX_URL="https://download.pytorch.org/whl/${TORCH_CHANNEL}"
else
  echo "Usage: bash setup_arch.sh [cpu|cuXXX] (example CUDA tag: cu132)" >&2
  exit 2
fi

if ! command -v python >/dev/null 2>&1; then
  echo "Python 3 is required. On Arch Linux run: sudo pacman -S python" >&2
  exit 1
fi

python -m venv .venv
.venv/bin/python -m pip install --upgrade pip
TORCH_MARKER=".venv/.torch_channel"
if [[ ! -f "$TORCH_MARKER" || "$(<"$TORCH_MARKER")" != "$TORCH_CHANNEL" ]]; then
  .venv/bin/python -m pip uninstall -y torch >/dev/null 2>&1 || true
fi
.venv/bin/python -m pip install --upgrade torch --index-url "$TORCH_INDEX_URL"
.venv/bin/python -c 'import torch; print("PyTorch:", torch.__version__, "| CUDA available:", torch.cuda.is_available())'
.venv/bin/python -m pip install transformers

echo
printf '%s\n' "$TORCH_CHANNEL" > "$TORCH_MARKER"
echo "Environment ready. Put the matching pretrained model files in this folder, then run:"
echo "  bash run_train.sh"
