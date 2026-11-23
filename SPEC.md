# POLYBRISK — 製品・技術仕様書 (Draft v0.27)

- **状態**: 実装可能な初版仕様。未決定の項目には `[暫定]` または `[要決定]` を付ける。
- **製品名**: POLYBRISK（決定済み）
- **改訂**: v0.3 (2026-11-21) — Standalone 版、アイコン/ロゴ、製品紹介ページを追加（`docs/DECISIONS.md` D-012〜D-014）。
- **改訂**: v0.4 (2026-11-21) — フレームワークを JUCE 8 系から JUCE 9 系へ変更（`docs/DECISIONS.md` D-001）。
- **改訂**: v0.5 (2026-11-21) — ボイス処理の 2x オーバーサンプリングを追加（§4.3、`docs/DECISIONS.md` D-019）。
- **改訂**: v0.6 (2026-11-21) — Trance Gate / Arpeggiator（RHYTHM セクション）を追加。詳細仕様は `docs/ADDON_TRANCE_GATE_ARPEGGIATOR.md`、統合の要約は §20（`docs/DECISIONS.md` D-023）。
- **改訂**: v0.7 (2026-11-21) — Phase 2 の実装に合わせ、Transient の Amount の意味（§5）、フィルターの変調レンジ（§6.1）、Velocity → Amp の量と Sustain 0 の扱い（§7.1 / §7.3）を明記（`docs/DECISIONS.md` D-024〜D-027）。
- **改訂**: v0.8 (2026-11-21) — Phase 3〜7 の実装に合わせ、FX の Mix の法則（§8）、マクロの上限とカーブ（§10）、プリセットの現在数（§11.1）、実装状況（§17）、未決定事項（§18）を更新（`docs/DECISIONS.md` D-030〜D-038）。メーカー名を CHEEBOW に決定（D-005）。
- **改訂**: v0.9 (2026-11-21) — Standalone での確認を受けた追加と変更: UI の見直し（D-039）、Trance Gate のステレオ化とパターン 52 種（D-040）、**LFO・Mono / Legato とグライド・Sub / Noise・アフタータッチ・Phaser / Flanger・Gate の 1/64 とランダム**（D-041、§3 / §4.4 / §7.6 / §7.7 / §8.7）。パラメーターは 137 個。
- **改訂**: v0.10 (2026-11-21) — Factory プリセットの見直し: 似た音色の作り直し、新機能を使う音色の追加、カテゴリ **Trance Gate** の新設（D-042、§11.1）。収録は 81 音色。
- **改訂**: v0.11 (2026-11-21) — Trance Gate カテゴリの先頭に代表の音色 **Supersaw Gate** を追加し、Factory プリセットをカテゴリ内で収録順に並べる（D-043、§11.1）。収録は 82 音色。
- **改訂**: v0.12 (2026-11-21) — Trance Gate の既定を **Rate 1/32・パターン Shifted Long** に変更し、収録パターンを選ぶと Rate がそのパターンの前提（1/16・1/32・1/64）になるようにした（D-044、§20.1）。
- **改訂**: v0.13 (2026-11-21) — 音質の見直し: SuperSAW のデチューンを上下非対称に、**Unison Mix** を追加（パラメーターは 139 個）、Reverb の余韻の改善（D-045、§4.3 / §8.6）。プリセットブラウザーに**タグでの絞り込み**を追加（D-046）。
- **改訂**: v0.14 (2026-11-22) — 82 音色の試聴（一巡目）と修正（D-050〜D-052）。Arpeggiator を使う 6 音色を追加（D-053）。収録は 88 音色。
- **改訂**: v0.15 (2026-11-22) — カテゴリ **Bass** と **Pad / Strings** を新設し、目標数の配分を変更（D-054、§11.1）。Bass の 5 音色を追加。収録は 93 音色。**Output Gain をプリセットから切り離し、音色ごとの音量は新しい Preset Level に持たせる**（D-055、§12）。パラメーターは 140 個。ブラウザーに **Init**（空の音色）を追加（D-057、§11.1）。
- **改訂**: v0.16 (2026-11-22) — Pad / Strings の 5 音色を追加（D-058）。収録は 98 音色。
- **改訂**: v0.17 (2026-11-22) — Lead / Hook の 5 音色を追加（D-059）。収録は 103 音色。
- **改訂**: v0.18 (2026-11-22) — **Factory の目標を 256 → 128 音色に変更**し、カテゴリ配分を決め直した（D-060、§11.1）。似た音色で数を埋めるより、すぐ使える音色に絞る。
- **改訂**: v0.19 (2026-11-22) — Synth Brass の 6 音色を追加（D-061）。収録は 109 音色。
- **改訂**: v0.20 (2026-11-22) — SuperSAW の 7 音色を追加（D-062）。収録は 116 音色。
- **改訂**: v0.21 (2026-11-23) — Bright Poly の 5 音色を追加（D-063）。収録は 121 音色。
- **改訂**: v0.22 (2026-11-23) — 残りの 7 音色を追加し、**Factory が目標の 128 音色に届いた**（D-064）。
- **改訂**: v0.23 (2026-11-23) — Simple に **TONE**（Cutoff / Resonance / Attack / Release）を追加（D-065、§9.1）。
- **改訂**: v0.24 (2026-11-23) — 近い音色の聴き比べと Idol Hyper Saw の作り直し（D-066）。Standalone の画面内鍵盤の左に **ピッチベンドとモジュレーションのホイール**を追加（D-067、§13）。
- **改訂**: v0.25 (2026-11-23) — コード全体の調査（バグ・安全性・整理）と修正（D-068）。仕様に関わる変更: **Output Gain はマクロの割当先にできない**、ヘッダーの Save は読み込んだファイルへ上書きする、Delete は 2 回押し、プリセットの ID は一覧の中で一意。**Drive の Mix は、原音と歪ませた音の位相をそろえて混ぜる**（D-069、§8.1。Drive を Mix 100 % 未満で使う 25 音色の高域が変わった）。
- **改訂**: v0.26 (2026-11-23) — ヘッダーに**出力メーター**を追加（D-071、§13）。画面の文言の英語 / 日本語の切り替えは試作して取りやめ、画面は英語のみ（D-070）。
- **改訂**: v0.27 (2026-11-23) — ソースコードを GitHub で公開し、製品ページを GitHub Pages で公開した（D-075、§18）。**ホストのキー操作を横取りしない**: キーを受け取るのは文字の入力欄だけ（D-076、§13）。
- **目的**: Claude Code で段階的に実装できる macOS 用ポリフォニック・シンセサイザーを作る。
- **想定用途**: J-POP、アイドルポップス、シンセポップ、ダンスポップで使う、明るく輪郭の強い SuperSAW / Synth Brass / Stab / Bright Poly / Pluck。
- **設計哲学**: 選べば使える実戦的なプリセット。普段は Simple、詳細は Advanced。同一DSP・同一状態を二つの表示で操作する。複雑なモジュレーションマトリクスは不要。
- **追加仕様**: `docs/ADDON_TRANCE_GATE_ARPEGGIATOR.md`（Trance Gate / Arpeggiator）。本書と衝突する箇所は追加仕様を優先する。

## 0. 仕様の優先順位と非目標

### 必須
1. AU / VST3 の Instrument として Cubase などの DAW で安定動作。DAW なしで演奏できる Standalone アプリも提供する。
2. 2 基の独立オシレーター（OSC A/B）を自由に組み合わせ可能。
3. SuperSAW / Saw / Pulse / Square / Triangle / Sine / Noise とユニゾン、ピッチ設定。
4. Transient レイヤー、3キャラクターのフィルター、Amp/Filter/Pitch エンベロープ。
5. ピッチベンド（上/下を独立設定可能）と CC1→ビブラートを簡単に設定。行き先を固定した LFO 1 基、Mono / Legato とグライド、アフタータッチ（v0.9）。
6. Drive（Saturation/Distortion）、Phaser / Flanger（v0.9）、Chorus、3バンドEQ、Stereo Imager、Delay、Reverb。
7. Simple/Advanced、4マクロのプリセット別名称・任意複数割り当て。
8. プリセット保存、ブラウズ、タグ検索、ユーザー音色、ホストプロジェクトへの状態保存。
9. オーディオスレッドをブロックしないリアルタイム安全な実装。
10. **RHYTHM セクション**: 音量パターン型の Trance Gate と、標準的な Arpeggiator（Up / Down / Up-Down / Random / As Played）。単独でも同時でも使え、DAW の BPM・演奏位置に同期する（§20）。

### 意図的に実装しない（初期リリース）
- 汎用モジュレーションマトリクス、ウェーブテーブル編集、サンプル読み込み、グラニュラー。
- 汎用ステップ・シーケンサー（ステップごとの任意音程入力、コード進行生成、長大なフレーズ）、Arpeggiator の外部 MIDI 出力、MIDI ファイル書き出し、複数レーン・ポリリズム・確率シーケンス・Euclidean generator、サイドチェイン入力で動くゲート。※ v0.2 で対象外としていた「アルペジエーター」は v0.6 で実装対象に変更した（標準的な Arpeggiator と Trance Gate のみ）。
- MPE、ノートごとに独立した Poly Aftertouch（受信はするが、全体に効く 1 つの値として扱う）、Windows版、AAX、CLAP、AUv3。
- 2 基目以降の LFO、LFO の行き先の自由な追加、2 基目のフィルター、3 基目の OSC（「選べば使える」方針を保つため。D-041）。
- ドラッグによるFX順序入れ替え、フル機能のマスタリングEQ/コンプレッサー。
- パッド音色専用の開発やアンビエント特化機能。
- 本物のアナログ回路の厳密なモデリングを名乗ること。

## 1. 対象環境と開発スタック

| 項目 | 仕様 |
|---|---|
| 言語 | C++20 `[暫定]` |
| フレームワーク | JUCE 9 系（安定タグに固定） |
| ビルド | CMake 3.22+ / Xcode / macOS SDK |
| プラグイン | AU (`.component`)、VST3 (`.vst3`) |
| Standalone | macOS アプリ (`POLYBRISK.app`)。オーディオ/MIDI デバイス設定、状態の永続化、画面内鍵盤（Phase 5） |
| 用途 | MIDI入力・ステレオ音声出力の Instrument。音声入力不要 |
| 対象macOS | macOS 13以降 `[暫定]` |
| CPU | Apple Silicon を第一ターゲット。Universal binary はビルド環境で検証して判断 `[暫定]` |
| DAW検証 | Cubase を優先、可能なら Logic Pro、JUCE AudioPluginHost |
| サンプルレート | 44.1 / 48 / 88.2 / 96 kHz（少なくとも） |
| バッファ | 32〜2048 samplesを対象、可変ブロック長にも対応 |
| テスト | JUCE UnitTest + CTest、オフラインWAVレンダリング比較テスト |

- `juce_add_plugin()` の Synth / MIDI input / AU・VST3・Standalone 設定を使用。Standalone はプラグインと同一の DSP・状態・UI を共有する。
- JUCE のライセンスと、配布方法・商用利用の条件は実装前に公式条件で確認すること。
- ビルド可能な最小プラグインを最初に作る。製品名は POLYBRISK に決定済み。Bundle ID / 4文字コードは開発中は仮値を使用してよいが、配布前に名称衝突・商標等を確認したうえで必ず確定する。

## 2. 音声処理の全体像

```text
MIDI (Note / Bend / CC1 / CC64)
       |
 MIDI Input State (held keys, sustain)
       |
 [ARPEGGIATOR]  ON: generated note events / OFF: original note events
       |
 Voice Manager (16-note polyphony)
       |
       +-- OSC A (waveform + unison + pitch + pan) --+
       +-- OSC B (waveform + unison + pitch + pan) --+-- Voice Mix
       +-- Sub Osc / Noise (v0.9) --------------------+
       +-- Transient (noise/pitch attack) ------------+
                                                      |
             Filter [Analog / Clean / Punch]
                   -> Amp Envelope
                   -> Voice Summing
                   -> FX Drive -> Phaser -> Chorus -> 3-band EQ -> Stereo Imager
                                                    |
                                             [TRANCE GATE]
                                                    |
                        +---------------------------+---------------------+
                        |                                                 |
                   Dry Path                                        Delay / Reverb
                        |                                       independent wet paths
                        +---------------------------+---------------------+
                                                    |
                                     Preset Level × Output Gain
                                                    |
                                                 Stereo Out
```

- フィルター・エンベロープは原則**各ボイス**。FXはボイスミックス後の**グローバル**処理。
- ReverbとDelayは同じ前段を入力する独立 wet パスとして並列処理。互いに接続しない（初期仕様）。
- 各FXに個別バイパス、全FX一括バイパスを設ける。バイパスの切替は短いクロスフェードでクリックを抑制。
- フィルターのDriveと独立したFX Driveは別物。フィルターのDriveはフィルター特性、FX Driveは全体の倍音作りに使う。
- **Arpeggiator** は音源の前段で内部ノートイベントを生成する。Pitch Bend / CC1 などの連続制御はそのまま音源へ伝える。
- **Trance Gate** は全ボイス合計に作用するグローバルな音量ゲート（Stereo を ON にすると左右で別のパターン。D-040）で、Imager の後・Delay / Reverb の入力より前に置く（刻んだ音の残響・反復が自然に残る）。
- Arp / Gate は 7 種類の FX とは別の **RHYTHM** セクションとし、FX 一括バイパスの対象外。

## 3. Polyphony / Voice Management

- 最大同時発音: **16ノート**（固定、将来可変化可）。
- 音域: MIDI Note 0〜127。Note On（Velocity 0 は Note Off）、Note Off を処理。
- 同音重複Note Onを扱えるよう各発音を独立ボイスとして追跡。
- Note OffはAmp Releaseへ。Sustain Pedal (CC64) に対応。
- ボイスが足りない場合: Release中で音量の小さいものを優先、次いで最古ボイスをスティール。クリック防止に短いデクリック。
- **発音モード（v0.9、D-041）**: `voice.mode` = Poly / Mono / Legato。
  - **Mono**: 鳴らすのは 1 声。新しい鍵を弾くと同じボイスの音程が移り、エンベロープ・Transient・LFO の Fade をいまのレベルから再始動する。
  - **Legato**: 鍵を押さえたまま次を弾くと、エンベロープを再始動せずに音程だけが移る。離してから弾けば通常どおり発音する。
  - どちらも後着優先。弾いている鍵を離すと、まだ押さえている鍵のうち最後に押したものへ戻る。サステインペダルで保持中のボイスも、新しい鍵へ移る。
  - Release 中の余韻は切らない（新しいノートは別のボイスで始まり、余韻と重なる）`[暫定]`。
  - Arpeggiator が ON のあいだ、生成ノートは通常のボイス割り当てで鳴らす（Arp は元から 1 音ずつ鳴らすため）。
- **グライド（v0.9）**: `voice.glide` 0〜2000 ms（0 でなし）。指定時間で、半音単位で直線的に移る。Poly と Mono では直前に弾いたノートから、Legato では鍵を押さえたまま次を弾いたときだけ滑らせる。
- MIDIイベントはブロック内のサンプルオフセットに従って処理。

## 4. OSC A / OSC B

両OSCは独立、同じ波形を選んでも別波形でも可。各OSCのON/OFFあり。

### 4.1 波形

| Wave | 説明 |
|---|---|
| Saw | バンド制限した鋸歯状波 |
| SuperSAW | Saw群の重ね合わせ。既定7本、ユーザー指定可能 |
| Pulse | デューティー比可変（既定50%） |
| Square | 50% Pulseの操作しやすいプリセット波形 |
| Triangle | 三角波 |
| Sine | 正弦波 |
| Noise | ホワイトノイズ（各ボイス独立シード） |

### 4.2 各OSCのパラメーター

| Parameter | 範囲・初期値 |
|---|---|
| Enabled | ON / OFF、OSC AはONが初期 |
| Wave | Saw / SuperSAW / Pulse / Square / Triangle / Sine / Noise |
| Octave | -2〜+2、初期0 |
| Semitone | -12〜+12、初期0 |
| Fine | -100〜+100 cents、初期0 |
| Level | 0〜100%、A=100、B=70 `[暫定]` |
| Unison Voices | 1〜9、Saw等は既定1、SuperSAWは既定7 |
| Detune | 0〜100%、初期 SuperSAW 35% `[暫定]` |
| Stereo Spread | 0〜100%、初期50% `[暫定]` |
| Phase Mode | Free / Retrigger、初期 Retrigger `[暫定]` |
| Pulse Width | 5〜95%、Pulse選択時のみ表示 |

**重要**: SuperSAW の7本は同一OSCの `Unison Voices = 7` を意味し、さらに別の内部7倍ユニゾンを掛けない。16ノート×2OSC×最大9本=最大288の発振成分。CPU負荷を計測する。

### 4.3 DSP要件

- Saw/Pulse等はPolyBLEPまたは同等のバンド制限方式を採用し、単純な不連続波形をそのまま出さない。
- ホストのサンプルレートが 44.1 / 48 kHz のときは、ボイス処理を 2 倍のレートで行ってから元のレートへ戻し、可聴帯域の折り返し成分を抑える（88.2 kHz 以上は等倍）。
- ユニゾンのDetuneは左右対称の分布で、中心のピッチ感を保つ。均等なパン配置と適切なゲイン正規化を行う。
- **SuperSAW だけは、対ごとに上下のデチューン量を数 % ずらす**（上下が同じ量だと、中央の声に対するうなりの速さがそろって揺れが周期的に聞こえるため）。中央の声は動かさない。Saw などのユニゾンは対称のまま（D-045）。
- **Unison Mix**（`osc.a.unisonMix` / `osc.b.unisonMix`、0〜100 %、既定 50 %）: 中央の声と外側の声の音量比。50 % で全声が同じ音量、0 % で中央だけ、100 % で外側だけ。どの値でも合計のパワーは一定。偶数本では最も内側の対を中央として扱い、1〜2 本では効かない（D-045）。
- OSCのステレオ広がりを0でもセンターの音は消えない。
- フェーズのFree / Retriggerを切り替え、Stabのアタックを再現性高くできる。
- 音程、チューニング、Detune、Spreadなど連続パラメーターは必要に応じスムージング。
- Noiseは通常の音階追従を必要としない。チューニング設定は無視または非表示。

### 4.4 Sub Oscillator / Noise（v0.9、D-041）
- **Sub**: ユニゾンなしの 1 声をセンターに足す。Level 0〜100 %（既定 0 = 鳴らない）、Octave -1 / -2、Wave Sine / Square。ノートのピッチ（ベンド・ビブラート・グライドを含む）に追従し、OSC A / B の Tune の影響は受けない。
- **Noise**: 持続するノイズ層。Level 0〜100 %（既定 0）、Color（Dark ↔ Bright。Transient の Noise Color と同じ規則）。Transient の Noise Burst（アタックだけ）とは別。
- どちらも OSC A / B と同じくフィルターの前で混ぜ、Amp Envelope に従う。OSC A / B を両方とも音色づくりに使ったまま、低音や息の成分を足すためのもの。

## 5. Transient レイヤー

短くパキッとした音を作るアタック専用レイヤー。OSC A/Bから独立してON/OFF可能。

| Parameter | 仕様 `[暫定]` |
|---|---|
| Mode | Noise Burst / Pitch Snap / Both |
| Amount | 0〜100% |
| Duration | 1〜150ms |
| Noise Color | Dark〜Bright |
| Pitch Snap | -24〜+24 semitonesから0へ収束（発振器のピッチを一時変調） |

- Noise Burstは独立の短いエンベロープで付加。
- Pitch SnapはOSC A/B共通の相対的なピッチ変化（個々のTune設定を保持）。
- 不用意にポップノイズが出ないフェード処理を行う。
- Amount は Noise Burst の音量と Pitch Snap の深さの両方に掛かる（Amount 0 で Transient なし）。Duration は −60 dB（Pitch Snap は 1/1000）まで減衰する時間 `[暫定]`。
- Noise Burst はボイスミックスに加わり、フィルターと Amp Envelope を通る。

## 6. Filter

### 6.1 3キャラクター

| Model | 特徴・実装方針 |
|---|---|
| Analog | 温かく太い、軽度な非線形性。アナログ風の飽和を組み込む |
| Clean | 透明で癖の少ない、適切な補間を持つ安定したフィルター |
| Punch | 強いエンベロープ変化でも輪郭が明確。過度な音量増幅は抑える |

- 初期実装は同じ安定したTPT/SVF系コアを活用し、モード別にゲイン補正・飽和・応答を設計してよい。3つの名前だけで中身が同一にならないよう、AB比較で差を検証する。
- Filter Mode: LP12、LP24、HP12、HP24、BP12。
- Cutoff 20Hz〜20kHz（Nyquistの安全域にクランプ、対数スケール）、Resonance 0〜100%。
- Env Amount: -100〜+100%（バイポーラ）、Filter ADSR、Key Tracking 0〜100%、Velocity→Cutoff 0〜100%。
- Filter Drive 0〜100%。高レゾナンス設定時も発散しないこと。
- Cutoff/Resonanceの大きな変更でクリックしないようスムージング。
- 変調レンジ `[暫定]`: Env Amount ±100% = ±8 オクターブ。Key Tracking 100% = ノート 60 を基準に音程へ 1:1。Velocity→Cutoff 100% = ベロシティ最大で変化なし、ベロシティ 0 で −5 オクターブ（弱く弾くほど暗くなる）。
- Filter Drive は 0〜+24 dB の入力ゲインで、音量は自動補正する。

## 7. Envelope / Modulation

### 7.1 Amp ADSR / Filter ADSR
- A: 0.1ms〜10s、D: 1ms〜10s、S: 0〜100%、R: 1ms〜20s `[暫定]`。
- Attack等の時間パラメーターは対数的な操作感を持たせる。
- AmpはNote Off後にReleaseし、リバーブ・ディレイの尾はそのまま続く。
- Amp の Sustain が 0% のときは、鍵盤を押さえたままでも減衰しきった時点（−100 dB）でボイスを解放する。

### 7.2 Pitch Envelope
- TransientのPitch Snapに連動する簡易Decay型。汎用LFOや複雑なエンベロープマトリクスにはしない。

### 7.3 固定ルーティング
- Velocity → Amp Level（量は `amp.velocity` 0〜100%、初期 100% `[暫定]`）、Filter Cutoff（量は `filter.velocity`）。
- Filter Envelope → Filter Cutoff。
- Amp Envelope → Amp Level。
- Pitch Envelope → アタック時のPitch。
- Pitch Bend → OSC A/Bの共通Pitch。
- Mod Wheel (CC1) → Vibrato Depth。
- LFO → Filter Cutoff / Pulse Width / Pitch / Amp（それぞれ深さを持つ。§7.6）。
- Aftertouch → Vibrato Depth / Filter Cutoff（§7.7）。
- マクロ4個 → 任意の**連続値パラメーター**（複数割り当て可）。

### 7.4 Pitch Bend
- Up / Down独立: 0〜48 semitones（半音単位）、初期±2。
- Symmetric（上下連動） / Independent（独立）を選べる。
- MIDI pitch bendの中心値は無変化、上端・下端で各指定幅に到達。
- Pitch Bendを変更してもOSCのOctave/Semitone/Fineの関係は保持。
- ピッチは短いスムージング（例3〜10ms、実装時聴感で最適化）でジッパーノイズを防止。
- 初期は通常のチャンネル単位のMIDI Pitch Bendのみ、MPE非対応。

### 7.5 Mod Wheel Vibrato
- CC1 = 0でVibratoなし、127で設定最大Depthに到達。
- Depth 0〜200 cents、初期 `[暫定]` 35 cents。
- Rate 0.1〜12Hz、初期5.5Hz。
- Delay 0〜2000ms、初期0ms。ノートオンからの経過時間でVibratoの有効度をなだらかに立ち上げる。
- CC1の動きはスムージング。ビブラートLFOは各ボイスが参照する共通位相 `[暫定]`、Delayゲートは各ボイス別。
- ピッチベンドとビブラートはcentsの加算で独立に合成する。
- CC1以外への自由なMIDI割り当ては初期必須ではない（マクロのMIDI Learnは別途対応）。

### 7.6 LFO（v0.9、D-041）
- **1 基のみ**。行き先は固定の 4 つで、それぞれに深さ（Depth）を持つ。既定はすべて 0（効かない）。
  - Cutoff: -100〜+100 %（±100 % で ±4 オクターブ）
  - Pulse Width: 0〜100 %（100 % でデューティー比 ±45 %。波形が Pulse の OSC にだけ効く）
  - Pitch: 0〜1200 cent
  - Amp: 0〜100 %（トレモロ。100 % で谷が無音）
- 波形: Sine / Triangle / Saw / Square / S&H。
- Rate: 0.05〜20 Hz、または **Sync** でテンポ同期（4 Bars 〜 1/16、3 連・付点）。Sync でホストが再生中のときは位相を音楽位置に固定する。
- **Fade In** 0〜5000 ms: ノートオンからなだらかに立ち上げる（ボイスごと）。
- **Key Sync**: ON でノートごとに位相 0 から始める。OFF は全ボイス共通の位相。
- Mod Wheel のビブラート（§7.5）とは別の LFO。
- Rate と各深さはマクロの割当先にできる。行き先の追加や 2 基目は実装しない。

### 7.7 Aftertouch（v0.9、D-041）
- チャンネルプレッシャーを受け、**Vibrato Depth**（0〜200 cent を加算）と **Filter Cutoff**（-100〜+100 %、±4 オクターブ）に効かせる。既定はどちらも 0。
- ポリフォニック・アフタータッチのメッセージも受けるが、ノートごとには扱わず、全体に効く 1 つの値とする。

## 8. FX セクション（7種類）

各FX: Enabled/Bypass、Dry/Wet設定（EQ/Imagerは除く）、プリセット保存、ホストオートメーションをサポートする。全FX一括バイパスも提供。

- Delay / Reverb の Mix は、並列の wet パスであることに合わせ、**50% までは dry を保ったまま wet を上げ、50% を超えると dry を下げる**（100% で wet のみ）`[暫定]`。
- 既定ではすべての FX が OFF。各パラメーターの範囲と既定値は `docs/PARAMETER_IDS.md`。

### 8.1 DRIVE — Saturation + Distortion
- Type: Warm Saturation / Tape Saturation / Tube Saturation / Soft Clip / Hard Clip / Wavefold。
- Drive 0〜100%、Tone 0〜100%、Mix 0〜100%、Output -24〜+12dB、Auto Gain ON/OFF。
- 初期実装では音楽的に識別できる異なる非線形カーブを設計。Tape/Tubeという名称を使っても厳密な機材エミュレーションを名乗らない。
- 非線形処理は2x〜4xの内部オーバーサンプリング `[暫定]` を設け、エイリアシングとCPU負荷を比較検証。レイテンシーが生じる場合はホストへ申告。

### 8.2 CHORUS
- Type: Classic / Wide。
- Rate 0.05〜8Hz、Depth 0〜100%、Width 0〜100%、Mix 0〜100%。
- モジュレーテッド・ショートディレイをベースにしたステレオ処理。極端な設定時も急激なピッチ飛びを防ぐ補間。

### 8.3 EQ
- Low Shelf: Frequency固定100Hz `[暫定]`、Gain -18〜+18dB。
- Mid Bell: Frequency 200Hz〜8kHz、Gain -18〜+18dB、Qは初期1.0 `[暫定]`（AdvancedでQ可変は将来候補）。
- High Shelf: Frequency固定8kHz `[暫定]`、Gain -18〜+18dB。
- AdvancedではLow/High棚の周波数可変対応も検討可能だが、最初のMVPでは固定でよい。

### 8.4 STEREO IMAGER
- Width 0〜200%、100%=元の幅、0%=Mono。
- M/S処理。Sideの増幅に応じて安全な出力補正を行う。
- Mono Bass ON/OFF、Crossover既定150Hz `[暫定]`。クロスオーバー部は位相整合に注意。
- ImagerはOSC Spreadとは別に最終ステレオ幅を変える。
- Mono合成で芯が過度に消えないことをテスト。

### 8.5 DELAY
- Stereo / Ping Pong。
- Host Tempo Sync ON/OFF、同期音価: 1/4, 1/8, 1/16 と付点・3連符。非同期Time 1〜2000ms。
- Feedback 0〜95%、Tone、Mix 0〜100%、Ducking 0〜100%。
- ホストテンポが得られない場合は120 BPMにフォールバック。テンポ変化時の遷移でクリックを抑える。
- Feedbackループを発散させないこと。Note Off後に尾を保持。

### 8.6 REVERB
- Type: Room / Plate / Hall。
- Size、Decay（約0.2〜15s）、Pre-delay（0〜200ms）、Tone、Mix。
- 実装は効率的なReverb networkまたはJUCE実装をベースに拡張してよい。3モードで聴覚的に明確なキャラクター差があること。
- プリセット切替時の残響の扱いは原則クリアまたは短いクロスフェード `[暫定]`。設定変更でノイズを出さない。

- 実装上の要件（D-045）: FDN のディレイ長はほぼ等比に並べる（等間隔にしない）。どのタイプでも半数のラインをわずかに変調する。ループ内にローカットを置き、低域ほど速く減衰させる。

### 8.7 PHASER / FLANGER（v0.9、D-041）
- 信号の位置は Drive の後・Chorus の前。Type で Phaser（オールパス 6 段）と Flanger（0.3〜7 ms の変調ディレイ）を切り替える。
- Rate 0.02〜8 Hz、Depth、Feedback 0〜90 %、Mix（100 % で原音とエフェクト音が 1:1）。左右の LFO は 90° ずらす。

## 9. Simple UI / Advanced UI

**常に同じDSP・パラメーター状態を共有**。モード切替そのものでは音は変化しない。

### 9.1 Simple
- プリセット名 / カテゴリー / お気に入り / 検索ブラウザー。
- 大きい4つの Macro ノブ。ラベルは**プリセットごとに変わる**。
- **TONE**: Cutoff / Resonance / Attack（Amp）/ Release（Amp）の 4 つのノブ。音色を選んだあとによく直す設定だけを Simple に出す。Advanced の同じパラメーターを操作する（新しいパラメーターではない）（D-065）。
- FXのON/OFFが見える簡潔なサマリー、FX BYPASS。
- **RHYTHM** セクション: `GATE` スイッチ・パターン選択・Depth、`ARP` スイッチ・Mode・Rate。コントロールは常に同じ位置に置き、OFF のあいだは薄く表示して操作できなくする（D-039。当初は「ON のときだけ表示」としていた）。
- 出力音量、Simple/Advanced切替。
- 最小限のPerformance表示。詳細なピッチベンド/ビブラートはAdvancedで編集。

### 9.2 Advanced
- OSC、Transient、Filter、Envelopes、Performance、FX、Rhythm、Macrosをセクション/タブで編集。
- Rhythm タブ: Gate（8/16/32 ステップの編集グリッド、ステップ音量、Rate、Depth、Smooth、Swing、Restart、パターン保存/ロード）と Arpeggiator（Mode、Rate、Octaves、Gate Length、Swing、Velocity、Latch、Restart）。現在ステップの表示は音声スレッドで描画更新しない。
- OSC Wave選択に応じ、Pulse Widthなどの無関係パラメーターを隠す。
- 数値はドラッグ/入力でも編集可能、ダブルクリックで初期値へ。
- Macrosのルーティング編集とノブ名変更を含む。
- Simpleへ戻っても詳細設定を保持。
- 最初はJUCEネイティブなカスタムComponentで実装。WebViewベースのUIは必須ではない。

## 10. Macro（4つ、プリセットごとに設定）

### 10.1 基本
- 4個固定。ホスト用パラメーターIDは `macro.1` / `macro.2` / `macro.3` / `macro.4` として不変。
- UI表示名・現在値・割当先群・各範囲・カーブはプリセットごとに保存。
- 1マクロ→複数パラメーター、複数マクロ→同一パラメーターの両方を許可。
- 割当先は連続値パラメーターのみ。WaveやFX Type、Enabledなど離散値は初期対象外。
- RHYTHM では `gate.depth`、`gate.smooth`、`gate.swing`、`arp.gate`、`arp.swing`、`arp.velocity` が割当可能。`gate.enabled`、`gate.steps`、`gate.rate`、`gate.restart`、`arp.enabled`、`arp.mode`、`arp.rate`、`arp.octaves`、`arp.latch`、`arp.restart`、Gate パターン全体は対象外。
- アサイン数は各Macro最大8 `[暫定]`。
- MIDI Learnで任意のCCを4マクロに割り当て可能（CC1はVibratoに予約、CC64はSustainに予約）。設定可能なのはCC2〜119のうち予約済みを除いたもの。CC割当はプリセットに保存するかGlobalに保存するか初期版はプリセット単位とする。
- ホストのオートメーションからMacro値を制御できること。

### 10.2 Advancedの直接編集と衝突しない値のモデル（必須）

マクロがすでに割り当てられたパラメーターをAdvancedで編集しても、ベース値が編集でき、急に値が飛ばないようにする。

- 各パラメーターは、ホストに公開する **Base Value** `b`（正規化0..1）を保持する。
- Macroの各割り当ては、正規化パラメーターへの相対変化量 `deltaMin`（マクロ0時）、`deltaMax`（マクロ1時）を持つ。マクロ0.5では変化量0（ニュートラル）。
- 1つの割り当ての変化は次のpiecewise補間で定義する（必要ならべき乗curveを0..0.5 / 0.5..1に適用）。

```text
if macro < 0.5:
    delta = lerp(deltaMin, 0, shape(macro * 2))
else:
    delta = lerp(0, deltaMax, shape((macro - 0.5) * 2))

effectiveNormalized = clamp(baseNormalized + sum(allAssignedDeltas), 0, 1)
```

- UIには「Minimum / Maximum」という絶対値ではなく **Low / High offset（±%）** と表示し、意味を明示する。
- パラメーターが周波数・時間など非線形レンジの場合、正規化空間で合成した後、実値へ変換する。
- 複数マクロの寄与は加算し、最終的にクランプ。ベースパラメーターのホストオートメーションと共存する。
- **0.5が常にニュートラル**であることを維持し、プリセット保存時に全マクロの現在値も保持する。
- 必要なスムージングはeffective側で行う。UIの表示値はbaseとeffectiveを区別して表示可能。
- Macro Assignの変更・プリセット読み込みはメッセージスレッドでimmutable設定スナップショットを構築し、音声スレッドへ安全に交換する（オーディオスレッドで文字列/JSON/ValueTreeを編集しない）。

### 10.3 カーブ
- Linear / Ease In / Ease Out を選択可能（3 種とも実装済み）。
- `Punch`等は単なる固定内部処理ではなく、ファクトリープリセット側で割当・名前を設定する。

## 11. プリセット・ブラウザー

### 11.1 目標数: Factory 128

| Category | 目標 |
|---|---:|
| SuperSAW | 18 |
| Synth Brass | 18 |
| Stab / Chord Hit | 18 |
| Bright Poly | 12 |
| Synth Pluck | 10 |
| Lead / Hook | 12 |
| Bass（v0.15 で追加） | 8 |
| Pad / Strings（v0.15 で追加） | 10 |
| Octave / Layered | 4 |
| Trance Gate（v0.10 で追加） | 18 |
| **合計** | **128** |

**注意**: 128 は最終収録目標。当初の目標は 256 だったが、v0.18 で 128 に変えた（D-060）。似た音色を増やして数を満たすより、用途がはっきりしていてすぐ使える音色に絞るため。自動生成の微差パッチで数を埋めて完了としない。

**現在数（v0.22 時点）**: 128 音色で、全カテゴリが目標に届いた（SuperSAW 18 / Synth Brass 18 / Stab 18 / Bright Poly 12 / Synth Pluck 10 / Lead 12 / Bass 8 / Pad 10 / Octave 4 / Trance Gate 18）。設計と音量の実測調整に加え、開発者が Standalone で 128 音色すべてを試聴済み（2026-11-23、D-052〜D-054、D-058、D-059、D-061〜D-064）。DAW 上での試聴と、DSP の細かな調整は未実施。

- **Trance Gate カテゴリ**: 和音を押さえたままにして、Trance Gate にリズムを刻ませる音色を集める。すべて Gate が ON。目標は現在数と同じ 18（D-060）。
- **Bass カテゴリ**: 単音で弾くベース。**Pad / Strings カテゴリ**: 立ち上がりの遅い、和音を長く伸ばす音色。製品の中心（明るく歯切れのよい音）から外れるので、目標は少なめにした（D-054、目標数は D-060）。
- **Init（空の音色）**: ブラウザーの Init ボタンで、すべての設定を既定値に戻し、マクロの割当を消し、Gate を既定のパターンにする。音作りの出発点。Factory の一覧には載せず、128 の数にも入れない（設計した音色ではないため）。Output Gain は変えない（D-057）。
- **並び順**: ブラウザーはカテゴリの順（上の表の順）に並べ、カテゴリ内では Factory を収録順（`presets/factory` のファイル番号順）、User を名前順に並べる。カテゴリを代表する音色を先頭に置くため（D-043）。Trance Gate の先頭は Supersaw Gate（SuperSAW + パターン Shifted Long、Rate 1/32）。
- 似た音色の洗い出しには `tools/preset_similarity.py`（帯域 × 時間のレベル分布と広がりの差）を使う。候補を挙げるだけで、試聴の代わりにはならない。

- 第一段階は実用性を確認する **24音色の手動検証済みスターターセット**。
- 次に64音色のベータライブラリ、最後に 128 音色（v0.18 で 256 から変更）。制作・試聴・音量調整は人が行う。
- 各パッチは想定用途が明確で、似た音色の水増しをしない。
- Mix Ready（控えめなFX）と Big / Produced（強めのFX）の双方を用意する。

### 11.2 メタデータ
- ID（UUID）、Name、Category、Tags、Author、Description、Version、Factory/User、Favorite（Favoriteはユーザー設定側管理）。
- 検索（名前、説明、タグ）、Categoryフィルター、Characterタグ（Bright / Punchy / Tight / Wide / Warm / Dry等）。
- 音色に対する4マクロの名前、割当、値も保存。
- Factoryプリセットは組込みリソースで読み取り専用。Userプリセットは Application Support 配下。
- プリセット保存、複製、リネーム、削除（Userのみ）、Import/Export。
- プリセット読み込み時にパラメーターのスムージングを考慮し、クリックや突発的音量増大を避ける。

### 11.3 ファイル形式
- JSON `schemaVersion: 1` + Plugin state を保存。
- Factory/User共通のシリアライズ形式にし、外部ファイル読み込み検証・破損耐性を持たせる。
- ホストが状態保存・復元するときはプリセット名だけでなく**全パラメーター、マクロ割当、マクロ名、状態を完全に保存**。外部プリセットファイルがなくてもDAWプロジェクトで音色が再現できること。
- 旧バージョンの未知フィールドは無視、欠落パラメーターには既定値を補完、schemaVersionを使ってマイグレーションする。

### 11.4 例（概念スキーマ）

```json
{
  "schemaVersion": 1,
  "id": "example-bright-stab",
  "name": "Bright Pop Stab",
  "category": "Stab / Chord Hit",
  "tags": ["Bright", "Tight", "Mix Ready"],
  "parameters": {
    "osc.a.wave": "Saw",
    "osc.b.wave": "Pulse",
    "filter.character": "Punch",
    "filter.mode": "LP24",
    "performance.bendUp": 2,
    "performance.bendDown": 2
  },
  "macros": [
    {
      "index": 1,
      "name": "Punch",
      "value": 0.5,
      "mappings": [
        {"target": "filter.envAmount", "deltaMin": -0.25, "deltaMax": 0.30, "curve": "linear"},
        {"target": "transient.amount", "deltaMin": -0.10, "deltaMax": 0.25, "curve": "linear"}
      ]
    }
  ]
}
```

※ 上記は説明用。実際は全設定値・4マクロを保存し、IDの体系をコードと定義ファイルで統一する。

## 12. 設定の保存とホスト連携

- JUCE `AudioProcessorValueTreeState`（APVTS）を採用。パラメーターIDは公開後に破壊的変更しない。
- Host automation可能な値（OSC、Filter、EG、FX、Macro 1〜4、Pitch Bend Range、Vibrato設定、`gate.*`、`arp.*`）は安定IDを用意。
- Gate のパターン配列（最大 32 ステップ、0..1）とパターン名・ユーザー編集状態は、ホストパラメーターではなく非数値メタデータとして状態ツリーとプリセットに保存する。
- **Output Gain（画面上部の OUT）はユーザーが決める全体の音量**。プリセットには保存せず、プリセットを読んでも変えない。動かしても音色は「変更あり」にならない。ホストの状態（DAW のプロジェクト、Standalone の設定）には保存する。音色ごとの音量の差は **Preset Level**（`preset.level`、−24〜+24 dB、既定 0）が持ち、こちらはプリセットに保存する。最終的な音量は 2 つの積（D-055）。
- **保存しないもの**: 現在の物理押鍵、発音中のノート、PPQ カウンター、演奏中の Latch 保持和音（復元時に発音を再開しない）。
- Arp / Gate の項目がない状態・プリセットは、両機能 OFF として復元する。
- プリセット切替・状態復元の際は Arp の生成ノートを終了し、Gate を安全に再初期化する。
- マクロ名や割当配列など、固定数のホストパラメーターにしにくい非数値メタデータは別の状態ツリーに保持し、ホストの `getStateInformation` / `setStateInformation` で合わせて保存。
- プリセットのパラメーター読込とホストのstate restoreを統一する。
- Host automationとUI操作が同時でも不整合を起こさない。
- パラメーターの削除より非表示・互換マイグレーションを優先。
- Performance Lock `[暫定]`: Preset切替でPitch Bend RangeとVibrato設定を保持するユーザーオプション（既定OFF）。

## 13. UI/UX仕様

- ライト/ダークは初期版はダーク中心 `[暫定]`、高DPIスケール（Retina）対応。
- 画面スケーリング対応。文字の視認性優先。
- ノブはMouse Drag + Double Click Reset + Shift微調整 + 数値入力を検討。
- MIDI Learnの対象は4マクロを優先。
- 音色変更後は `*` 等のDirty表示、Save Asでユーザープリセット保存。
- Preset Previous/Next、Favorite、Category/Tags、Search。
- Advancedのタブ: OSC / Filter & Mod / Performance / FX / Rhythm / Macros。
- Gate パターンの編集はステップ単位の On/Off 切替と高さ調整。パターン選択でユーザー編集が失われる場合は Undo または確認を設ける `[暫定]`。
- キーボード操作とDAWウインドウフォーカスを壊さない。キーを受け取るのは文字の入力欄だけで、入力を終える（Esc / Return、または欄の外をクリック）とホストへ返す。ボタン、スイッチ、選択欄はキーを受け取らない（D-076）。
- **出力メーター**: ヘッダーの OUT のスライダーの下に、細いステレオのピークメーター（−48〜0 dB）。安全用リミッターが効く大きさになると `LIMIT` と表示する（D-071）。
- 画面の文言は英語のみ（D-070）。
- Standalone 実行時のみ、画面下部に演奏用の鍵盤を表示する（プラグイン時は非表示）。鍵盤は 88 鍵（A0〜C8）。鍵盤の左に、マウスで操作するピッチベンド（離すと中央へ戻る）とモジュレーション（離した位置に残る）のホイールを置く。ホイールの表示は、外部の MIDI キーボードのホイールにも追従する（D-067）。
- アプリアイコンとロゴは `assets/branding/` で管理し、UI ヘッダー・Standalone・製品紹介ページで共通に使う。
- ON/OFF、Simple/Advanced切替はオーディオスレッド上の状態を破壊しない。

## 14. DSP・性能・品質要件

### 14.1 リアルタイム安全
- `processBlock` 内でのファイルアクセス、JSONパース、ロック待機、メモリ確保、ログ書き込み禁止。
- 音声スレッドではパラメーター値の軽量取得と、事前生成されたimmutable snapshotを使用。
- UI/FX/プリセットの状態はメッセージスレッドで構築し、リアルタイム安全な更新機構で渡す。
- `prepareToPlay()`でDSPバッファ等を確保、`releaseResources()`で後始末。
- サンプルレート変更、バッファ長変更、DAW停止/再開、オフラインレンダリングに耐える。

### 14.2 レベル/安全
- ボイス数やユニゾン数が変わっても極端な音量上昇を抑えるゲイン正規化。
- Feedbackや高ResonanceでNaN/Inf、異常振幅が出ない。
- 必要なら出力に安全なソフトリミットを設けるが、過度な常時コンプレッションは避ける。
- DCオフセット検査。サウンドがモノラルにしても破綻しない。

### 14.3 実測する項目
- M1 Maxを基準として、48kHz/128samples、16音コード連打、各OSC 7ユニゾン、FXすべてONで音切れ・異常発熱・プチノイズがないか計測。
- CPU使用率とオーディオスレッドのピーク処理時間を数値記録。重ければエンジンの最適化を優先し、仕様値をむやみに下げない。
- 44.1/48/96kHzでピッチ、エンベロープ時間、Filter/FX挙動の一貫性。
- 低音域/高音域を含むスペクトル比較、特にSaw、Pulse、Wavefoldのエイリアシングを確認。

## 15. テスト計画と受け入れ条件

### 自動テスト
1. MIDI Note On/Offで正しいピッチと消音。
2. 同時16音、Release、Sustain、Voice Stealで音声処理が安定。
3. Pitch Bend上下0/2/12/48半音の数学的境界が正しい。
4. CC1=0でVibrato深さゼロ、CC1=127でDepth最大、RateとDelayも期待どおり。
5. Macro=0.5で割当オフセット0、Macro=0/1で指定デルタ、複数割当加算後にクランプ。
6. AdvancedでBase値を変更してもMacroの割当が失われない。
7. Preset保存→再読込、ホスト状態シリアライズ→復元で一致。
8. すべてのFXのバイパスと一括バイパス、Reverb/Delay tailの破綻なし。
9. NaN/Inf、不正入力、壊れたプリセット、未対応versionの処理。
10. 異なるサンプルレート・ブロックサイズでクラッシュなし。

11. Trance Gate: 全 ON / 全 OFF / Depth 0、8/16/32 ステップの周回、ステップ値 0.5 で 50 % 音量かつ左右同ゲイン、演奏中の変更でクリックなし、閉じたステップ中も Delay / Reverb の tail が残る。
12. Arpeggiator: 各モードの発音順序、Octaves と音域外スキップ、Gate Length、Random の再現性、Latch、CC64、同音重複、ON/OFF・All Notes Off・ホスト停止/シークでノートが残らない。
13. Host/Timing: BPM 120 で 1/16 = 125 ms・1/16T ≒ 83.33 ms、Swing で 2 ステップ合計が不変、44.1/48/96 kHz × 32/128/512/2048 samples で整合、ループ・ジャンプ・テンポチェンジでドリフトなし。

（11〜13 の全項目は `docs/ADDON_TRANCE_GATE_ARPEGGIATOR.md` §10）

### DAWでの手動検証
- CubaseでAU（対応していれば）/VST3をインストール・スキャン、Instrument Trackで発音。
- MIDIキーボード演奏、Pitch Bend、CC1、Sustain、ホストテンポ同期。
- オートメーション（Macro / Filter / FX）を記録・再生。
- プロジェクト保存→DAW再起動→同じ音色を復元。
- 複数インスタンス、音源切替、バウンス/オフライン書き出し。
- Arp / Gate: 再生・停止・ループ・シーク・テンポチェンジでの同期、オフライン書き出しの再現性、ノートの張り付きがないこと。
- Logic ProでAU動作検証（利用可能な場合）。

### 音質の試聴シナリオ
- SuperSAW: 4〜6声和音、高域のキラつきとセンターの芯。
- Brass: 短音＋レガート気味のコードでフィルター立ち上がり。
- Stab: 8分/16分の反復で粒立ち、Driveと短いRoom。
- Mix Ready: 既存ドラム・ベーストラックに乗せても低域過多でない。

## 16. 推奨ディレクトリ構成

```text
PolyBrisk/
├── CMakeLists.txt
├── README.md
├── CLAUDE.md
├── SPEC.md
├── docs/
│   ├── ARCHITECTURE.md
│   ├── PARAMETER_IDS.md
│   ├── PRESET_SCHEMA.md
│   ├── TEST_PLAN.md
│   ├── ADDON_TRANCE_GATE_ARPEGGIATOR.md
│   └── DECISIONS.md
├── external/                  # JUCEはgit submodule / FetchContent等で固定
├── src/
│   ├── PluginProcessor.h/.cpp
│   ├── PluginEditor.h/.cpp
│   ├── synth/
│   │   ├── SynthEngine.h/.cpp
│   │   ├── Voice.h/.cpp
│   │   ├── Oscillator.h/.cpp
│   │   ├── Unison.h/.cpp
│   │   ├── Transient.h/.cpp
│   │   ├── Envelopes.h/.cpp
│   │   ├── Filters.h/.cpp
│   │   └── MidiController.h/.cpp
│   ├── rhythm/
│   │   ├── RhythmClock.h/.cpp       # BPM/PPQ/再生位置/ステップ境界/Swing
│   │   ├── TranceGate.h/.cpp        # オーディオゲイン処理
│   │   ├── GatePattern.h/.cpp       # immutableな8/16/32ステップパターン
│   │   ├── Arpeggiator.h/.cpp       # 入力ノート追跡・生成ノート管理
│   │   └── ArpNoteScheduler.h/.cpp  # サンプル精度のOn/Offスケジュール
│   ├── fx/
│   │   ├── FxChain.h/.cpp
│   │   ├── Drive.h/.cpp
│   │   ├── Chorus.h/.cpp
│   │   ├── EQ.h/.cpp
│   │   ├── Imager.h/.cpp
│   │   ├── Delay.h/.cpp
│   │   └── Reverb.h/.cpp
│   ├── state/
│   │   ├── Parameters.h/.cpp
│   │   ├── MacroEngine.h/.cpp
│   │   ├── PresetManager.h/.cpp
│   │   └── StateSerializer.h/.cpp
│   └── ui/
│       ├── MainView.h/.cpp
│       ├── SimpleView.h/.cpp
│       ├── AdvancedView.h/.cpp
│       ├── PresetBrowser.h/.cpp
│       ├── RhythmSimpleView.h/.cpp
│       ├── RhythmAdvancedView.h/.cpp
│       └── MacroEditor.h/.cpp
├── assets/
│   └── branding/              # アイコン、ロゴ（SVG 正本と生成物）
├── site/                      # 製品紹介ページ（静的 HTML）
├── presets/
│   ├── factory/
│   └── starter/
├── tests/
│   ├── dsp/
│   ├── midi/
│   ├── rhythm/
│   ├── state/
│   └── fixtures/
└── scripts/
    ├── build.sh
    ├── test.sh
    ├── validate.sh
    └── make_icons.sh
```

注: 表記上の `*.h/.cpp` は例示。実際には `*.h` と `*.cpp` を別ファイルとして作る。

## 17. 実装フェーズ（Claude Code向け）

**各フェーズ終了時にビルド/テスト/動作確認を行う。単にソースを生成して完了としない。**

### Phase 0 — Skeleton / Audio Output
- CMake/JUCE環境、AU/VST3 Instrument + Standalone、APVTS、Build Scripts、UnitTests。
- アプリアイコン・ロゴの暫定版、製品紹介ページの初版（未実装機能は開発中と明示）。
- MIDI→単音Sine/Saw→Stereo Outの最小音源。
- Done: Debug/Releaseビルド、プラグインホストで発音、保存復元。

### Phase 1 — Poly Synth Core
- 16 voice、OSC A/B、基本波形、Unison、ADSR、Sustain、VoiceSteal。
- Done: 16音コードの発音とNoteOff、発振器のテスト、CPU測定。

### Phase 2 — Sound Character
- 3タイプFilter、Filter EG、Velocity、Transient/Pitch Snap、Phase Mode。
- Done: SuperSAW / Brass / Stab の3つの手動初期パッチを評価。

### Phase 3 — Performance & Macros
- Pitch Bend上下独立、CC1 Vibrato、Macro1〜4、relative mapping、ホストAutomation。
- Done: 直接編集との共存、DAWオートメーション、state roundtrip。

### Phase 4 — FX
- Drive、Chorus、EQ、Imager、Delay、Reverb、bypass/oversampling。
- Done: FX単体および全FXの安定性・レイテンシー・DAWテンポ連携。

### Phase 4.5 — Rhythm（Trance Gate / Arpeggiator）
詳細は `docs/ADDON_TRANCE_GATE_ARPEGGIATOR.md` §9。Phase 4 完了後、Phase 5 の前に行う。

**4.5a — 共通クロックと Trance Gate**
- BPM / PPQ / バッファ内ステップ境界 / Swing を扱う `RhythmClock` をテスト駆動で実装。
- 8/16/32 ステップ、Depth、Smooth、Gate ON/OFF。Delay / Reverb より前の音声経路へ統合。状態の保存復元。
- Done: 120 BPM・1/16 で 8 ステップ = 1 秒、テンポ変更・シークでも周期が崩れず、クリックなし。

**4.5b — Arpeggiator**
- 押鍵・離鍵・CC64・Latch の状態機械、Up / Down / Up-Down / Random / As Played と Octaves。
- Rate、Gate Length、Swing、Velocity を内部ノートイベントに反映。Arp と Gate の同時使用、ON/OFF 切替、再生停止、オフライン処理を検証。
- Done: 発音順序の正確さ、ノート張り付きなし、ホストタイムラインと同期。

### Phase 5 — UI & Preset System
- Simple / Advanced、Macro editor、ブラウザー、Search/Tag/Favorite、User Preset。
- Standalone の画面内鍵盤、UI へのロゴ組込み、紹介ページへの実画面スクリーンショット掲載。
- Simple の RHYTHM セクション、Advanced の Rhythm タブ、Gate パターンの選択・ユーザーパターン保存、RHYTHM パラメーターのマクロ割当。
- Done: 全パラメーター操作・Save/Load・ホスト復元。Starter24音色。

### Phase 6 — Sound Design & Polish
- Factory64→Factory128の段階的制作（v0.18 で 256 から変更）。音量の整合、使える音か評価。
- UIスケーリング、アクセシビリティ、プリセット切替時ノイズ対策。
- 紹介ページへプリセットのデモ音源を掲載。
- 128 音色の目標内で Gate / Arp を生かした音色を制作し、BPM 90 / 120 / 140 / 180 で試聴する（総数目標は変更しない）。
- Done: 128音色の各パッチにメタデータ、4マクロ、試聴検証記録。

### Phase 7 — Release
- AU/VST3/Standalone検証、署名/Notarization（外部配布時）、インストーラ、ユーザーガイド、紹介ページの公開準備（ダウンロード導線、ライセンス表記）。
- Done: 複数ホスト・環境でインストール→録音→書き出し→復元まで成功。

### 実装状況（v0.9、2026-11-21）

| Phase | 実装 | 自動テスト | 実機・聴感での確認 |
|---|---|---|---|
| 0 Skeleton | 済み | 済み | DAW での発音・保存復元は未確認 |
| 1 Poly Core | 済み | 済み | 未確認 |
| 2 Sound Character | 済み | 済み | フィルター 3 種の聴感評価は未実施 |
| 3 Performance & Macros | 済み | 済み | DAW のオートメーションは未確認 |
| 4 FX | 済み | 済み | FX の聴感評価は未実施 |
| 4.5 Rhythm | 済み | 済み | 実ホストでのテンポ同期・ループ・シークは未確認 |
| 5 UI & Presets | 済み（Starter 24 を含む） | 済み（UI は画像の目視） | 実機でのマウス操作・DAW 内表示は未確認 |
| 6 Sound Design | **128 音色（目標に到達）** | 済み（128 音色の検査） | Standalone で 128 音色すべてを試聴済み（D-052〜D-054、D-058、D-059、D-061〜D-064）。DAW 上では未確認 |
| 7 Release | ユーザーガイド、パッケージ作成スクリプト、署名なしの pkg まで | auval / pluginval（strictness 10）合格 | 署名・公証・複数ホスト検証は未実施。`docs/RELEASE_CHECKLIST.md` |

## 18. 現時点の未決定事項

以下は**開発開始の阻害要因ではない**。記載の暫定値で実装を始めてよい。変更時は `docs/DECISIONS.md` に記録。

1. **プラグイン識別子、ロゴ・配色** — 製品名 POLYBRISK とメーカー名 CHEEBOW は決定済み（D-005）。Bundle ID・AU製造者コード/プラグインコード等は配布前に確定（**公開後は変更不可**。`docs/RELEASE_CHECKLIST.md`）。ロゴ・配色は暫定案を採用済み（D-013）で、配布前に商標確認とあわせて確定。
2. **最低macOS、Intel Macサポート、販売/公開形態** — 当面Apple Silicon/macOS 13+、ライセンス確認。
3. **DSPの音質チューニング** — Filter各タイプ、SuperSAWのDetune曲線、Driveの倍音、Chorus/FXの設計。実装後のAB試聴で確定。
4. **プリセット制作** — 128音色の完成はソフト開発とは別に音色設計と試聴が必要。24→64→128の段階を採る。
5. **MIDI Learnの保存単位** — 初期はプリセット単位。Globalへ拡張の余地を残す。
6. **RHYTHM の `[暫定]` 項目** — Gate Smooth の既定 3 ms、初期収録パターン、Arp の Restart 既定（Note On）、RHYTHM 独立バイパスの有無、DAW 停止時の Arp の扱い、マルチチャンネル MIDI の扱い、パターン上書き時の Undo/確認。Phase 4.5 で確定する。
7. **製品紹介ページの公開先** — GitHub Pages で公開済み（<https://cheebow.github.io/POLYBRISK/>、D-075）。独自ドメインは未定。
8. **JUCE のライセンスと配布形態** — AGPLv3 か商用か（D-002）。署名・公証はこの判断と識別子の確定後に行う。Developer ID Installer 証明書の発行が必要。
9. **Factory 128 音色の仕上げ** — 数は目標に届いた（D-064）。128 音色とも Standalone で試聴済み。近いと測定された組の聴き比べも一巡した（D-066）。残っているのは、DAW 上での試聴。

## 19. 作業ルール

- 仕様にない機能を独断で大規模追加しない。必要なら `docs/DECISIONS.md` に候補と影響を記録。
- レイヤードなアーキテクチャを守る: DSPはUI、ファイルシステム、JSONに依存させない。
- 1フェーズ1PR相当の小単位で進める。まずコンパイルできる状態を維持。
- 新しいパラメーターは単一レジストリへ定義し、Parameter IDと保存形式を一致させる。
- 実装完了の主張にはビルド成功、テスト結果、DAW検証状態を明記。
- 音色品質はテストだけで確定しない。実際のMIDI試聴を必須とする。

## 20. RHYTHM — Trance Gate / Arpeggiator（v0.6 で追加）

**詳細仕様の正本は `docs/ADDON_TRANCE_GATE_ARPEGGIATOR.md`。** ここには本書へ統合した要点だけを記す。

### 20.1 Trance Gate
- 全ボイス合計に掛けるステレオ音量ゲート。位置は Imager の後、Delay / Reverb の入力の前。
- ゲイン: `targetGain = 1 - depth × (1 - step)` を `Smooth`（0.5〜30 ms）で平滑化し、左右に同じ値を掛ける。ゼロ長の切替は禁止。
- パラメーター: `gate.enabled`、`gate.steps`（8 / 16 / 32）、`gate.rate`（1/8, 1/16, 1/32, 1/8T, 1/16T, 1/64。既定 1/32）、`gate.depth`、`gate.smooth`、`gate.swing`（0〜50 %）、`gate.restart`（Transport / Note On）。
- パターン（最大 32 ステップ、各 0〜100 %）はホストパラメーターにせず、状態・プリセットに保存して UI から編集する。初期収録パターン: Straight 8ths / Pulse 16 / Offbeat / Syncopated / Build Up / Full `[暫定]`。既定のパターンは Shifted Long（Fast 1/32 グループ）。収録パターンはそれぞれ前提の Rate を持ち（Fast 1/32 グループは 1/32 か 1/64、それ以外は 1/16）、メニューから選ぶと Rate がその値になる。Undo はパターンと Rate を一緒に戻す（D-044）。

### 20.2 Arpeggiator
- Arp ON のときは、ホストからの Note On を直接発音させず、内部生成した Note On/Off に置き換える。外部 MIDI 出力はしない。
- パラメーター: `arp.enabled`、`arp.mode`（Up / Down / Up-Down / Random / As Played）、`arp.rate`（1/4, 1/8, 1/16, 1/32, 1/8T, 1/16T）、`arp.octaves`（1〜4）、`arp.gate`（10〜100 %）、`arp.swing`（0〜50 %）、`arp.velocity`（25〜200 %）、`arp.latch`、`arp.restart`（Transport / Note On）。
- Random は決定的な擬似乱数とし、同じ条件のオフライン書き出しが再現できること。
- Latch / Sustain / All Notes Off / プリセット切替 / ホスト停止・シーク / サンプルレート変更のいずれでも、取り残されたノートを発生させない。
- 生成ノートはバッファ内の正しいサンプル位置で発行し、固定レイテンシーを追加しない。

### 20.3 共通クロック
- ホストの BPM・再生状態・PPQ 位置を読み、PPQ を音楽的位相の正本とする。取得できない場合や停止中の手弾きは内部クロック（BPM 不明なら 120）。
- Swing: 2 ステップを 1 組とし、前を `T(1+k)`、後を `T(1-k)`（k = swing % / 100）として合計時間を保つ。Arp と Gate は別々の Swing 値を持つ。
- ブロック長（32〜2048 samples）や分割位置に依存しないステップ判定。

### 20.4 実装時期
Phase 4（FX）の後、Phase 5（UI / プリセット）の前に **Phase 4.5** として実装する（§17）。UI は Phase 5、音色制作は Phase 6 に含める。

## 参考（実装時に公式ドキュメントを確認）

- JUCE CMake API: https://github.com/juce-framework/JUCE/blob/master/docs/CMake%20API.md
- JUCE AudioPlugin CMake例: https://github.com/juce-framework/JUCE/blob/master/examples/CMake/AudioPlugin/CMakeLists.txt
- JUCE AudioProcessorValueTreeState: https://docs.juce.com/develop/classjuce_1_1AudioProcessorValueTreeState.html

