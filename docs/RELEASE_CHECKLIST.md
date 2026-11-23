# POLYBRISK — Release Checklist（Phase 7）

配布までに済ませること。状態: ✅ 済み / 🟡 一部 / ⬜ 未着手 / ⛔ 判断が必要。

## 判断が必要なもの（開発側では決められない）

| 項目 | 状態 | 内容 |
|---|---|---|
| JUCE のライセンス | ⛔ | JUCE 9 は AGPLv3 と商用ライセンス（JUCE 9 EULA）のデュアル。AGPLv3 で配布するならソース公開が条件。クローズドで配布するなら商用ライセンスの条件（収益上限など）を公式サイトで確認して契約する（D-002） |
| 識別子の確定 | ⛔ | メーカー名は CHEEBOW で決定。Bundle ID `com.polybrisk.polybrisk`、AU 製造者コード `Pbrk`、プラグインコード `Pbk1` は仮値のまま（D-005）。**公開後に変えると、既存の DAW プロジェクトからプラグインが見つからなくなる。** 変更箇所は `CMakeLists.txt` と `scripts/validate.sh`、`scripts/package.sh` |
| 製品名・ロゴの商標確認 | ⛔ | 名称衝突・商標の調査は未実施（D-013） |
| 配布形態・公開先 | ⛔ | 販売 / 無償、紹介ページのホスティングとドメイン（D-014）。紹介ページは GitHub Pages で公開済み（<https://cheebow.github.io/POLYBRISK/>、D-075）。独自ドメインは未定 |
| 対応環境 | ⛔ | 現在は Apple Silicon / macOS 13 以降のみ。Intel（Universal）対応は未検証（D-004） |

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
| インストーラーの作成 | 🟡 | `scripts/package.sh` で**署名なし**の `build/dist/POLYBRISK-<version>-unsigned.pkg` を作成・内容を確認済み。インストールの実行は未確認 |
| コード署名（Developer ID Application） | ⬜ | `DEVELOPER_ID_APPLICATION` を設定して `scripts/package.sh`。署名は実行していない |
| インストーラーの署名（Developer ID Installer） | ⬜ | Developer ID Installer 証明書の発行が必要 |
| 公証（Notarization）と staple | ⬜ | `xcrun notarytool store-credentials` でプロファイルを作り、`NOTARY_PROFILE` を設定。Apple へのアップロードを伴うため、識別子とライセンスの確定後に行う |
| バージョン番号 | 🟡 | `CMakeLists.txt` の `project(... VERSION 0.1.0)` |
| ライセンス表記 | ⬜ | JUCE ほか、配布物に含める表記（ライセンスの判断後） |
| ユーザーガイド | ✅ | `docs/USER_GUIDE.md` |
| 紹介ページ | 🟡 | `site/index.html`（実画面とデモ音源を掲載。GitHub Pages で公開済み。ダウンロードの導線は未設置） |

## 配布用パッケージを作る手順（判断と証明書がそろった後）

```bash
scripts/build.sh Release
scripts/test.sh Release
PLUGINVAL_STRICTNESS=10 scripts/validate.sh Release

export DEVELOPER_ID_APPLICATION="Developer ID Application: …"
export DEVELOPER_ID_INSTALLER="Developer ID Installer: …"
export NOTARY_PROFILE="…"          # xcrun notarytool store-credentials で作成
scripts/package.sh                  # 署名 → 公証 → staple まで行う
```

出来上がった `build/dist/POLYBRISK-<version>.pkg` を、別の Mac（開発環境のないもの）でインストールし、Cubase と Logic Pro でスキャン・発音・保存・復元を確認する。
