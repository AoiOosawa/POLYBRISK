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
| インストーラーの作成 | ✅ | `scripts/package.sh` が Universal の `build/dist/package/POLYBRISK-<version>-macOS.pkg` を作る。VST3 / Audio Unit / Standalone を選んで入れられる。**0.1.0 を開発機にインストールして確認済み**（2026-11-23）: 3 つが `/Library/Audio/Plug-Ins/VST3`・`/Library/Audio/Plug-Ins/Components`・`/Applications` に入り、署名が有効。入った製品版で `auval -v aumu Pbrk Chbw` と pluginval（strictness 10、VST3 / AU）が合格。開発用の「POLYBRISK Dev」とは重複しない |
| コード署名（Developer ID Application） | ✅ | 2026-11-23、0.1.0。VST3 / Audio Unit / Standalone アプリに、Hardened Runtime 付きで署名（`SIGN_APP`） |
| インストーラーの署名（Developer ID Installer） | ✅ | 2026-11-23、0.1.0（`SIGN_PKG`） |
| 公証（Notarization）と staple | ✅ | 2026-11-23、0.1.0。Apple の公証に合格し、チケットを添付した。`spctl` の判定は「Notarized Developer ID」（インストーラーと、中のアプリの両方） |
| バージョン番号 | ✅ | 最初の配布は **0.1.0**（`CMakeLists.txt` の `project(... VERSION 0.1.0)`）。ユーザーが決定 |
| ライセンス表記 | ✅ | `LICENSE`（AGPL-3.0）と `THIRD_PARTY_NOTICES.md`。インストーラーのライセンスの画面に両方を表示する |
| ユーザーガイド | ✅ | `docs/USER_GUIDE.md` |
| 紹介ページ | ✅ | `site/index.html`。GitHub Pages で公開済み（<https://cheebow.github.io/POLYBRISK/>）。ダウンロードのボタンは、GitHub の最新のリリースへ案内する |
| リリース | ✅ | **0.1.0 を 2026-11-23 に公開**: <https://github.com/cheebow/POLYBRISK/releases/tag/v0.1.0>（`POLYBRISK-0.1.0-macOS.pkg`）。タグ `v0.1.0` は、インストーラーをビルドしたコミットを指す。公開後、インストールした製品版を DAW で読み込み、発音とキー操作（クリック後のスペースキー）をユーザーが確認済み |

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

**開発用のビルドとの関係**: インストーラーは製品版「POLYBRISK」を `/Library/Audio/Plug-Ins/` に入れる。開発用のビルド（`scripts/build.sh`）は、別のプラグイン「POLYBRISK Dev」を `~/Library/Audio/Plug-Ins/` にコピーする。名前も識別子も違うので、両方を入れても重複しない（D-078）。インストール後の製品版の AU は `auval -v aumu Pbrk Chbw` で検証できる。
