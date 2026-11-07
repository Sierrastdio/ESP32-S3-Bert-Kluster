#!/usr/bin/env bash
set -euo pipefail

WIN_PROJECT="${WIN_PROJECT:-/mnt/c/Users/sierra/Documents/ESP32-S3-Bert-Kluster}"
WSL_PROJECT="${WSL_PROJECT:-$HOME/projects/ESP32-S3-Bert-Kluster}"
WSL_PRUNED="$WSL_PROJECT/Bert/pruned"
WIN_PRUNED="$WIN_PROJECT/Bert/pruned"

if [[ ! -d "$WIN_PROJECT" ]]; then
  echo "Windows project folder not found: $WIN_PROJECT" >&2
  exit 1
fi
if [[ ! -d "$WSL_PRUNED" ]]; then
  echo "WSL training output not found: $WSL_PRUNED" >&2
  exit 1
fi
mkdir -p "$WIN_PRUNED"

cp -r "$WSL_PRUNED/." "$WIN_PRUNED/"

echo "WSL training outputs copied to Windows: $WIN_PRUNED"
echo "Only Bert/pruned was copied; Windows source files were left untouched."
