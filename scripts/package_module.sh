#!/usr/bin/env bash
# Builds the effect library and packs the Magisk module zip into build/.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SDK="${ANDROID_SDK_ROOT:-$LOCALAPPDATA/Android/Sdk}"
NDK="${ANDROID_NDK_ROOT:-$SDK/ndk/29.0.14206865}"
STRIP="$NDK/toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-strip.exe"
STAGE="$ROOT/build/module_stage"
VERSION="$(sed -n 's/^version=//p' "$ROOT/module/module.prop")"
ZIP="$ROOT/build/GalaxyDownmixer-$VERSION.zip"

bash "$ROOT/scripts/build_android.sh"

rm -rf "$STAGE"
cp -r "$ROOT/module" "$STAGE"
mkdir -p "$STAGE/system/vendor/lib/soundfx" "$STAGE/system/vendor/lib64/soundfx"
"$STRIP" --strip-unneeded -o "$STAGE/system/vendor/lib/soundfx/libgalaxydownmixer.so" \
    "$ROOT/build/android-armeabi-v7a/libgalaxydownmixer.so"
"$STRIP" --strip-unneeded -o "$STAGE/system/vendor/lib64/soundfx/libgalaxydownmixer.so" \
    "$ROOT/build/android-arm64-v8a/libgalaxydownmixer.so"
cp "$ROOT/dsp/tests/data/ig955_eq.txt" "$STAGE/system/vendor/etc/galaxy_downmixer/eq.example.txt"

# Shell scripts run under Magisk's busybox and must not contain CR.
if grep -rlI $'\r' "$STAGE" >/dev/null; then
    echo "CRLF line endings found in:" >&2
    grep -rlI $'\r' "$STAGE" >&2
    exit 1
fi

python - "$STAGE" "$ZIP" <<'EOF'
import os, sys, zipfile
stage, out = sys.argv[1], sys.argv[2]
with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
    for base, _, files in os.walk(stage):
        for name in sorted(files):
            path = os.path.join(base, name)
            z.write(path, os.path.relpath(path, stage).replace(os.sep, "/"))
EOF
echo "built $ZIP"
python -c "import zipfile,sys; [print(f'  {i.file_size:>9}  {i.filename}') for i in zipfile.ZipFile(sys.argv[1]).infolist()]" "$ZIP"
