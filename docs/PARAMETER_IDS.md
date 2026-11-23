# POLYBRISK — Parameter IDs

正本は `src/state/Parameters.h/.cpp` の単一レジストリ。この文書の「実装済み」表は
`tests/state/ParameterRegistryTests.cpp` がレジストリとの一致を検査する（ID の過不足でテスト失敗）。

## ルール

- 形式: `section.subsection.name`（小文字始まりの camelCase をドットで連結）。例: `osc.a.wave`、`filter.envAmount`。
- **公開後は ID を変更・削除しない。** 不要になったパラメーターは非表示にし、互換マイグレーションで対応する（SPEC §12）。
- APVTS へは `juce::ParameterID { id, versionHint }` で登録する。初版は全て `versionHint = 1`。後のリリースで追加するパラメーターには、そのリリースの番号を与える（AU のパラメーター順序を安定させるため）。
- 選択肢（Choice）の**順序は追加のみ可**。途中への挿入・並べ替えは保存済み状態を壊す。
- 状態保存・プリセット JSON のキーは Parameter ID と同一。値は実値（非正規化）で保存する。
- マクロ割当の対象は「連続」パラメーターのみ（SPEC §10.1）。ただしマクロ自身と `output.gain` は割当先にできない。

## 実装済み (140 個)

<!-- implemented:begin -->
| ID | 名称 | 種別 | 範囲 | 既定値 | マクロ | 備考 |
|---|---|---|---|---|---|---|
| `osc.a.enabled` | OSC A Enabled | ON/OFF | — | ON | 不可 | |
| `osc.a.wave` | OSC A Wave | 選択 | Saw / SuperSAW / Pulse / Square / Triangle / Sine / Noise | Saw | 不可 | SuperSAW の DSP は Saw と同一（D-016） |
| `osc.a.octave` | OSC A Octave | 整数 | -2〜+2 | 0 | 不可 | Noise では無視 |
| `osc.a.semitone` | OSC A Semitone | 整数 | -12〜+12 st | 0 | 不可 | Noise では無視 |
| `osc.a.fine` | OSC A Fine | 連続 | -100〜+100 cent | 0 | 可 | Noise では無視 |
| `osc.a.level` | OSC A Level | 連続 | 0〜100 % | 100 | 可 | 線形振幅 |
| `osc.a.unisonVoices` | OSC A Unison Voices | 整数 | 1〜9 | 1 | 不可 | SuperSAW の既定 7 本は UI 側で設定（D-016） |
| `osc.a.detune` | OSC A Detune | 連続 | 0〜100 % | 35 | 可 | 最外声 ±100 cent（100 % 時）。カーブは D-017 |
| `osc.a.spread` | OSC A Stereo Spread | 連続 | 0〜100 % | 50 | 可 | |
| `osc.a.phaseMode` | OSC A Phase Mode | 選択 | Free / Retrigger | Retrigger | 不可 | |
| `osc.a.pulseWidth` | OSC A Pulse Width | 連続 | 5〜95 % | 50 | 可 | Pulse 選択時のみ有効 |
| `osc.a.unisonMix` | OSC A Unison Mix | 連続 | 0〜100 % | 50 | 可 | 中央の声と外側の声の音量比。50 % で全声が同じ、0 % で中央だけ、100 % で外側だけ。合計の音量は一定。ユニゾン 3 本以上で効く（D-045） |
| `osc.b.enabled` | OSC B Enabled | ON/OFF | — | OFF | 不可 | |
| `osc.b.wave` | OSC B Wave | 選択 | Saw / SuperSAW / Pulse / Square / Triangle / Sine / Noise | Saw | 不可 | SuperSAW の DSP は Saw と同一（D-016） |
| `osc.b.octave` | OSC B Octave | 整数 | -2〜+2 | 0 | 不可 | Noise では無視 |
| `osc.b.semitone` | OSC B Semitone | 整数 | -12〜+12 st | 0 | 不可 | Noise では無視 |
| `osc.b.fine` | OSC B Fine | 連続 | -100〜+100 cent | 0 | 可 | Noise では無視 |
| `osc.b.level` | OSC B Level | 連続 | 0〜100 % | 70 | 可 | 線形振幅 |
| `osc.b.unisonVoices` | OSC B Unison Voices | 整数 | 1〜9 | 1 | 不可 | SuperSAW の既定 7 本は UI 側で設定（D-016） |
| `osc.b.detune` | OSC B Detune | 連続 | 0〜100 % | 35 | 可 | 最外声 ±100 cent（100 % 時）。カーブは D-017 |
| `osc.b.spread` | OSC B Stereo Spread | 連続 | 0〜100 % | 50 | 可 | |
| `osc.b.phaseMode` | OSC B Phase Mode | 選択 | Free / Retrigger | Retrigger | 不可 | |
| `osc.b.pulseWidth` | OSC B Pulse Width | 連続 | 5〜95 % | 50 | 可 | Pulse 選択時のみ有効 |
| `osc.b.unisonMix` | OSC B Unison Mix | 連続 | 0〜100 % | 50 | 可 | OSC A と同じ（D-045） |
| `amp.attack` | Amp Attack | 連続 | 0.1〜10000 ms | 2 | 可 | スキュー中央 100 ms |
| `amp.decay` | Amp Decay | 連続 | 1〜10000 ms | 300 | 可 | スキュー中央 300 ms |
| `amp.sustain` | Amp Sustain | 連続 | 0〜100 % | 80 | 可 | |
| `amp.release` | Amp Release | 連続 | 1〜20000 ms | 200 | 可 | スキュー中央 400 ms |
| `amp.velocity` | Amp Velocity | 連続 | 0〜100 % | 100 | 可 | Velocity → Amp Level の量。gain = 1 − a + a·velocity（D-027） |
| `transient.enabled` | Transient Enabled | ON/OFF | — | OFF | 不可 | |
| `transient.mode` | Transient Mode | 選択 | Noise Burst / Pitch Snap / Both | Noise Burst | 不可 | |
| `transient.amount` | Transient Amount | 連続 | 0〜100 % | 50 | 可 | ノイズ音量と Pitch Snap の深さの両方に掛かる（D-026） |
| `transient.duration` | Transient Duration | 連続 | 1〜150 ms | 20 | 可 | この時間で −60 dB。スキュー中央 30 ms |
| `transient.noiseColor` | Transient Noise Color | 連続 | 0〜100 % | 50 | 可 | 0 = Dark、50 = ほぼホワイト、100 = Bright |
| `transient.pitchSnap` | Transient Pitch Snap | 連続 | -24〜+24 st | 12 | 可 | ここから 0 へ収束。OSC A/B 共通 |
| `filter.character` | Filter Character | 選択 | Analog / Clean / Punch | Analog | 不可 | D-024 |
| `filter.mode` | Filter Mode | 選択 | LP12 / LP24 / HP12 / HP24 / BP12 | LP24 | 不可 | |
| `filter.cutoff` | Filter Cutoff | 連続 | 20〜20000 Hz | 20000 | 可 | スキュー中央 632 Hz（対数に近い操作感） |
| `filter.resonance` | Filter Resonance | 連続 | 0〜100 % | 0 | 可 | |
| `filter.drive` | Filter Drive | 連続 | 0〜100 % | 0 | 可 | 入力ゲイン 0〜+24 dB |
| `filter.envAmount` | Filter Env Amount | 連続 | -100〜+100 % | 0 | 可 | ±100 % = ±8 オクターブ（D-025） |
| `filter.keyTrack` | Filter Key Track | 連続 | 0〜100 % | 0 | 可 | 100 % でノート 60 を基準に 1:1 |
| `filter.velocity` | Filter Velocity | 連続 | 0〜100 % | 0 | 可 | 100 % のときベロシティ 0 で −5 オクターブ（D-025） |
| `filterEnv.attack` | Filter Env Attack | 連続 | 0.1〜10000 ms | 2 | 可 | スキュー中央 100 ms |
| `filterEnv.decay` | Filter Env Decay | 連続 | 1〜10000 ms | 300 | 可 | スキュー中央 300 ms |
| `filterEnv.sustain` | Filter Env Sustain | 連続 | 0〜100 % | 30 | 可 | |
| `filterEnv.release` | Filter Env Release | 連続 | 1〜20000 ms | 200 | 可 | スキュー中央 400 ms |
| `performance.bendUp` | Bend Up | 整数 | 0〜48 st | 2 | 不可 | Symmetric では上下の両方に使う |
| `performance.bendDown` | Bend Down | 整数 | 0〜48 st | 2 | 不可 | Independent のときだけ有効 |
| `performance.bendMode` | Bend Mode | 選択 | Symmetric / Independent | Symmetric | 不可 | |
| `performance.vibratoDepth` | Vibrato Depth | 連続 | 0〜200 cent | 35 | 可 | CC1 = 127 のときの深さ |
| `performance.vibratoRate` | Vibrato Rate | 連続 | 0.1〜12 Hz | 5.5 | 可 | |
| `performance.vibratoDelay` | Vibrato Delay | 連続 | 0〜2000 ms | 0 | 可 | スキュー中央 400 ms。Delay の半分の時間で立ち上がる（D-030） |
| `performance.aftertouchVibrato` | Aftertouch Vibrato | 連続 | 0〜200 cent | 0 | 可 | アフタータッチ最大で加わるビブラートの深さ（D-041） |
| `performance.aftertouchCutoff` | Aftertouch Cutoff | 連続 | -100〜100 % | 0 | 可 | ±100 % で ±4 オクターブ |
| `voice.mode` | Voice Mode | 選択 | Poly / Mono / Legato | Poly | 不可 | Mono はノートごとにエンベロープを再始動、Legato は押さえたまま次を弾くと再始動しない |
| `voice.glide` | Glide Time | 連続 | 0〜2000 ms | 0 | 可 | スキュー中央 200 ms。0 でグライドなし |
| `sub.level` | Sub Level | 連続 | 0〜100 % | 0 | 可 | 0 で鳴らない |
| `sub.octave` | Sub Octave | 選択 | -1 Oct / -2 Oct | -1 Oct | 不可 | |
| `sub.wave` | Sub Wave | 選択 | Sine / Square | Sine | 不可 | |
| `noise.level` | Noise Level | 連続 | 0〜100 % | 0 | 可 | 持続するノイズ層（Transient の Noise Burst とは別） |
| `noise.color` | Noise Color | 連続 | 0〜100 % | 50 | 可 | 0 = Dark、50 = ほぼホワイト、100 = Bright |
| `lfo.wave` | LFO Wave | 選択 | Sine / Triangle / Saw / Square / S&H | Sine | 不可 | |
| `lfo.sync` | LFO Sync | ON/OFF | — | OFF | 不可 | ON で `lfo.division`、OFF で `lfo.rate` を使う |
| `lfo.rate` | LFO Rate | 連続 | 0.05〜20 Hz | 2 | 可 | スキュー中央 2 Hz |
| `lfo.division` | LFO Division | 選択 | 4 Bars / 2 Bars / 1 Bar / 1/2 / 1/4 / 1/8 / 1/16 / 1/4 T / 1/8 T / 1/16 T / 1/4 D / 1/8 D | 1 Bar | 不可 | 1 周期の長さ |
| `lfo.fade` | LFO Fade In | 連続 | 0〜5000 ms | 0 | 可 | スキュー中央 800 ms。ノートオンからの立ち上がり |
| `lfo.retrigger` | LFO Key Sync | ON/OFF | — | OFF | 不可 | ON でノートごとに位相 0 から、OFF で全ボイス共通 |
| `lfo.cutoff` | LFO to Cutoff | 連続 | -100〜100 % | 0 | 可 | ±100 % で ±4 オクターブ |
| `lfo.pulseWidth` | LFO to Pulse Width | 連続 | 0〜100 % | 0 | 可 | 100 % でデューティー比 ±45 %。波形が Pulse の OSC にだけ効く |
| `lfo.pitch` | LFO to Pitch | 連続 | 0〜1200 cent | 0 | 可 | スキュー中央 100 cent |
| `lfo.amp` | LFO to Amp | 連続 | 0〜100 % | 0 | 可 | トレモロ。100 % で谷が無音 |
| `macro.1` | Macro 1 | 連続 | 0〜100 % | 50 | 不可 | 50 % がニュートラル。マクロ自身は割当先にできない |
| `macro.2` | Macro 2 | 連続 | 0〜100 % | 50 | 不可 | |
| `macro.3` | Macro 3 | 連続 | 0〜100 % | 50 | 不可 | |
| `macro.4` | Macro 4 | 連続 | 0〜100 % | 50 | 不可 | |
| `fx.bypass` | FX Bypass | ON/OFF | — | OFF | 不可 | 全 FX の一括バイパス（RHYTHM は対象外） |
| `fx.drive.enabled` | Drive Enabled | ON/OFF | — | OFF | 不可 | |
| `fx.drive.type` | Drive Type | 選択 | Warm Saturation / Tape Saturation / Tube Saturation / Soft Clip / Hard Clip / Wavefold | Warm Saturation | 不可 | |
| `fx.drive.drive` | Drive Amount | 連続 | 0〜100 % | 30 | 可 | プリゲイン 0〜+30 dB |
| `fx.drive.tone` | Drive Tone | 連続 | 0〜100 % | 100 | 可 | ローパス 800 Hz〜20 kHz |
| `fx.drive.mix` | Drive Mix | 連続 | 0〜100 % | 100 | 可 | |
| `fx.drive.output` | Drive Output | 連続 | -24〜+12 dB | 0 | 可 | |
| `fx.drive.autoGain` | Drive Auto Gain | ON/OFF | — | ON | 不可 | |
| `fx.chorus.enabled` | Chorus Enabled | ON/OFF | — | OFF | 不可 | |
| `fx.chorus.type` | Chorus Type | 選択 | Classic / Wide | Classic | 不可 | |
| `fx.chorus.rate` | Chorus Rate | 連続 | 0.05〜8 Hz | 0.8 | 可 | スキュー中央 1 Hz |
| `fx.chorus.depth` | Chorus Depth | 連続 | 0〜100 % | 50 | 可 | |
| `fx.chorus.width` | Chorus Width | 連続 | 0〜100 % | 100 | 可 | |
| `fx.chorus.mix` | Chorus Mix | 連続 | 0〜100 % | 50 | 可 | |
| `fx.eq.enabled` | EQ Enabled | ON/OFF | — | OFF | 不可 | |
| `fx.eq.lowGain` | EQ Low Gain | 連続 | -18〜+18 dB | 0 | 可 | 100 Hz シェルフ |
| `fx.eq.midFreq` | EQ Mid Freq | 連続 | 200〜8000 Hz | 1000 | 可 | スキュー中央 1265 Hz |
| `fx.eq.midGain` | EQ Mid Gain | 連続 | -18〜+18 dB | 0 | 可 | Q = 1 |
| `fx.eq.highGain` | EQ High Gain | 連続 | -18〜+18 dB | 0 | 可 | 8 kHz シェルフ |
| `fx.imager.enabled` | Imager Enabled | ON/OFF | — | OFF | 不可 | |
| `fx.imager.width` | Imager Width | 連続 | 0〜200 % | 100 | 可 | |
| `fx.imager.monoBass` | Imager Mono Bass | ON/OFF | — | OFF | 不可 | 150 Hz 未満の Side を除く |
| `fx.delay.enabled` | Delay Enabled | ON/OFF | — | OFF | 不可 | |
| `fx.delay.mode` | Delay Mode | 選択 | Stereo / Ping Pong | Stereo | 不可 | |
| `fx.delay.sync` | Delay Sync | ON/OFF | — | ON | 不可 | |
| `fx.delay.division` | Delay Division | 選択 | 1/4 / 1/4 D / 1/4 T / 1/8 / 1/8 D / 1/8 T / 1/16 / 1/16 D / 1/16 T | 1/8 D | 不可 | Sync ON のとき有効 |
| `fx.delay.time` | Delay Time | 連続 | 1〜2000 ms | 350 | 可 | Sync OFF のとき有効。スキュー中央 400 ms |
| `fx.delay.feedback` | Delay Feedback | 連続 | 0〜95 % | 35 | 可 | |
| `fx.delay.tone` | Delay Tone | 連続 | 0〜100 % | 60 | 可 | |
| `fx.delay.mix` | Delay Mix | 連続 | 0〜100 % | 25 | 可 | 50 % まで dry を保つ（D-032） |
| `fx.delay.ducking` | Delay Ducking | 連続 | 0〜100 % | 0 | 可 | |
| `fx.reverb.enabled` | Reverb Enabled | ON/OFF | — | OFF | 不可 | |
| `fx.reverb.type` | Reverb Type | 選択 | Room / Plate / Hall | Plate | 不可 | |
| `fx.reverb.size` | Reverb Size | 連続 | 0〜100 % | 50 | 可 | |
| `fx.reverb.decay` | Reverb Decay | 連続 | 0.2〜15 s | 1.8 | 可 | スキュー中央 2 s |
| `fx.reverb.preDelay` | Reverb Pre-Delay | 連続 | 0〜200 ms | 10 | 可 | |
| `fx.reverb.tone` | Reverb Tone | 連続 | 0〜100 % | 60 | 可 | |
| `fx.reverb.mix` | Reverb Mix | 連続 | 0〜100 % | 20 | 可 | 50 % まで dry を保つ（D-032） |
| `fx.phaser.enabled` | Phaser Enabled | ON/OFF | — | OFF | 不可 | 信号の位置は Drive の後・Chorus の前（D-041） |
| `fx.phaser.type` | Phaser Type | 選択 | Phaser / Flanger | Phaser | 不可 | |
| `fx.phaser.rate` | Phaser Rate | 連続 | 0.02〜8 Hz | 0.4 | 可 | スキュー中央 0.8 Hz |
| `fx.phaser.depth` | Phaser Depth | 連続 | 0〜100 % | 60 | 可 | |
| `fx.phaser.feedback` | Phaser Feedback | 連続 | 0〜90 % | 40 | 可 | |
| `fx.phaser.mix` | Phaser Mix | 連続 | 0〜100 % | 50 | 可 | 100 % で原音とエフェクト音が 1:1 |
| `gate.enabled` | Gate Enabled | ON/OFF | — | OFF | 不可 | |
| `gate.steps` | Gate Steps | 選択 | 8 / 16 / 32 | 16 | 不可 | |
| `gate.rate` | Gate Rate | 選択 | 1/8 / 1/16 / 1/32 / 1/8T / 1/16T / 1/64 | 1/32 | 不可 | 1/64 は後から末尾に追加（D-041）。既定値は 1/16 → 1/32 に変更（D-044） |
| `gate.depth` | Gate Depth | 連続 | 0〜100 % | 100 | 可 | |
| `gate.smooth` | Gate Smooth | 連続 | 0.5〜30 ms | 3 | 可 | この時間でおおむね遷移し終える |
| `gate.swing` | Gate Swing | 連続 | 0〜50 % | 0 | 可 | |
| `gate.restart` | Gate Restart | 選択 | Transport / Note On | Transport | 不可 | |
| `gate.stereo` | Gate Stereo | ON/OFF | — | OFF | 不可 | ON で左右に別のパターンを使う（D-040） |
| `gate.width` | Gate Width | 連続 | 0〜100 % | 100 | 可 | Stereo ON のときの左右の分離。0 % で左右同じ |
| `arp.enabled` | Arp Enabled | ON/OFF | — | OFF | 不可 | |
| `arp.mode` | Arp Mode | 選択 | Up / Down / Up-Down / Random / As Played | Up | 不可 | |
| `arp.rate` | Arp Rate | 選択 | 1/4 / 1/8 / 1/16 / 1/32 / 1/8T / 1/16T | 1/16 | 不可 | |
| `arp.octaves` | Arp Octaves | 整数 | 1〜4 | 1 | 不可 | |
| `arp.gate` | Arp Gate Length | 連続 | 10〜100 % | 65 | 可 | ステップの実長に対する比率 |
| `arp.swing` | Arp Swing | 連続 | 0〜50 % | 0 | 可 | |
| `arp.velocity` | Arp Velocity | 連続 | 25〜200 % | 100 | 可 | 入力ベロシティに対する係数 |
| `arp.latch` | Arp Latch | ON/OFF | — | OFF | 不可 | |
| `arp.restart` | Arp Restart | 選択 | Transport / Note On | Note On | 不可 | |
| `output.gain` | Output Gain | 連続 | -60〜+12 dB | 0 | 不可 | 範囲は暫定（D-009）。全体の音量。**プリセットには保存せず、プリセットを読んでも変わらない**（D-055）。マクロの割当先にもできない（D-068）。ホストの状態には保存する |
| `preset.level` | Preset Level | 連続 | -24〜+24 dB | 0 | 可 | 音色ごとの音量の補正。プリセットに保存する（D-055） |
<!-- implemented:end -->

時間パラメーターは ms、割合は %（0〜100）の実値で保存する。DSP へ渡す直前に秒・0..1 へ変換する。

## 予定 ID（未実装。実装フェーズで確定し、上の表へ移す）

範囲・既定値は SPEC の該当節に従う。ここでは命名のみを予約する。

### Rhythm — Trance Gate / Arpeggiator（実装済み。上の表を参照）

- `gate.pattern` は**ホストパラメーターにしない**。パターン配列・パターン名・編集状態は META ツリーに保存する（下記）。
- 追加仕様の概念スキーマでは `arp.gate: 0.65` や `gate.depth: 1.0` のように 0..1 で書かれているが、本プロジェクトの規約（割合は % の実値で保存）に合わせ、`65` / `100` として保存する（D-023）。選択肢は他のパラメーターと同じくインデックスで保存する。
- `arp.velocity`（Arp のベロシティ係数）と `amp.velocity`（Velocity → Amp Level の量、Phase 2）は別のパラメーター。

### Output
`output.gain`、`preset.level`（実装済み）

## ホストパラメーターにしない状態（META ツリーに保存、Phase 3 以降）
マクロ名、マクロ割当（target / deltaMin / deltaMax / curve）、マクロの MIDI CC 割当、現在のプリセット情報、UI モード（Simple / Advanced）、Performance Lock、Gate パターン（最大 32 ステップの 0..1 配列、パターン名、ユーザー編集フラグ。Phase 4.5）。

保存しない実行時状態: 物理押鍵、発音中のノート、PPQ カウンター、Latch で保持中の和音。
