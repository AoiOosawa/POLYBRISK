# POLYBRISK — Architecture

全フェーズ実装後（SPEC v0.8）の構成と、設計上の約束をまとめる。製品要件の正本は `SPEC.md`。

## レイヤーと依存方向

```text
            +--------------------------------------------------+
            |  Plugin wrappers (JUCE: AU / VST3 / Standalone)  |
            +------------------------+-------------------------+
                                     |
   src/ui  -------------->  src/PluginProcessor / PluginEditor
                                     |
                 +-------------------+-------------------+
                 |                                       |
        src/state (JUCE 依存)              src/synth, src/fx, src/rhythm
   Parameters / StateSerializer            polybrisk_dsp: 純粋 C++
   MetaState / PresetManager               JUCE・UI・I/O・JSON に非依存
                 |                                       ^
                 +------- POD スナップショット -----------+
```

- 依存は上から下への一方向。`polybrisk_dsp` は JUCE をリンクしない静的ライブラリで、**入力は POD 構造体（`EngineParams`）とノートイベントのみ**。
- `src/state` は APVTS とシリアライズを担当し、DSP の内部構造を知らない。
- UI は APVTS・`MetaState`・`PresetManager` とプロセッサーの公開 API だけを使い、DSP へ直接触れない。

## CMake ターゲット

| ターゲット | 内容 | 依存 |
|---|---|---|
| `polybrisk_dsp` | `src/synth`、`src/fx`、`src/rhythm` | なし（標準ライブラリのみ） |
| `PolyBriskPresets` | `presets/factory/*.json` を組み込んだバイナリデータ（`PresetData.h`） | JUCE（生成のみ） |
| `PolyBrisk` | `juce_add_plugin`。`_AU` / `_VST3` / `_Standalone` を生成 | `polybrisk_dsp`、`PolyBriskPresets`、JUCE |
| `polybrisk_tests` | JUCE UnitTest ランナー。CTest に登録 | `polybrisk_dsp`、JUCE、state/processor のソース |
| `polybrisk_bench` | DSP ベンチマーク（`scripts/bench.sh`） | `polybrisk_dsp` のみ |
| `polybrisk_render` | Factory プリセットの WAV 書き出し（`scripts/render_presets.sh`）、音量の計測（`tools/level_presets.py`）、UI の画像書き出し | `polybrisk_dsp`、JUCE、state/processor/ui のソース |

## スレッドモデル

| スレッド | 許可される処理 |
|---|---|
| オーディオ | `processBlock`。パラメーターの atomic 読み取り、事前構築済みスナップショットの参照、DSP。**確保・ロック・ファイル I/O・JSON/ValueTree 操作・ログは禁止** |
| メッセージ | UI、プリセット読込、状態シリアライズ、マクロ割当の編集、スナップショット構築 |

`processBlock` の流れ:

1. `ScopedNoDenormals`。
2. 画面内鍵盤（Standalone）のノートを MIDI に混ぜる。プリセット切替の直後なら Arp をリセットする。
3. 新しいマクロ設定 / Gate パターンがあれば取り込む（**try-lock のみ。取れなければ次のブロック**）。
4. APVTS の `std::atomic<float>` から Base 値を読み、マクロのオフセットを正規化空間で加えて Effective 値にする（スタック上の配列）。そこから `EngineParams`（POD）を組み立てる。
5. `AudioPlayHead` からテンポ・再生状態・PPQ を読み、`TransportInfo`（POD）として `SynthEngine` に渡す。
6. MIDI イベントのサンプル位置でブロックを分割し、区間ごとに `SynthEngine::render`。イベントは位置に達した時点で適用（サンプル精度）。

### メッセージスレッドとの受け渡し

| 向き | 内容 | 方法 |
|---|---|---|
| メッセージ → オーディオ | マクロ割当（32 エントリ固定の POD）、Gate パターン（32 要素） | `MetaState` の変更時にスナップショットを作り、スピンロックで保護した領域へコピー。オーディオ側は try-lock でコピーを取る（確保・解放なし、待機なし） |
| メッセージ → オーディオ | 「Arp をリセットせよ」（プリセット切替・状態復元） | atomic フラグ |
| オーディオ → メッセージ | CC で動かしたマクロの値、MIDI Learn の結果、「パラメーターが変わった」（Dirty 表示用） | atomic に書き、30 Hz のタイマーが読んでホストパラメーター / `MetaState` に反映 |
| オーディオ → UI | Gate の現在ステップ、マクロ適用後の実効値 | atomic（UI は 20 Hz のタイマーで読む） |

## パラメーター

- 単一レジストリ `src/state/Parameters.cpp`。ここから APVTS レイアウトを生成し、状態保存・プリセットのキーにも同じ ID を使う。
- 規約と一覧は `docs/PARAMETER_IDS.md`。レジストリとの一致をテストで検査する。

## 状態保存

- `StateSerializer` がホスト state とプリセット（Phase 5）の共通経路。形式と互換ルールは `docs/DECISIONS.md` D-010。
- `getStateInformation` / `setStateInformation` は `StateSerializer` を呼ぶだけ。

## DSP

```text
SynthEngine (ホスト側レート)
 ├─ MIDI: noteOn / noteOff / CC64 / CC1 / Pitch Bend / CC123 / CC120   ← processBlock がサンプル位置で呼ぶ
 ├─ RhythmClock: ホストの PPQ (再生中) または内部クロックから、サンプルごとの位置 (拍) を出す
 ├─ Arpeggiator (ON のとき): 押鍵集合からノートを生成。イベントの位置でレンダリング区間を切る
 ├─ renderVoices (内部レート = 44.1/48 kHz では 2x)
 │    ├─ コントロールレート (32 サンプルごと、絶対位置にそろえる)
 │    │     ピッチ・Detune・Spread・Pulse Width・Cutoff・Resonance・Drive・Bend・Vibrato のスムージング、
 │    │     UnisonLayout、ADSR 係数、FilterSettings、TransientControl
 │    │     → 全ボイス共通の VoiceControl を 1 回だけ計算
 │    │     → 発音中の各ボイスに controlTick (カットオフの目標値とピッチオフセットを更新)
 │    └─ Voice × 16
 │          UnisonOscillator A / B (各最大 9 声。Pitch Snap + Bend + Vibrato 込みのピッチ) → OSC レベル
 │            + NoiseBurst (Transient)
 │            → StereoFilter (Analog / Clean / Punch、Filter EG・Key Track・Velocity で変調)
 │            → Amp ADSR × Velocity → 加算
 ├─ HalfbandDecimator L / R (2x のときのみ)
 ├─ FxChain: Drive → Chorus → EQ → Imager → [Trance Gate のゲイン] → Dry + Delay wet + Reverb wet
 └─ Preset Level × Output Gain (スムージング) → 安全用ソフトリミッター
```

| ファイル | 役割 |
|---|---|
| `synth/Oscillator.h` | `Waveform` と、バンド制限した 1 サンプル生成関数（PolyBLEP Saw / Pulse、PolyBLAMP Triangle、Sine） |
| `synth/Unison.*` | `UnisonLayout`（デチューン比・パン・ゲイン）の計算と、最大 9 声の `UnisonOscillator` |
| `synth/Envelopes.*` | `Adsr` と、サンプルレート依存の `AdsrCoefficients` |
| `synth/Filters.*` | `StereoFilter`（TPT / SVF 2 段、3 キャラクター × 5 モード）と、全ボイス共通の `FilterSettings` |
| `synth/Transient.h` | `TransientControl`（Noise Burst と Pitch Snap の係数）と、ボイスごとの `NoiseBurst` |
| `synth/Voice.*` | 1 発音。鍵盤/ペダルの状態、スティール時のフェードと発音待ち、フィルターと Filter EG、Transient |
| `synth/Halfband.h` | 2:1 デシメーション用 IIR ハーフバンドフィルター |
| `synth/SynthEngine.*` | 16 ボイス管理、スティール、パラメーターのスムージング、オーバーサンプリング、Bend / Vibrato、FX と RHYTHM の呼び出し、出力段 |
| `fx/FxParams.h` | FX のパラメーター（POD）と選択肢の enum |
| `fx/Drive.h` `Chorus.h` `Equalizer.h` `Imager.h` `Delay.h` `Reverb.h` | 各 FX（D-032） |
| `fx/FxChain.*` | FX の順序、ON/OFF のクロスフェード、Delay / Reverb の並列ミックス、無音時の停止 |
| `rhythm/RhythmClock.h` | 位置（拍）の管理、ステップの位置計算（Swing）、不連続の検出 |
| `rhythm/GatePattern.h` `TranceGate.h` | Gate のパターン（固定長 POD）とゲイン計算 |
| `rhythm/Arpeggiator.*` | 押鍵・CC64・Latch の状態機械、発音順序、サンプル精度の Note On / Off |

設計上の要点:

- **確保なし**: すべての状態は固定長配列。作業バッファはスタック上の小さな配列（最大 256 サンプル × 2ch）。
- **ブロック分割に依存しない出力**: コントロールレートの更新位置を絶対サンプル位置にそろえているため、ホストのバッファ長や MIDI イベントによる分割が変わっても同じ出力になる（テストで検査）。
- **全ボイス共通の計算は 1 回だけ**: ユニゾンの `exp2` / `cos` / `sin` や ADSR 係数はエンジンがコントロールレートで計算し、ボイスは参照するだけ。
- **スムージング**: ピッチ系とユニゾン系は 5 ms（コントロールレート）、OSC レベルと出力ゲインは 10 ms（サンプル単位）。OSC を OFF にしてもレベルが下がり切るまで発振を続けてクリックを防ぐ。
- **カットオフの変調**: 各ボイスはコントロール周期の先頭（`controlTick`）で目標のカットオフを決め、次の周期までの間は係数 g をサンプル単位で線形補間する。周期は絶対サンプル位置にそろっているので、ブロック分割に依存しない。ノート開始時だけは補間せず目標値へ直接合わせる。
- レベル設計・ユニゾン・オーバーサンプリング・ボイス管理の詳細と根拠は `docs/DECISIONS.md` D-016〜D-022、フィルター・Transient は D-024〜D-029。

## state 層（JUCE 依存、メッセージスレッド）

| ファイル | 役割 |
|---|---|
| `state/Parameters.*` | パラメーターの単一レジストリ（ID・範囲・既定値・スキュー・選択肢）。APVTS レイアウト、正規化、Rate の変換 |
| `state/MacroEngine.h` | マクロの評価（POD と純粋関数。オーディオスレッドから使える） |
| `state/MetaState.*` | ホストパラメーターにしない状態: マクロ名・CC・割当、Gate パターン、現在のプリセット情報、Dirty、表示モード |
| `state/StateSerializer.*` | ホスト状態 ⇄ ツリー（`POLYBRISK_STATE`）。プリセットも同じ経路で適用する |
| `state/PresetSerializer.*` | プリセット JSON ⇄ ツリー（`docs/PRESET_SCHEMA.md`） |
| `state/PresetManager.*` | Factory / User の一覧、検索、保存・削除・Import / Export、お気に入り、Performance Lock |

## UI（`src/ui`）

| ファイル | 役割 |
|---|---|
| `ui/Theme.*` | 配色、`LookAndFeel`（ノブ・ボタン・タブ）、ロゴの描画 |
| `ui/Controls.*` | パラメーターに結び付いたノブ / 選択 / トグルと、折り返しレイアウトの `Section` |
| `ui/Views.*` | `SimpleView`、`AdvancedView`（6 タブ）、`MacroEditor`、`GateGrid`、`PresetBrowser` |
| `PluginEditor.*` | ヘッダー、表示の切り替え、拡大縮小、Standalone の鍵盤、20 Hz の更新タイマー |

Simple と Advanced は同じ APVTS / `MetaState` を見ているだけで、切り替えは表示の入れ替えにすぎない。

## ツール

- `tools/make_factory_presets.py`: Factory プリセットの定義（人が書いた値）を JSON に書き出す。
- `tools/level_presets.py`: `polybrisk_render --levels` の実測から `preset.level` だけを調整する（`output.gain` はプリセットに含めない。D-055）。
- `tools/RenderPresets.cpp`（`polybrisk_render`）: プロセッサーをオフラインで駆動して WAV を書き出す。エディターの画像も書き出せる（UI の確認と紹介ページ用）。

## Rhythm の約束

- `src/rhythm/` は `polybrisk_dsp` の一部で、JUCE に依存しない。ホスト情報は POD で受け取る。
- 固定長配列のみ。音声スレッドで確保・ロック・文字列処理をしない。
- Arp の Random は決定的。ステップの判定はブロック長や分割位置で結果が変わらない（テストで検査）。
- 汎用シーケンサーや外部 MIDI 出力へ拡張しない（追加仕様 §11-7）。
- 仕様にない点の判断は `docs/DECISIONS.md` D-034。

## ディレクトリ

```text
src/synth  src/fx  src/rhythm     DSP (polybrisk_dsp)
src/state                         パラメーター・状態・プリセット
src/ui  src/PluginEditor.*        UI
src/PluginProcessor.*             ホストとの接点
presets/factory                   Factory プリセット (JSON、バイナリに組み込み)
tests                             自動テスト (dsp / fx / rhythm / midi / state) とベンチマーク
tools                             プリセット生成・音量調整・レンダリング
scripts                           build / test / validate / bench / render_presets / package / make_icons
docs                              仕様の補足、判断の記録、テスト計画、ユーザーガイド
assets/branding  site             アイコン・ロゴ、製品紹介ページ
```
