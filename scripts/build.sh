#!/usr/bin/env bash
# POLYBRISK をビルドする。
#   scripts/build.sh            Debug と Release の両方
#   scripts/build.sh Debug      指定した構成のみ
# 環境変数:
#   POLYBRISK_COPY_PLUGIN=OFF   ~/Library/Audio/Plug-Ins へのコピーを行わない
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CONFIGS=("$@")
[ ${#CONFIGS[@]} -eq 0 ] && CONFIGS=(Debug Release)
JOBS="$(sysctl -n hw.ncpu)"

for CONFIG in "${CONFIGS[@]}"; do
    BUILD_DIR="$ROOT/build/$CONFIG"
    echo "==> Configure ($CONFIG)"
    cmake -S "$ROOT" -B "$BUILD_DIR" \
        -DCMAKE_BUILD_TYPE="$CONFIG" \
        -DPOLYBRISK_COPY_PLUGIN="${POLYBRISK_COPY_PLUGIN:-ON}"
    echo "==> Build ($CONFIG)"
    cmake --build "$BUILD_DIR" --parallel "$JOBS"
done

echo "==> Build finished: ${CONFIGS[*]}"
