# POLYBRISK — Test Plan

SPEC §15 の受け入れ条件を、自動テストと手動検証に対応づけて追跡する。
状態: ✅ 実装済み・合格 / 🟡 一部 / ⬜ 未着手。結果の詳細は各フェーズの報告を参照。

## 実行方法

```bash
scripts/build.sh            # Debug + Release
scripts/test.sh Debug       # ctest (JUCE UnitTest ランナー)
scripts/test.sh Release
scripts/validate.sh Release # auval (+ pluginval があれば)
scripts/bench.sh            # DSP の CPU 計測 (Release)
scripts/render_presets.sh   # Factory プリセット 128 音色を build/renders/*.wav に書き出す (試聴用)
python3 tools/preset_similarity.py   # 書き出した WAV から、同じカテゴリ内で似ている音色の組を挙げる
PLUGINVAL_STRICTNESS=10 scripts/validate.sh   # リリース前の厳しい検証
scripts/package.sh          # インストーラー (.pkg) の作成
```

## 自動テスト（SPEC §15）

| # | 項目 | Phase | 状態 | テスト |
|---|---|---|---|---|
| 1 | MIDI Note On/Off で正しいピッチと消音 | 0 / 1 | ✅ | `tests/dsp/EngineTests.cpp`, `tests/midi/ProcessorMidiTests.cpp` |
| 2 | 同時16音、Release、Sustain、Voice Steal | 1 | ✅ | `tests/dsp/EngineTests.cpp`, `tests/midi/ProcessorMidiTests.cpp` |
| 3 | Pitch Bend 0/2/12/48 半音の境界 | 3 | ✅ | `tests/midi/PerformanceTests.cpp` |
| 4 | CC1 Vibrato（Depth / Rate / Delay） | 3 | ✅ | `tests/midi/PerformanceTests.cpp` |
| 5 | Macro 0.5 ニュートラル、0/1 のデルタ、加算後クランプ | 3 | ✅ | `tests/state/MacroTests.cpp` |
| 6 | Base 値編集で Macro 割当が失われない | 3 | ✅ | `tests/state/MacroTests.cpp` |
| 7 | Preset 保存→再読込、ホスト状態の復元が一致 | 0 / 5 | ✅ | `tests/state/StateTests.cpp`, `tests/state/PresetTests.cpp`, `tests/state/MacroTests.cpp` |
| 8 | FX バイパス、一括バイパス、tail | 4 | ✅ | `tests/fx/FxTests.cpp` |
| 9 | NaN/Inf、不正入力、壊れたプリセット、未対応 version | 0 / 4 / 5 | ✅ | `tests/state/StateTests.cpp`, `tests/state/PresetTests.cpp`, `tests/dsp/FilterTests.cpp`, `tests/fx/FxTests.cpp` |
| 10 | 異なるサンプルレート・ブロックサイズでクラッシュなし | 0〜 | ✅ | `tests/midi/ProcessorMidiTests.cpp`, `tests/fx/FxTests.cpp`, `tests/rhythm/*`, `tests/state/PresetTests.cpp` ほか |
| 11 | Trance Gate（追加仕様 §10 Gate 1〜7） | 4.5a | ✅ | `tests/rhythm/TranceGateTests.cpp` |
| 12 | Arpeggiator（追加仕様 §10 Arp 1〜8） | 4.5b | ✅ | `tests/rhythm/ArpeggiatorTests.cpp` |
| 13 | Host/Timing（追加仕様 §10 Host/Timing 1〜7） | 4.5a / 4.5b | ✅（実ホストでの確認は未実施） | `tests/rhythm/RhythmClockTests.cpp`、上の 2 ファイル |

### テスト内容（全フェーズ実装後、アサーション 224,017 件）
- **Oscillator waveforms**（`tests/dsp/OscillatorTests.cpp`）: Saw / Square / Pulse / Triangle / Sine × 44.1/48/96 kHz × 27.5〜7040 Hz のピッチ（誤差 1 cent 未満）・DC・ピーク、位相 0 での立ち上がり、Pulse Width とデューティー比（5〜95 %）、PolyBLEP / PolyBLAMP が単純波形より折り返しを減らすこと。
- **Unison**（`tests/dsp/UnisonTests.cpp`）: Detune カーブの単調性、1〜9 本 × Detune × Spread の全組合せで「デチューンの左右対称」「パンの左右対称」「パワー一定」「奇数本は中央 1 声」、本数によるレベル安定（±1.5 dB 以内）、Spread 0 で完全モノ・Spread 100 % でもモノ合成の芯が残ること、Retrigger の再現性と Free のボイス差、Noise の独立性・白色性・音程非依存。
- **ADSR envelope**（`tests/dsp/EnvelopeTests.cpp`）: 44.1/48/96 kHz での Attack / Decay / Release 時間、Sustain 保持、Release 後の完全停止、アタック中の離鍵で段差なし、10 秒 Decay の収束、異常値でも有限。Sustain 0 は減衰しきった時点で停止。
- **Filter**（`tests/dsp/FilterTests.cpp`）: Clean の各モードの −3 dB 点と傾き（12 / 24 dB/oct）、44.1〜192 kHz でのカットオフ精度、Nyquist 手前へのクランプ、Clean が Drive 0 で厳密に線形・Analog / Punch は非線形、レゾナンスのピーク（ノブで増加、Analog / Punch は Clean より小さい）、Drive による倍音の増加と音量の抑制、レゾナンス・Drive 最大 + 乱暴なカットオフ変調での有界性（3 キャラクター × 5 モード × 2 レート）、NaN 入力からの復帰、全開フィルターの透明性、Filter EG（正・負・Note Off）、Key Tracking（0 / 50 / 100 %）、Velocity → Cutoff / Amp、**3 キャラクターの出力が互いに十分に異なること**、演奏中のカットオフ / レゾナンスの急変でクリックなし、サンプルレート間のレベル一致、極端な設定 × 16 音で有限・有界。
- **Transient**（`tests/dsp/TransientTests.cpp`）: OFF のとき出力が完全一致、Noise Burst が OSC から独立・フェードイン・指定時間で約 −60 dB・センター定位（44.1/48/96 kHz）、Amount の線形性と 0 での無音、Noise Color の Dark → Bright、Noise Burst がフィルターを通ること、Pitch Snap の開始オフセット（正・負・Amount 半分）と収束後のピッチ（誤差 0.03 % 未満）、モードごとの有効・無効、OSC 間の音程関係の保持、Pitch Snap の連続性とレート間の一致、極端な設定 + スティールで有限・有界。
- **Performance**（`tests/midi/PerformanceTests.cpp`）: 14 bit ピッチベンドの両端、Bend 0 / 2 / 12 / 48 半音 × 3 レートで誤差 1 cent 未満、上下独立、OSC 間の音程の保持、ベンドのスムージング、CC1 = 0 / 127 での Vibrato の深さと Rate（3 レート）、Vibrato Delay、Bend + Vibrato の加算、プロセッサー経由の MIDI と Symmetric / Independent。
- **Macros**（`tests/state/MacroTests.cpp`）: `delta()` の端点・ニュートラル・単調性、正規化が APVTS と一致、Macro 50 % で Base 値のまま・0 / 100 % で指定のオフセット（非線形レンジは正規化空間で加算）、複数マクロの加算とクランプ、Base 値を編集しても割当が残ること、音への反映、割当の規則（連続値のみ・最大 8・上書き・削除）、状態のラウンドトリップ（バイト一致）、壊れた / 古いマクロデータ、CC によるマクロ操作とホストパラメーターへの反映、MIDI Learn（予約 CC の除外、1 CC = 1 マクロ）。
- **FX**（`tests/fx/FxTests.cpp`）: 全 OFF・一括バイパスで完全な素通し、各 FX と一括バイパスの ON/OFF でクリックなし、Drive の 6 カーブ（有界・互いに異なる・倍音・DC なし・Auto Gain・Mix 0 で dry）と 4x オーバーサンプリングでの折り返し量、Chorus（ステレオ化・Width 0 でモノ・極端な設定でピッチ飛びなし・2 タイプの差）、EQ（3 バンドのゲインと周波数 × 3 レート、掃引）、Imager（0 / 100 / 200 %、モノ合成、Mono Bass）、Delay（同期音価の時刻 × 3 レート、120 BPM フォールバック、Ping Pong、Mix の法則、Feedback 95 % で有界、Time 変更でクリックなし、Ducking）、Reverb（RT60 と Decay の一致、Pre-delay、最大設定で有界、3 タイプの差、切替でクリックなし）、Delay / Reverb の並列性、Note Off 後の尾と最終的な完全無音、全 FX ON × 16 音 × レート × ブロック長。
- **Rhythm clock**（`tests/rhythm/RhythmClockTests.cpp`）: 音価（BPM 120 で 1/16 = 125 ms、1/16T ≒ 83.33 ms）、Swing 0 の等間隔と負の位置、Swing で 2 ステップ合計が不変、ホスト追従 / 内部クロック、ジャンプ・停止の検出、不正なテンポのフォールバック。
- **Trance Gate**（`tests/rhythm/TranceGateTests.cpp`）: ゲインの式、全 ON で 1 のまま・全 OFF でゼロに到達・Depth 0 で素通し、8 / 16 / 32 ステップの周回と「8 ステップ = 1 秒」、境界のサンプル精度 × 5 Rate × 3 レート × 4 ブロック長（結果が一致）、Swing と Smooth、演奏中の変更で段差・NaN なし、ステップ値 0.5 で 50 % かつ左右同ゲイン、閉じたステップ中も Reverb の尾が残ること、FX BYPASS で Gate が切れないこと、Restart = Transport / Note On、テンポ変更とループジャンプでの位置固定、パターンの状態保存と古い状態・壊れたパターンの読込、初期パターンが追加仕様どおり。
- **Arpeggiator**（`tests/rhythm/ArpeggiatorTests.cpp`）: Up / Down / Up-Down（端を重ねない）/ As Played、Octaves と 127 超のスキップ、Random の再現性、タイミング × 6 Rate × 3 レート × 4 ブロック長（一致）、Gate Length と 100 % での Off → On の順序、Swing、Velocity の保持・係数・クランプ、Latch OFF / ON と和音の置換、CC64、同音重複、Restart = Note On / Transport、ループ・シーク・ホスト停止でノートが残らないこと、テンポ変更でドリフトなし、エンジン経由（直接ノートとの切替、Arp OFF、All Notes Off / All Sound Off）、Arp + Gate をプロセッサー経由で × 3 レート × 4 ブロック長（ブロック長に依存しない）。
- **Presets**（`tests/state/PresetTests.cpp`）: **Output Gain をプリセットが保存も変更もしないこと・Preset Level の保存（D-055）**、**Init で全設定が既定値に戻ること（D-057）**、JSON のラウンドトリップ、読みやすい表現（選択肢は名前、ON/OFF は真偽値、パターンは rhythm のみ）、壊れた / 未対応ファイルの拒否（音色は変えない）、寛容な読込（未知フィールド・欠落・範囲外・不正値）、**Factory 103 音色の検査**（ID・範囲・選択肢名・メタデータ・4 マクロ・重複なし・カテゴリ配分、Trance Gate カテゴリは全音色で Gate が ON）、全音色のレンダリング（有限・可聴・フルスケール未満・DC なし・**どの 2 つも十分に違う**）、Gate / Arp 音色を BPM 90 / 120 / 140 / 180 で、発音中の全音色切替、一覧・検索・カテゴリ・前後移動、User の保存・再読込・上書き・名前変更・削除、Import / Export と壊れたファイルの無視、お気に入りと Performance Lock の永続化、ホスト状態にプリセット情報・Dirty・表示モードが残りプリセットファイルなしで再現されること、プリセット切替で Arp が止まること。
- **Modulation**（`tests/dsp/ModulationTests.cpp`、D-041）: LFO の 5 波形の範囲と始点・S&H が状態を持たないこと、**深さ 0 では出力がビット単位で変わらないこと**、Pitch / Fade In / Amp（切り替えで段差なし）/ Cutoff / Pulse Width（Saw には効かない）、テンポ同期がホスト位置に固定されブロック長によらないこと、Mono（1 声・後着優先・押さえている鍵へ戻る）、Mono は再始動し Legato はしないこと（どちらも段差なし）、ペダル保持中の移動、グライド（中間点・到達・Poly・Legato は重ねたときだけ）、Sub（-1 / -2 オクターブ・センター・OSC の Tune に非追従）、Noise（持続・Color・離鍵で停止）、アフタータッチ（ビブラート・Cutoff・量 0 で不変・チャンネルプレッシャーと Poly Aftertouch の受信）、Phaser / Flanger（極端な設定で有限・上限内、OFF は素通し、ON の切り替えで段差なし）、Gate の 1/64 とランダム生成（再現性・1 拍目が開く・開くステップ数）。
- **Oversampling / halfband**（`tests/dsp/HalfbandTests.cpp`）: 通過域の平坦性、阻止域 -95 dB 以下、リセット、レートごとの倍率、**エンジン経由の可聴帯域エイリアシング（-50 dB 以下）**。
- **SynthEngine**（`tests/dsp/EngineTests.cpp`）: 全波形 × 3 レート × 4 音程のピッチ、Octave / Semitone / Fine、16 音コードの全音検出と完全消音、同音重複、サステイン、スティール順序（Release 中の最小 → 最古）、スティールのデクリック、フェード中の離鍵、OSC A/B のミックスと ON/OFF、ユニゾンのステレオ、Phase Mode、Velocity、リミッター（線形領域・上限・最悪条件）、通常の和音がリミッター未満、All Notes Off / All Sound Off、**ブロック分割に依存しない出力（完全一致）**、演奏中のパラメーター変更（クリックなし・乱数ファズで有限）、範囲外ノート。
- **Processor MIDI**（`tests/midi/ProcessorMidiTests.cpp`）: バス構成、サンプルオフセット精度、Note Off / Velocity 0、CC64 / CC123 / CC120、パラメーターがエンジンへ届くこと、44.1/48/88.2/96 kHz × ブロック 32〜2048 + 可変ブロックで 18 音（スティール発生）× 2 OSC × 7 ユニゾンが有限・有界・ブロック非依存、`prepareToPlay` の再実行、空/モノバッファ。
- **State**（`tests/state/StateTests.cpp`）: 全パラメーターを既定値以外にしたラウンドトリップ、バイト列とレンダリング結果の一致、破損/空/他プラグイン/途中切断データの拒否、未対応 version の拒否、欠落→既定値、未知フィールド無視、範囲外クランプ、非有限値→既定値。
- **Parameter registry**（`tests/state/ParameterRegistryTests.cpp`）: ID 一意・書式・範囲、整数/選択肢の整合、マクロ割当可否 = 連続値のみ、APVTS との一致、Wave 選択肢と DSP enum の順序、SPEC の既定値、`docs/PARAMETER_IDS.md` との一致。

### Rhythm の受け入れ条件（追加仕様 §10）
`docs/ADDON_TRANCE_GATE_ARPEGGIATOR.md` §10 の各項目を自動テスト化した（上の Rhythm clock / Trance Gate / Arpeggiator）。「DAW オフラインバウンス」「プロジェクト再起動後の復元」は実ホストでの確認が残る。項目の要点:

- **Gate**: 全 ON・Depth 100 % で振幅不変、全 OFF・Depth 100 % でゼロに到達、Depth 0 で素通し、8/16/32 ステップの周回、ステップ値 0.5 で 50 % かつ左右同ゲイン、演奏中の Rate / Swing / Pattern 変更で NaN・クリックなし、閉じたステップ中も Delay / Reverb の tail が残る。
- **Arp**: C4/E4/G4 で Up / Down / Up-Down（端を二重発音しない）、Octaves 2 と 127 超のスキップ、Gate Length 50 % の Note Off 位置、As Played の順序、Random の再現性、Latch OFF/ON と和音の置換、CC64・Velocity 0・同音重複、Arp ON/OFF・プリセット変更・All Notes Off・ホスト停止/シークでノートが残らない、Gate との同時使用。
- **Host/Timing**: BPM 120 で 1/16 = 125 ms・1/16T ≒ 83.33 ms（境界誤差 1 サンプル程度）、Swing 0 で等間隔・Swing 50 で 2 ステップ合計が不変、44.1/48/96 kHz × 32/128/512/2048 samples で整合、ループ・ジャンプ・停止/再開・テンポチェンジでドリフトなし、状態復元（押鍵状態は復元しない）、Arp/Gate 情報のないプリセットは両方 OFF、CPU の再計測。

## ホスト非依存の検証

| 項目 | 状態 |
|---|---|
| `auval -v aumu Pbrk Chbw`（識別子の確定前は `aumu Pbk1 Pbrk`。D-077） | ✅ 2026-11-23 Release（確定した識別子）: AU VALIDATION SUCCEEDED |
| pluginval（VST3 / AU、strictness 5、`/Applications/pluginval.app`） | ✅ 2026-11-21 Release（最終コード）: strictness 5 と 10 の両方で VST3 / AU とも SUCCESS（AU で警告 `Current program is -1` が 1 件、合否には影響なし） |

## DAW での手動検証（SPEC §15）

実機での確認が必要。結果は日付・ホストのバージョンとともにここへ追記する。

| 項目 | Cubase (VST3) | Logic Pro (AU) | Standalone |
|---|---|---|---|
| スキャン・インストール・発音 | ✅ Cubase で音源として読み込み、発音を確認（2026-11-23、ユーザー） | ✅ Logic Pro で AU 音源として読み込み、発音を確認（2026-11-23、ユーザー）。確認したのはここまで | ✅ 128 音色を試聴（2026-11-23、ユーザー） |
| MIDI キーボード演奏 | ⬜ | ⬜ | ⬜ |
| プロジェクト保存→再起動→復元 | ✅ 音色・動かしたノブ・音が復元された（2026-11-23、ユーザー） | ⬜ | ⬜（終了→再起動で状態復元） |
| オーディオ/MIDI デバイス設定 | — | — | ⬜ |
| 和音（16 音まで）、Sustain ペダル、ボイススティール時のノイズ（Phase 1） | ⬜ | ⬜ | ⬜ |
| フィルター（3 キャラクター × 5 モード）、Filter EG、Transient（Phase 2） | ⬜ | ⬜ | ⬜ |
| Pitch Bend / CC1（Phase 3） | ⬜ | ⬜ | ⬜ |
| オートメーション記録・再生（Phase 3〜） | ✅ Cutoff の書き込みと再生。ノブと音が記録どおりに動く（2026-11-23、ユーザー） | ⬜ | — |
| ホストテンポ同期（Phase 4） | ✅ Supersaw Gate がクリックと合う。120 → 140 のテンポ変更、小節の途中からの再生でもずれない（2026-11-23、ユーザー） | ⬜ | — |
| 複数インスタンス、バウンス/オフライン書き出し | 🟡 オーディオミックスダウンは再生と同じ音で、ノイズ・音切れなし（2026-11-23、ユーザー）。複数インスタンスは未確認 | ⬜ | — |
| FX 7 種の音、Delay のテンポ同期、バイパス切替（Phase 4、D-041） | ⬜ | ⬜ | ⬜ |
| LFO（4 つの行き先、テンポ同期）、Mono / Legato、グライド、Sub / Noise、アフタータッチ（D-041） | ⬜ | ⬜ | ⬜ |
| Trance Gate: ステレオのパターン、Width、Random、1/64（D-040 / D-041） | ⬜ | ⬜ | ⬜ |
| Arp / Gate: 再生・停止・ループ・シーク・テンポチェンジ、書き出しの再現性、ノート張り付きなし（Phase 4.5） | 🟡 Gate の再生・停止・テンポチェンジ・途中からの再生は確認（2026-11-23、ユーザー）。ループ、書き出しの再現性、Arp は未確認 | ⬜ | ⬜（内部クロック） |
| UI: Simple / Advanced、全タブの操作、ノブ（Shift・ダブルクリック・数値入力）、拡大縮小、Gate グリッド、マクロ編集、MIDI Learn（Phase 5） | ⬜ | ⬜ | ⬜（画面内鍵盤と BEND / MOD ホイールを含む） |
| プリセット: 選択・前後移動・検索・お気に入り（行の ★ をクリック）・Save As・Delete・Import / Export、Dirty 表示（Phase 5） | ⬜ | ⬜ | ⬜ |
| インストーラーからの導入（Phase 7） | ⬜ | ⬜ | ⬜ |

注: Cubase は AU をサポートしないため、Cubase では VST3 のみを検証する。

## 性能計測（SPEC §14.3）
M1 Max 基準、48 kHz / 128 samples、16 音 × 各 OSC 7 ユニゾン、全 FX ON。CPU 使用率とオーディオスレッドのピーク処理時間を数値で記録する。

- 🟡 **Phase 1（2026-11-21）**: DSP エンジン単体（フィルター・FX なし）を `scripts/bench.sh` で計測。基準条件で平均 6.38 %、ピーク 287 µs（予算の 10.8 %）。全結果は `docs/DECISIONS.md` D-019。
- 🟡 **Phase 2（2026-11-21）**: フィルター・Filter EG・Transient 込みで再計測。基準条件 + Analog LP24 で平均 9.57 %、ピーク 377 µs（予算の 14.1 %）。全結果は `docs/DECISIONS.md` D-029。
- ✅ **最終コード（2026-11-21）**: SPEC §14.3 の基準条件（16 音 × 各 OSC 7 ユニゾン × 全 FX ON）で平均 10.76 %、ピーク 419 µs（予算の 15.7 %）。Gate + Arp を加えても増えない。全結果は `docs/DECISIONS.md` D-038。
- ⬜ プラグインホスト内での計測（音切れ・発熱・プチノイズ）は未実施。

## 音質の試聴（SPEC §15）
自動テストだけで音色品質を確定しない。**2026-11-22 に、開発者が Standalone で 128 音色すべてを試聴した（D-052〜D-054、D-058、D-059、D-061〜D-064）。DAW 上での試聴と、下の表の個別項目は未実施。**

`scripts/render_presets.sh` が Factory 128 音色を、カテゴリごとの試聴フレーズ（128 BPM）で `build/renders/NNN_<名前>.wav` に書き出す。プラグインでは上部のプリセット名からブラウザーを開いて選ぶ。

| シナリオ | 素材 | 聴感での評価 |
|---|---|---|
| SuperSAW: 4〜6 声和音、高域のキラつきとセンターの芯 | SuperSAW 16 音色（Anthem Saw、Idol Hyper Saw、Mono Core Saw ほか） | ⬜ |
| Brass: 短音 + レガート気味のコードでフィルターの立ち上がり | Synth Brass 18 音色（Pop Brass、Sforzando Brass、Brass Swell ほか） | ⬜ |
| Stab: 8 分 / 16 分の反復で粒立ち、Drive と短い Room | Stab 16 音色（Bright Pop Stab、Tiny Tight Stab ほか） | ⬜ |
| Mix Ready: ドラム・ベースに乗せて低域過多でない | タグ「Mix Ready」「Dry」の音色 | ⬜ |
| Gate / Arp: BPM 90 / 120 / 140 / 180 で楽曲用途を確認 | Gate Saw Runner、Pump Saw、Triplet Gate Saw、Build Gate Saw、Chopped Chords、Arp Pulse Stab、Arp Pluck Lights | ⬜ |

聴くときの観点: フィルター 3 キャラクターの違いが音楽的に意味のある差か、Analog の飽和が和音で濁らないか、Resonance を上げたときの音量、Transient が輪郭になるか、Drive の 6 カーブの使い分け、Reverb 3 タイプの差、Chorus の揺れ、プリセット間の音量差（測定上は持続音で ±0.3 dB、減衰音はピーク基準）、プリセット切替時のノイズ、マクロの効き幅が音楽的か。
