#!/usr/bin/env bash
# Installs the packaged module with Magisk. Does not reboot; the module is
# active after the next reboot.
#
# To back out: Magisk app > Modules > remove, or
#   adb shell "su -c 'touch /data/adb/modules/galaxy_downmixer/remove'"  then reboot.
# If the phone misbehaves at boot, Magisk's safe mode (hold volume down while
# booting) starts with all modules disabled.
set -euo pipefail
export MSYS_NO_PATHCONV=1

ROOT="$(cd "$(dirname "$0")/.." && pwd -W 2>/dev/null || pwd)"  # Windows form for adb.exe
VERSION="$(sed -n 's/^version=//p' "$ROOT/module/module.prop")"
ZIP="$ROOT/build/GalaxyDownmixer-$VERSION.zip"
[ -f "$ZIP" ] || { echo "missing $ZIP; run scripts/package_module.sh first" >&2; exit 1; }

adb push "$ZIP" /data/local/tmp/galaxy_downmixer.zip
adb shell "su -c 'magisk --install-module /data/local/tmp/galaxy_downmixer.zip'" | tr -d '\r'
echo "installed; reboot the phone to activate"
