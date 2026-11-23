#!/usr/bin/env bash
# インストール済みプラグインをホスト非依存の検証ツールで検査する。
#   scripts/validate.sh [Debug|Release]   既定は Release
# - auval: AU の検証 (macOS 標準)
# - pluginval: 見つかった場合のみ VST3 / AU を検証 (PLUGINVAL 環境変数でパス指定可)
#   厳しさは PLUGINVAL_STRICTNESS で指定 (1〜10、既定 5。リリース前は 10)
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CONFIG="${1:-Release}"
ARTEFACTS="$ROOT/build/$CONFIG/PolyBrisk_artefacts/$CONFIG"
STATUS=0

# 識別子 (docs/DECISIONS.md D-077)。CMakeLists.txt と一致させること。
AU_TYPE=aumu
AU_SUBTYPE=Pbrk
AU_MANUFACTURER=Chbw

echo "==> auval -v $AU_TYPE $AU_SUBTYPE $AU_MANUFACTURER"
if auval -v "$AU_TYPE" "$AU_SUBTYPE" "$AU_MANUFACTURER"; then
    echo "auval: PASS"
else
    echo "auval: FAIL" >&2
    STATUS=1
fi

PLUGINVAL_BIN="${PLUGINVAL:-}"
if [ -z "$PLUGINVAL_BIN" ]; then
    if command -v pluginval >/dev/null 2>&1; then
        PLUGINVAL_BIN="$(command -v pluginval)"
    elif [ -x "/Applications/pluginval.app/Contents/MacOS/pluginval" ]; then
        PLUGINVAL_BIN="/Applications/pluginval.app/Contents/MacOS/pluginval"
    fi
fi

if [ -n "$PLUGINVAL_BIN" ]; then
    for PLUGIN in "$ARTEFACTS/VST3/POLYBRISK.vst3" "$ARTEFACTS/AU/POLYBRISK.component"; do
        echo "==> pluginval $PLUGIN"
        if "$PLUGINVAL_BIN" --strictness-level "${PLUGINVAL_STRICTNESS:-5}" --validate-in-process --validate "$PLUGIN"; then
            echo "pluginval: PASS ($PLUGIN)"
        else
            echo "pluginval: FAIL ($PLUGIN)" >&2
            STATUS=1
        fi
    done
else
    echo "==> pluginval: SKIPPED (未インストール。https://github.com/Tracktion/pluginval)"
fi

exit $STATUS
