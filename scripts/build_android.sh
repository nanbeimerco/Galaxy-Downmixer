#!/usr/bin/env bash
# Builds libgalaxydownmixer.so with the Android NDK for both ABIs the audio
# HAL may use (the Galaxy S21's android.hardware.audio.service is 32-bit).
# libc++ is linked statically: vendor processes cannot load libc++_shared.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SDK="${ANDROID_SDK_ROOT:-$LOCALAPPDATA/Android/Sdk}"
NDK="${ANDROID_NDK_ROOT:-$SDK/ndk/29.0.14206865}"
CMAKE_DIR="$SDK/cmake/3.22.1/bin"
API="${ANDROID_API:-30}"

for ABI in armeabi-v7a arm64-v8a; do
    BUILD="$ROOT/build/android-$ABI"
    "$CMAKE_DIR/cmake.exe" -S "$ROOT/effect" -B "$BUILD" -G Ninja \
        -DCMAKE_MAKE_PROGRAM="$CMAKE_DIR/ninja.exe" \
        -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
        -DANDROID_ABI="$ABI" -DANDROID_PLATFORM="android-$API" -DANDROID_STL=c++_static \
        -DCMAKE_BUILD_TYPE=Release
    "$CMAKE_DIR/cmake.exe" --build "$BUILD"
done
