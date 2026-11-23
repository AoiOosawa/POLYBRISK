#!/usr/bin/env bash
# 配布用のインストーラー (.pkg) を作る。
#   scripts/package.sh
#
# 既定では「署名なし」の pkg を build/dist/ に作る (手元での確認用。そのまま配布はできない)。
# 配布用にするには、次の環境変数を設定して実行する:
#   DEVELOPER_ID_APPLICATION  "Developer ID Application: …"   プラグインとアプリの署名
#   DEVELOPER_ID_INSTALLER    "Developer ID Installer: …"     pkg の署名
#   NOTARY_PROFILE            notarytool のキーチェーンプロファイル名 (xcrun notarytool store-credentials で作成)
# 3 つとも設定されているときだけ、署名 → 公証 → staple まで行う。公証は Apple へのアップロードを伴う。
#
# 配布前に必ず確認すること (docs/RELEASE_CHECKLIST.md):
#   - 識別子の確定 (Bundle ID / AU の製造者コード。変更すると既存プロジェクトから見つからなくなる)
#   - JUCE のライセンス (AGPLv3 か商用か)
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$ROOT/build/Release"
ARTEFACTS="$BUILD_DIR/PolyBrisk_artefacts/Release"
DIST="$ROOT/build/dist"
STAGE="$DIST/root"
VERSION="$(sed -n 's/^project(PolyBrisk VERSION \([0-9.]*\).*/\1/p' "$ROOT/CMakeLists.txt")"
IDENTIFIER="com.polybrisk.polybrisk.pkg"

echo "==> Build (Release, プラグインフォルダーへのコピーなし)"
cmake -S "$ROOT" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release -DPOLYBRISK_COPY_PLUGIN=OFF >/dev/null
cmake --build "$BUILD_DIR" --target PolyBrisk_AU PolyBrisk_VST3 PolyBrisk_Standalone --parallel "$(sysctl -n hw.ncpu)" >/dev/null

echo "==> Stage"
rm -rf "$DIST"
mkdir -p "$STAGE/Library/Audio/Plug-Ins/Components" "$STAGE/Library/Audio/Plug-Ins/VST3" "$STAGE/Applications"
ditto --norsrc --noextattr "$ARTEFACTS/AU/POLYBRISK.component" "$STAGE/Library/Audio/Plug-Ins/Components/POLYBRISK.component"
ditto --norsrc --noextattr "$ARTEFACTS/VST3/POLYBRISK.vst3" "$STAGE/Library/Audio/Plug-Ins/VST3/POLYBRISK.vst3"
ditto --norsrc --noextattr "$ARTEFACTS/Standalone/POLYBRISK.app" "$STAGE/Applications/POLYBRISK.app"

SIGNED=0
if [ -n "${DEVELOPER_ID_APPLICATION:-}" ] && [ -n "${DEVELOPER_ID_INSTALLER:-}" ] && [ -n "${NOTARY_PROFILE:-}" ]; then
    echo "==> Code sign (hardened runtime)"
    for BUNDLE in "$STAGE/Library/Audio/Plug-Ins/Components/POLYBRISK.component" \
                  "$STAGE/Library/Audio/Plug-Ins/VST3/POLYBRISK.vst3" \
                  "$STAGE/Applications/POLYBRISK.app"; do
        codesign --force --deep --options runtime --timestamp --sign "$DEVELOPER_ID_APPLICATION" "$BUNDLE"
        codesign --verify --deep --strict "$BUNDLE"
    done
    SIGNED=1
else
    echo "==> 署名なしで作成します (DEVELOPER_ID_APPLICATION / DEVELOPER_ID_INSTALLER / NOTARY_PROFILE が未設定)"
fi

echo "==> pkgbuild"
COMPONENT_PLIST="$DIST/components.plist"
pkgbuild --analyze --root "$STAGE" "$COMPONENT_PLIST" >/dev/null
# アプリを別の場所へ移していても、そこへ上書きせず /Applications に入れる
/usr/libexec/PlistBuddy -c "Set :0:BundleIsRelocatable false" "$COMPONENT_PLIST" 2>/dev/null || true
/usr/libexec/PlistBuddy -c "Set :1:BundleIsRelocatable false" "$COMPONENT_PLIST" 2>/dev/null || true
/usr/libexec/PlistBuddy -c "Set :2:BundleIsRelocatable false" "$COMPONENT_PLIST" 2>/dev/null || true

if [ "$SIGNED" = 1 ]; then
    PKG="$DIST/POLYBRISK-$VERSION.pkg"
    pkgbuild --root "$STAGE" --component-plist "$COMPONENT_PLIST" --identifier "$IDENTIFIER" --version "$VERSION" \
             --install-location / --sign "$DEVELOPER_ID_INSTALLER" --timestamp "$PKG"

    echo "==> Notarize (Apple へアップロードします)"
    xcrun notarytool submit "$PKG" --keychain-profile "$NOTARY_PROFILE" --wait
    xcrun stapler staple "$PKG"
    spctl --assess --type install -vv "$PKG"
else
    PKG="$DIST/POLYBRISK-$VERSION-unsigned.pkg"
    pkgbuild --root "$STAGE" --component-plist "$COMPONENT_PLIST" --identifier "$IDENTIFIER" --version "$VERSION" \
             --install-location / "$PKG"
fi

echo "==> $PKG"
pkgutil --payload-files "$PKG" | grep -E '\.(component|vst3|app)$'
