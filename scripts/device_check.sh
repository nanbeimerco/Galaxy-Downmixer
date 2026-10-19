#!/usr/bin/env bash
# Checks on the phone whether the effect is installed, loaded by the audio HAL
# and receiving 7.1.4. Read-only. Play an Apple Music Dolby Atmos track first.
set -euo pipefail
export MSYS_NO_PATHCONV=1  # keep /data/... paths as they are in Git Bash

ROOT="$(cd "$(dirname "$0")/.." && pwd -W 2>/dev/null || pwd)"  # Windows form for adb.exe
adb push "$ROOT/scripts/device/gdm_check.sh" /data/local/tmp/gdm_check.sh >/dev/null
adb shell "su -c 'sh /data/local/tmp/gdm_check.sh'" | tr -d '\r'
