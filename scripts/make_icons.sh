#!/usr/bin/env bash
# assets/branding/icon.svg から icon-1024.png と POLYBRISK.icns を生成する (macOS 標準ツールのみ)。
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DIR="$ROOT/assets/branding"
ICONSET="$(mktemp -d)/POLYBRISK.iconset"
mkdir -p "$ICONSET"

sips -s format png "$DIR/icon.svg" --out "$DIR/icon-1024.png" >/dev/null

for SIZE in 16 32 128 256 512; do
    sips -z "$SIZE" "$SIZE" "$DIR/icon-1024.png" --out "$ICONSET/icon_${SIZE}x${SIZE}.png" >/dev/null
    DOUBLE=$((SIZE * 2))
    sips -z "$DOUBLE" "$DOUBLE" "$DIR/icon-1024.png" --out "$ICONSET/icon_${SIZE}x${SIZE}@2x.png" >/dev/null
done

iconutil -c icns "$ICONSET" -o "$DIR/POLYBRISK.icns"
sips -s format png "$DIR/logo-mark.svg" --out "$DIR/logo-mark.png" >/dev/null 2>&1 || true
echo "Generated: $DIR/icon-1024.png, $DIR/POLYBRISK.icns"
