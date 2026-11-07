#!/usr/bin/env bash
set -euo pipefail

WIN_PROJECT="${WIN_PROJECT:-/mnt/c/Users/sierra/Documents/ESP32-S3-Bert-Kluster}"
WSL_PROJECT="${WSL_PROJECT:-$HOME/projects/ESP32-S3-Bert-Kluster}"

if [[ ! -d "$WIN_PROJECT" ]]; then
  echo "Windows project folder not found: $WIN_PROJECT" >&2
  exit 1
fi
mkdir -p "$WSL_PROJECT"
if [[ ! -w "$WSL_PROJECT" ]]; then
  echo "WSL project folder is not writable: $WSL_PROJECT" >&2
  echo "Make it user-owned before syncing; do not work from ~/clone." >&2
  exit 1
fi

# Copy the repository in full, preserving only Git metadata and the WSL-only venv.
tar -C "$WIN_PROJECT" \
  --exclude-vcs \
  --exclude='./.git.disabled.*' \
  --exclude='./Bert/.venv' \
  --exclude='./pico1/build*' \
  --exclude='./pico1/CMakeFiles' \
  -cf - . | tar -C "$WSL_PROJECT" -xf -

chmod u+rwx "$WSL_PROJECT"
if [[ -d "$WSL_PROJECT/.git" ]]; then
  DISABLED_GIT="$WSL_PROJECT/.git.disabled.$(date +%Y%m%d_%H%M%S)"
  mv "$WSL_PROJECT/.git" "$DISABLED_GIT"
  echo "WSL Git metadata parked at $(basename "$DISABLED_GIT"); use the Windows project for Git."
fi
echo "Windows project synced to WSL: $WSL_PROJECT"
echo "Excluded and preserved: Git metadata, Bert/.venv, Pico build outputs"
