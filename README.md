# POLYBRISK

SuperSAW / Synth Brass / Stab に特化した macOS 用ポリフォニック・シンセサイザー（AU / VST3 / Standalone）。

- 製品ページ: <https://cheebow.github.io/POLYBRISK/>

- 製品要件の正本: [`SPEC.md`](SPEC.md)（追加仕様: [`docs/ADDON_TRANCE_GATE_ARPEGGIATOR.md`](docs/ADDON_TRANCE_GATE_ARPEGGIATOR.md)）
- 開発ルール: [`CLAUDE.md`](CLAUDE.md)
- 設計: [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) / パラメーター: [`docs/PARAMETER_IDS.md`](docs/PARAMETER_IDS.md) / 判断ログ: [`docs/DECISIONS.md`](docs/DECISIONS.md) / テスト: [`docs/TEST_PLAN.md`](docs/TEST_PLAN.md)

**現在の状態: 全フェーズの実装と自動テストまで完了。配布はまだできません。** 16 音ポリフォニー、OSC A/B とユニゾン、3 キャラクターのフィルター、Transient、ピッチベンド / ビブラート、4 マクロ（MIDI Learn）、LFO、Mono / Legato とグライド、Sub / Noise、FX 7 種、Trance Gate（ステレオ対応）/ Arpeggiator、Simple / Advanced の UI、プリセットブラウザー、Factory 128 音色が動きます。Cubase（VST3）では、発音・プロジェクトの保存と復元・テンポ同期・オートメーション・書き出しを確認済みです。Logic Pro（AU）では、読み込みと発音まで確認済みです。残っているのは、Logic Pro と Cubase の残りの項目（`docs/TEST_PLAN.md`）、識別子とライセンスの確定、署名・公証です（`docs/RELEASE_CHECKLIST.md`）。

## 必要な環境

| | 検証済み |
|---|---|
| macOS | 26.7.3（Apple Silicon）。ビルド対象は macOS 13.0 以降 / arm64 |
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
| VST3 | `VST3/POLYBRISK.vst3` |
| AU | `AU/POLYBRISK.component` |
| Standalone | `Standalone/POLYBRISK.app` |

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
scripts/package.sh          # インストーラー（署名なしの .pkg）を build/dist/ に作る
```

## 動かしてみる

- **Standalone**: `open build/Release/PolyBrisk_artefacts/Release/Standalone/POLYBRISK.app`
  ウインドウ左上の **Options → Audio/MIDI Settings…** で出力デバイスと MIDI 入力を選び、MIDI キーボードを弾きます。
  画面下の鍵盤をクリックして試奏できます。
- **Cubase**: VST3 を再スキャンし、Instrument Track に POLYBRISK を挿入。
- **Logic Pro**: Audio Units の一覧（POLYBRISK > POLYBRISK）から Software Instrument として挿入。

使い方は [`docs/USER_GUIDE.md`](docs/USER_GUIDE.md) を参照してください。上部のプリセット名をクリックするとブラウザーが開きます。

**SuperSAW を鳴らすには**: Advanced の OSC タブで Wave を SuperSAW にすると、ユニゾンが 1 本のときは自動で 7 本になります。ホストの汎用パラメーター表示から Wave だけを変えた場合は 1 本のままなので、`Unison Voices` も設定してください（`docs/DECISIONS.md` D-016）。

## アイコン・ロゴ・紹介ページ

- `assets/branding/` — `icon.svg`、`logo-mark.svg`、`logo-wordmark-dark.svg` / `-light.svg`（SVG が正本）。
  `scripts/make_icons.sh` で `icon-1024.png` と `POLYBRISK.icns` を再生成します。
- `site/index.html` — 製品紹介ページ（依存なしの静的 HTML）。`open site/index.html` で確認できます。`main` に push すると、GitHub Actions（`.github/workflows/pages.yml`）が <https://cheebow.github.io/POLYBRISK/> に公開します。

## ライセンスに関する注意

**このリポジトリのライセンスは、まだ決めていません。** `LICENSE` ファイルを置くまで、ソースコード・音色・画像・音源の再利用や再配布の許諾はありません（閲覧のための公開です）。

JUCE 9 は AGPLv3 と商用ライセンス（JUCE 9 EULA）のデュアルライセンスです。ビルドしたプラグインを配布する前に、どちらで配布するかを決める必要があります（`docs/DECISIONS.md` D-002）。JUCE 本体はこのリポジトリに含めず、ビルド時に取得します。プラグインの識別子（Bundle ID、AU/VST3 コード）も現在は仮の値です（D-005）。
