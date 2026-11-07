#!/usr/bin/env bash
set -euo pipefail

WIN_PROJECT="${WIN_PROJECT:-/mnt/c/Users/sierra/Documents/ESP32-S3-Bert-Kluster}"
WSL_PROJECT="${WSL_PROJECT:-$HOME/projects/ESP32-S3-Bert-Kluster}"

if [[ ! -d "$WIN_PROJECT" ]]; then
  echo "Windows project folder not found: $WIN_PROJECT" >&2
  exit 1
fi
if [[ ! -d "$WSL_PROJECT" ]]; then
  echo "WSL project folder not found: $WSL_PROJECT" >&2
  exit 1
fi
mkdir -p "$WIN_PROJECT"

# Copy the repository in full, preserving Windows Git metadata and WSL-only files.
tar -C "$WSL_PROJECT" \
  --exclude-vcs \
  --exclude='./.git.disabled.*' \
  --exclude='./Bert/.venv' \
  --exclude='./pico1/build*' \
  --exclude='./pico1/CMakeFiles' \
  -cf - . | tar -C "$WIN_PROJECT" -xf -

echo "WSL project synced to Windows: $WIN_PROJECT"
echo "Excluded and preserved: Git metadata, Bert/.venv, Pico build outputs"
