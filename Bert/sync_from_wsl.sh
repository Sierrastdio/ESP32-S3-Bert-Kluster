#!/usr/bin/env bash
set -euo pipefail

WIN_PROJECT="${WIN_PROJECT:-/mnt/c/Users/sierra/Documents/ESP32-S3-Bert-Kluster}"
WSL_PROJECT="${WSL_PROJECT:-$HOME/projects/ESP32-S3-Bert-Kluster}"
WSL_BERT="$WSL_PROJECT/Bert"
WIN_BERT="$WIN_PROJECT/Bert"

if [[ ! -d "$WSL_BERT/pruned" ]]; then
  echo "WSL training output not found: $WSL_BERT/pruned" >&2
  exit 1
fi
mkdir -p "$WIN_BERT/pruned"
cp -r "$WSL_BERT/pruned/." "$WIN_BERT/pruned/"

for file in last_unseen_errors.tsv last_train_cycle.log; do
  if [[ -f "$WSL_BERT/pruned/$file" ]]; then
    destination="latest_${file#last_}"
    cp "$WSL_BERT/pruned/$file" "$WIN_BERT/$destination"
  fi
done

echo "WSL pruned outputs copied to Windows project: $WIN_BERT/pruned"
echo "Git operations remain in the Windows project."
