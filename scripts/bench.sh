#!/usr/bin/env bash
# DSP ベンチマーク (SPEC §14.3) を Release ビルドで実行する。
#   scripts/bench.sh [seconds]   既定は 1 シナリオあたり 20 秒分の音声
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$ROOT/build/Release"

if [ ! -d "$BUILD_DIR" ]; then
    echo "build/Release がありません。先に scripts/build.sh Release を実行してください。" >&2
    exit 1
fi

cmake --build "$BUILD_DIR" --target polybrisk_bench --parallel "$(sysctl -n hw.ncpu)" >/dev/null
sysctl -n machdep.cpu.brand_string
"$BUILD_DIR/polybrisk_bench" "${1:-20}"
