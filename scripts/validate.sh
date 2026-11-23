#!/usr/bin/env bash
# ビルドしたプラグインを、ホスト非依存の検証ツールで検査する。
# auval は、システムに登録された AU を識別子で探す。scripts/build.sh が ~/Library/Audio/Plug-Ins へコピーした直後に実行すること。
#   scripts/validate.sh [Debug|Release]   既定は Release
# - auval: AU の検証 (macOS 標準)
# - pluginval: 見つかった場合のみ VST3 / AU を検証 (PLUGINVAL 環境変数でパス指定可)
#   厳しさは PLUGINVAL_STRICTNESS で指定 (1〜10、既定 5。リリース前は 10)
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CONFIG="${1:-Release}"
ARTEFACTS="$ROOT/build/$CONFIG/PolyBrisk_artefacts/$CONFIG"
STATUS=0

# 検証するのは、このビルドの成果物 (開発用のビルドなら「POLYBRISK Dev」。D-078)。
# AU の識別子は、ビルドした .component の Info.plist から読む (CMakeLists.txt と二重に持たない)。
VST3_BUNDLE="$(ls -d "$ARTEFACTS"/VST3/*.vst3 2>/dev/null | head -1)"
AU_BUNDLE="$(ls -d "$ARTEFACTS"/AU/*.component 2>/dev/null | head -1)"

if [ -z "$VST3_BUNDLE" ] || [ -z "$AU_BUNDLE" ]; then
    echo "$ARTEFACTS にプラグインがありません。先に scripts/build.sh $CONFIG を実行してください。" >&2
    exit 1
fi

plist() { /usr/libexec/PlistBuddy -c "Print :AudioComponents:0:$1" "$AU_BUNDLE/Contents/Info.plist"; }
AU_TYPE="$(plist type)"
AU_SUBTYPE="$(plist subtype)"
AU_MANUFACTURER="$(plist manufacturer)"
echo "==> $(plist name)  ($AU_TYPE $AU_SUBTYPE $AU_MANUFACTURER)"

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
    for PLUGIN in "$VST3_BUNDLE" "$AU_BUNDLE"; do
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
