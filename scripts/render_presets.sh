#!/usr/bin/env bash
# Factory プリセットを WAV に書き出す (試聴用)。
#   scripts/render_presets.sh [sample-rate]   出力先: build/renders/
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$ROOT/build/Release"

if [ ! -d "$BUILD_DIR" ]; then
    echo "build/Release がありません。先に scripts/build.sh Release を実行してください。" >&2
    exit 1
fi

cmake --build "$BUILD_DIR" --target polybrisk_render --parallel "$(sysctl -n hw.ncpu)" >/dev/null
BIN="$(find "$BUILD_DIR" -type f -name polybrisk_render -perm +111 | head -1)"
rm -rf "$ROOT/build/renders"
"$BIN" "$ROOT/build/renders" "${1:-48000}"
