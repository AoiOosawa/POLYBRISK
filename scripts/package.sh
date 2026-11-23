#!/usr/bin/env bash
# 配布用のインストーラー (.pkg) を作る: Universal (Apple Silicon + Intel) の VST3 / Audio Unit / Standalone アプリ。
#
#   scripts/package.sh
#
# 環境変数 (どれも省略できる):
#   SIGN_APP        "Developer ID Application: Name (TEAMID)"  プラグインとアプリに署名する
#   SIGN_PKG        "Developer ID Installer: Name (TEAMID)"    インストーラーに署名する
#   NOTARY_PROFILE  `xcrun notarytool store-credentials` で作ったキーチェーンのプロファイル名。
#                   指定すると Apple の公証に出し、通ったらチケットを付ける (SIGN_APP と SIGN_PKG が必要)
#
# 何も指定しなければ、署名なしのインストーラーができる。この Mac で試すには十分だが、
# ほかの Mac では開けない (Gatekeeper が止める)。配布するものは、3 つとも指定して作る。
#
# 出力: build/dist/package/POLYBRISK-<version>-macOS.pkg
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/build/dist"
OUT="$BUILD/package"
ARTEFACTS="$BUILD/PolyBrisk_artefacts/Release"

VERSION="$(sed -nE 's/^project\(PolyBrisk VERSION ([0-9.]+).*/\1/p' "$ROOT/CMakeLists.txt")"
[ -n "$VERSION" ] || { echo "CMakeLists.txt からバージョンを読めません" >&2; exit 1; }

if [ -n "${NOTARY_PROFILE:-}" ] && { [ -z "${SIGN_APP:-}" ] || [ -z "${SIGN_PKG:-}" ]; }; then
    echo "公証 (NOTARY_PROFILE) には SIGN_APP と SIGN_PKG の両方が必要です" >&2
    exit 1
fi

echo "== Building version $VERSION (arm64 + x86_64)"
mkdir -p "$BUILD"
rm -rf "$ARTEFACTS"        # 前の名前やバージョンの成果物を残さない
cmake -S "$ROOT" -B "$BUILD" \
      -DCMAKE_BUILD_TYPE=Release \
      "-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64" \
      -DPOLYBRISK_DEV_IDENTITY=OFF \
      -DPOLYBRISK_COPY_PLUGIN=OFF \
      -DPOLYBRISK_BUILD_TESTS=OFF > "$BUILD/configure.log" 2>&1 || { tail -30 "$BUILD/configure.log"; exit 1; }
cmake --build "$BUILD" --target PolyBrisk_VST3 PolyBrisk_AU PolyBrisk_Standalone \
      --parallel "$(sysctl -n hw.ncpu)" > "$BUILD/build.log" 2>&1 || { grep -E "error" "$BUILD/build.log" | head -30; exit 1; }

VST3="$(ls -d "$ARTEFACTS"/VST3/*.vst3 | head -1)"
AU="$(ls -d "$ARTEFACTS"/AU/*.component | head -1)"
APP="$(ls -d "$ARTEFACTS"/Standalone/*.app | head -1)"
NAME="$(basename "$VST3" .vst3)"
[ "$NAME" = "POLYBRISK" ] || { echo "製品版の名前ではありません: $NAME (POLYBRISK_DEV_IDENTITY=OFF でビルドされていない)" >&2; exit 1; }
BUNDLE_ID="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$APP/Contents/Info.plist")"

for bundle in "$VST3" "$AU" "$APP"; do
    binary="$bundle/Contents/MacOS/$NAME"
    archs="$(lipo -archs "$binary")"
    case "$archs" in *arm64*x86_64*|*x86_64*arm64*) ;; *) echo "$bundle が Universal ではありません: $archs" >&2; exit 1;; esac
done

# ---- 署名 ------------------------------------------------------------------
if [ -n "${SIGN_APP:-}" ]; then
    echo "== Signing with: $SIGN_APP"
    for bundle in "$VST3" "$AU" "$APP"; do
        codesign --force --timestamp --options runtime --sign "$SIGN_APP" "$bundle"
    done
    codesign --verify --strict "$VST3" "$AU" "$APP"
else
    echo "== SIGN_APP が未設定: プラグインはビルド時の (ad hoc の) 署名のまま"
fi

# ---- 部品ごとのパッケージ ----------------------------------------------------
rm -rf "$OUT"
mkdir -p "$OUT/parts" "$OUT/resources"

component() {   # <bundle> <install location> <id suffix>
    local stage="$OUT/stage-$3"
    mkdir -p "$stage"
    ditto "$1" "$stage/$(basename "$1")"

    # 必ず指定のフォルダーへ入れる (別の場所にあるコピーへ上書きしない)
    local plist="$OUT/parts/$3.plist"
    pkgbuild --analyze --root "$stage" "$plist" > /dev/null
    if /usr/libexec/PlistBuddy -c "Print :0:BundleIsRelocatable" "$plist" > /dev/null 2>&1; then
        /usr/libexec/PlistBuddy -c "Set :0:BundleIsRelocatable false" "$plist"
    fi

    pkgbuild --root "$stage" --component-plist "$plist" \
             --install-location "$2" \
             --identifier "$BUNDLE_ID.pkg.$3" --version "$VERSION" \
             "$OUT/parts/$3.pkg" > /dev/null
    rm -rf "$stage"
}

component "$VST3" "/Library/Audio/Plug-Ins/VST3"       vst3
component "$AU"   "/Library/Audio/Plug-Ins/Components" au
component "$APP"  "/Applications"                      app

# ---- インストーラー -----------------------------------------------------------
{
    cat "$ROOT/LICENSE"
    printf '\n\n==========================================================================\n\n'
    cat "$ROOT/THIRD_PARTY_NOTICES.md"
} > "$OUT/resources/LICENSE.txt"

cat > "$OUT/distribution.xml" <<XML
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>$NAME $VERSION</title>
    <license file="LICENSE.txt"/>
    <options customize="always" require-scripts="false" hostArchitectures="arm64,x86_64"/>
    <os-version min="12.0"/>
    <choices-outline>
        <line choice="vst3"/>
        <line choice="au"/>
        <line choice="app"/>
    </choices-outline>
    <choice id="vst3" title="VST3 plugin" description="For Cubase, Studio One, REAPER and other VST3 hosts.">
        <pkg-ref id="$BUNDLE_ID.pkg.vst3"/>
    </choice>
    <choice id="au" title="Audio Unit plugin" description="For Logic Pro, GarageBand and other Audio Unit hosts.">
        <pkg-ref id="$BUNDLE_ID.pkg.au"/>
    </choice>
    <choice id="app" title="Standalone app" description="Runs without a host, in the Applications folder.">
        <pkg-ref id="$BUNDLE_ID.pkg.app"/>
    </choice>
    <pkg-ref id="$BUNDLE_ID.pkg.vst3" version="$VERSION">vst3.pkg</pkg-ref>
    <pkg-ref id="$BUNDLE_ID.pkg.au" version="$VERSION">au.pkg</pkg-ref>
    <pkg-ref id="$BUNDLE_ID.pkg.app" version="$VERSION">app.pkg</pkg-ref>
</installer-gui-script>
XML

PKG="$OUT/$NAME-$VERSION-macOS.pkg"
SIGN_ARGS=()
[ -n "${SIGN_PKG:-}" ] && SIGN_ARGS=(--sign "$SIGN_PKG" --timestamp)

productbuild --distribution "$OUT/distribution.xml" --package-path "$OUT/parts" \
             --resources "$OUT/resources" ${SIGN_ARGS[@]+"${SIGN_ARGS[@]}"} "$PKG" > /dev/null

[ -n "${SIGN_PKG:-}" ] || echo "== SIGN_PKG が未設定: インストーラーは署名なし"

# ---- 公証 ------------------------------------------------------------------
if [ -n "${NOTARY_PROFILE:-}" ]; then
    echo "== Sending to Apple for notarisation (数分かかります)"
    xcrun notarytool submit "$PKG" --keychain-profile "$NOTARY_PROFILE" --wait
    xcrun stapler staple "$PKG"
    spctl --assess --type install -vv "$PKG"
fi

echo "== Done: $PKG"
