#!/usr/bin/env bash
# 製品ページの OGP 画像 (SNS などでリンクを貼ったときに出る画像) を作り直す。
#   scripts/make_og_image.sh
# tools/og_image.html (ロゴ + Simple の画面) を、Google Chrome のヘッドレスモードで 1200×630 の PNG にする。
# 画面の見た目を変えたら、先に polybrisk_render --screenshots で site/assets/screens/ を更新してから実行する。
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CHROME="${CHROME:-/Applications/Google Chrome.app/Contents/MacOS/Google Chrome}"
OUT="$ROOT/site/assets/og.png"

[ -x "$CHROME" ] || { echo "Google Chrome が見つかりません (CHROME でパスを指定できます)" >&2; exit 1; }

"$CHROME" --headless=new --disable-gpu --hide-scrollbars --force-device-scale-factor=1 \
          --window-size=1200,630 --screenshot="$OUT" "file://$ROOT/tools/og_image.html" > /dev/null 2>&1

sips -g pixelWidth -g pixelHeight "$OUT" | tail -2
echo "==> $OUT"
