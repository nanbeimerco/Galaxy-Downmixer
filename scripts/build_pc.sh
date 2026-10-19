#!/usr/bin/env bash
# Builds the DSP core with Visual Studio 2022 and runs the unit tests.
# Uses the CMake that ships with the Android SDK.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CMAKE="${CMAKE:-$LOCALAPPDATA/Android/Sdk/cmake/3.22.1/bin/cmake.exe}"
BUILD="$ROOT/build/pc"

"$CMAKE" -S "$ROOT/dsp" -B "$BUILD" -G "Visual Studio 17 2022" -A x64
"$CMAKE" --build "$BUILD" --config Release
"$BUILD/Release/gdm_tests.exe"
