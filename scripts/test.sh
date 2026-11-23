#!/usr/bin/env bash
# ユニットテスト (JUCE UnitTest + CTest) を実行する。
#   scripts/test.sh [Debug|Release]   既定は Debug
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CONFIG="${1:-Debug}"
BUILD_DIR="$ROOT/build/$CONFIG"

if [ ! -d "$BUILD_DIR" ]; then
    echo "build/$CONFIG がありません。先に scripts/build.sh $CONFIG を実行してください。" >&2
    exit 1
fi

cmake --build "$BUILD_DIR" --target polybrisk_tests --parallel "$(sysctl -n hw.ncpu)"
ctest --test-dir "$BUILD_DIR" --output-on-failure
