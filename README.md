# POLYBRISK

SuperSAW / Synth Brass / Stab に特化した macOS 用ポリフォニック・シンセサイザー（AU / VST3 / Standalone）。

音色を選んで、4 つのノブで曲に合わせれば、すぐに使えます。128 のプリセットが入っています。

音を聞きたい方、画面を見たい方は [紹介ページ](https://cheebow.github.io/POLYBRISK/) をどうぞ。使い方は [ユーザーガイド](docs/USER_GUIDE.md) にあります。

## インストール

[最新のリリース](https://github.com/cheebow/POLYBRISK/releases/latest) からインストーラ（`.pkg`）をダウンロードして開き、画面の案内に沿って進めてください。VST3、Audio Unit、スタンドアロンアプリの中から、入れたいものを選べます。

- macOS 12 以降
- Apple Silicon。Intel Mac でも動くように作っていますが、実機ではまだ試せていません。
- Apple の公証済みです

### アンインストール

次のものを削除します。自分で保存したプリセットと設定も消すなら、最後の 1 つも削除してください。

```text
/Library/Audio/Plug-Ins/VST3/POLYBRISK.vst3
/Library/Audio/Plug-Ins/Components/POLYBRISK.component
/Applications/POLYBRISK.app
~/Library/Application Support/POLYBRISK
```

## 開発者向け

ここから下は、ソースからビルドする人向けの説明です。

- 製品要件の正本: [`SPEC.md`](SPEC.md)（追加仕様: [`docs/ADDON_TRANCE_GATE_ARPEGGIATOR.md`](docs/ADDON_TRANCE_GATE_ARPEGGIATOR.md)）
- 開発ルール: [`CLAUDE.md`](CLAUDE.md)
- 設計: [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) / パラメーター: [`docs/PARAMETER_IDS.md`](docs/PARAMETER_IDS.md) / 判断ログ: [`docs/DECISIONS.md`](docs/DECISIONS.md) / テスト: [`docs/TEST_PLAN.md`](docs/TEST_PLAN.md) / 配布の手順: [`docs/RELEASE_CHECKLIST.md`](docs/RELEASE_CHECKLIST.md)

**現在の状態: 0.1.0 を公開しました。** Cubase（VST3）では、発音・プロジェクトの保存と復元・テンポ同期・オートメーション・書き出しを確認済みです。Logic Pro（Audio Unit）では、読み込みと発音まで確認済みです。公開した 0.1.0 は、インストールして DAW で読み込み、音が出ることを確認済みです。Intel Mac の実機と、残りの DAW の確認項目（`docs/TEST_PLAN.md`）は未確認です。

## 必要な環境

| | 検証済み |
|---|---|
| macOS | 26.7.3（Apple Silicon）。ビルド対象は macOS 12.0 以降。開発用のビルドは arm64、配布用（`scripts/package.sh`）は Universal |
| Xcode | 27.2（Command Line Tools を含む） |
| CMake | 4.4.5（3.22 以上が必要） |
| JUCE | 9.0.6。初回の CMake 構成時に `external/_deps/` へ自動取得（ネットワーク接続が必要） |

## ビルド

```bash
scripts/build.sh            # Debug と Release
scripts/build.sh Release    # 片方だけ
```

成果物は `build/<Config>/PolyBrisk_artefacts/<Config>/` 以下:

| 形式 | パス |
|---|---|
| VST3 | `VST3/POLYBRISK Dev.vst3` |
| AU | `AU/POLYBRISK Dev.component` |
| Standalone | `Standalone/POLYBRISK Dev.app` |

**開発用のビルドは「POLYBRISK Dev」という別のプラグインになります。** 名前も識別子も製品版（インストーラーで入れる「POLYBRISK」）と違うので、両方を入れても重複せず、DAW には別々のプラグインとして並びます。画面のロゴの横には `DEV` と出ます。ユーザープリセットと設定は、製品版と共通です（`~/Library/Application Support/POLYBRISK`）。開発用で保存した DAW のプロジェクトは、製品版では開けません。

ビルド後、AU と VST3 は `~/Library/Audio/Plug-Ins/Components` と `~/Library/Audio/Plug-Ins/VST3` へ自動コピーされます（最後にビルドした構成が残ります）。コピーしたくない場合:

```bash
POLYBRISK_COPY_PLUGIN=OFF scripts/build.sh
```

## テスト

```bash
scripts/test.sh Debug       # JUCE UnitTest ランナーを CTest 経由で実行
scripts/validate.sh Release # auval（pluginval があれば VST3 / AU も検証）
scripts/bench.sh            # DSP の CPU 計測（Release）
scripts/render_presets.sh   # Factory 128 音色を build/renders/*.wav に書き出す（試聴用）
scripts/package.sh          # インストーラー（Universal の .pkg）を build/dist/package/ に作る。署名と公証は docs/RELEASE_CHECKLIST.md
```

## 動かしてみる

- **Standalone**: `open "build/Release/PolyBrisk_artefacts/Release/Standalone/POLYBRISK Dev.app"`
  ウインドウ左上の **Options → Audio/MIDI Settings…** で出力デバイスと MIDI 入力を選び、MIDI キーボードを弾きます。
  画面下の鍵盤をクリックして試奏できます。
- **Cubase**: VST3 を再スキャンし、Instrument Track に POLYBRISK Dev を挿入。
- **Logic Pro**: Audio Units の一覧（CHEEBOW > POLYBRISK Dev）から Software Instrument として挿入。

使い方は [`docs/USER_GUIDE.md`](docs/USER_GUIDE.md) を参照してください。上部のプリセット名をクリックするとブラウザーが開きます。

**SuperSAW を鳴らすには**: Advanced の OSC タブで Wave を SuperSAW にすると、ユニゾンが 1 本のときは自動で 7 本になります。ホストの汎用パラメーター表示から Wave だけを変えた場合は 1 本のままなので、`Unison Voices` も設定してください（`docs/DECISIONS.md` D-016）。

## アイコン・ロゴ・紹介ページ

- `assets/branding/` — `icon.svg`、`logo-mark.svg`、`logo-wordmark-dark.svg` / `-light.svg`（SVG が正本）。
  `scripts/make_icons.sh` で `icon-1024.png` と `POLYBRISK.icns` を再生成します。
- `site/index.html` — 製品紹介ページ（依存なしの静的 HTML）。`open site/index.html` で確認できます。`main` に push すると、GitHub Actions（`.github/workflows/pages.yml`）が <https://cheebow.github.io/POLYBRISK/> に公開します。

## ライセンス

POLYBRISK は [GNU Affero General Public License v3.0](LICENSE) で公開しています。使っているソフトウェアは [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) にまとめてあります。JUCE 本体はこのリポジトリに含めず、ビルド時に取得します。
