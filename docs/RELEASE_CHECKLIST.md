# POLYBRISK — Release Checklist（Phase 7）

配布までに済ませること。状態: ✅ 済み / 🟡 一部 / ⬜ 未着手 / ⛔ 判断が必要。

## 判断が必要なもの（開発側では決められない）

| 項目 | 状態 | 内容 |
|---|---|---|
| JUCE のライセンス | ✅ | **AGPL-3.0 で配布する**（D-077）。ソースは <https://github.com/cheebow/POLYBRISK> で公開済み。`LICENSE` と `THIRD_PARTY_NOTICES.md` を置き、インストーラーにも表示する |
| 識別子の確定 | ✅ | Bundle ID `info.cheebow.polybrisk`、AU のメーカーコード `Chbw`（CHEEBOW のプラグインで共通）、プラグインコード `Pbrk`（D-077）。**配布後は変えない**（利用者の DAW のプロジェクトからプラグインが見つからなくなる）。書いてある場所は `CMakeLists.txt` と `scripts/validate.sh` |
| 製品名・ロゴの商標確認 | ⛔ | 名称衝突・商標の調査は未実施（D-013） |
| 配布形態・公開先 | ⛔ | 販売 / 無償、紹介ページのホスティングとドメイン（D-014）。紹介ページは GitHub Pages で公開済み（<https://cheebow.github.io/POLYBRISK/>、D-075）。独自ドメインは未定 |
| 対応環境 | ✅ | macOS 12 以降、Apple Silicon と Intel（Universal）（D-077）。Intel は Rosetta 上の自動テストまでで、実機では未確認 |

## 検証

| 項目 | 状態 | 記録 |
|---|---|---|
| 自動テスト（Debug / Release） | ✅ | `scripts/test.sh`。件数は `docs/TEST_PLAN.md` |
| auval | ✅ | `scripts/validate.sh` |
| pluginval（VST3 / AU） | ✅ | strictness 5 と 10。`PLUGINVAL_STRICTNESS=10 scripts/validate.sh` |
| Cubase（VST3）での動作 | ⬜ | `docs/TEST_PLAN.md`「DAW での手動検証」 |
| Logic Pro（AU）での動作 | ⬜ | 同上 |
| Standalone での発音・設定の保存 | 🟡 | 起動と終了のみ確認 |
| プロジェクト保存 → 再起動 → 復元 | ⬜ | 自動テストでは状態の往復が一致。実ホストでは未確認 |
| オートメーション、テンポ同期、バウンス | ⬜ | |
| ホスト内での CPU・音切れ | ⬜ | エンジン単体の計測は D-038 |
| 音色の試聴・調整（82 音色。Standalone で一巡済み、D-052。DAW 上では未確認） | ⬜ | `scripts/render_presets.sh` で WAV を書き出せる |

## パッケージ

| 項目 | 状態 | 内容 |
|---|---|---|
| インストーラーの作成 | ✅ | `scripts/package.sh` が Universal の `build/dist/package/POLYBRISK-<version>-macOS.pkg` を作る。VST3 / Audio Unit / Standalone を選んで入れられる。署名なしで作成し、中身（3 つの部品、入れる場所、識別子、対応 CPU と OS）を確認済み。インストールの実行は未確認 |
| コード署名（Developer ID Application） | ⬜ | `SIGN_APP` を設定して `scripts/package.sh`。ユーザーの指示で行う |
| インストーラーの署名（Developer ID Installer） | ⬜ | `SIGN_PKG` を設定して `scripts/package.sh`。ユーザーの指示で行う |
| 公証（Notarization）と staple | ⬜ | `NOTARY_PROFILE`（`xcrun notarytool store-credentials` で作ったプロファイル名）を設定。Apple へのアップロードを伴うので、ユーザーの指示で行う |
| バージョン番号 | 🟡 | `CMakeLists.txt` の `project(... VERSION 0.1.0)` |
| ライセンス表記 | ✅ | `LICENSE`（AGPL-3.0）と `THIRD_PARTY_NOTICES.md`。インストーラーのライセンスの画面に両方を表示する |
| ユーザーガイド | ✅ | `docs/USER_GUIDE.md` |
| 紹介ページ | 🟡 | `site/index.html`（実画面とデモ音源を掲載。GitHub Pages で公開済み。ダウンロードの導線は未設置） |

## 配布用パッケージを作る手順

```bash
scripts/build.sh Release
scripts/test.sh Release
PLUGINVAL_STRICTNESS=10 scripts/validate.sh Release

export SIGN_APP="Developer ID Application: …"
export SIGN_PKG="Developer ID Installer: …"
export NOTARY_PROFILE="…"           # xcrun notarytool store-credentials で作ったプロファイル名
scripts/package.sh                   # Universal でビルド → 署名 → 公証 → チケットの添付まで行う
```

3 つの環境変数を省くと、署名なしのインストーラーができる（この Mac で試す用。ほかの Mac では開けない）。

出来上がった `build/dist/package/POLYBRISK-<version>-macOS.pkg` を GitHub Releases に置く。できれば、開発環境のない別の Mac でインストールし、DAW で読み込みと発音を確かめる。

**開発機での注意**: インストーラーは `/Library/Audio/Plug-Ins/` に入れる。開発用のビルド（`scripts/build.sh`）は `~/Library/Audio/Plug-Ins/` にコピーするので、両方があると DAW に同じプラグインが 2 つ見える。インストーラーを試すときは、`~/Library` 側の `POLYBRISK.vst3` と `POLYBRISK.component` を消す。
