# POLYBRISK — 追加仕様: Trance Gate / Arpeggiator

- **文書種別**: `SPEC.md` v0.2 に対する追加仕様（実装指示用）
- **文書バージョン**: v0.1
- **対象製品**: POLYBRISK（macOS AU / VST3 Instrument）
- **ステータス**: 機能採用決定。実装詳細の一部は `[暫定]`。
- **目的**: SuperSAW / Synth Brass / Stab のリズミカルな演奏を、簡単な操作で実現する。

> **優先順位**: 本書は `SPEC.md` v0.2 の追加・差分仕様である。衝突する箇所については本書を優先する。特に `SPEC.md` §0 の「アルペジエーター、シーケンサーは初期リリースで実装しない」という記述を、**標準的なArpeggiatorと音量パターン型Trance Gateは実装する**に変更する。汎用ステップ・シーケンサー（ステップごとの任意音程入力等）は依然として対象外。

---

## 1. 実装範囲と基本方針

### 必須

1. **Trance Gate**: 入力音声をステップパターンに従ってリズミカルに音量制御する。
2. **Arpeggiator**: 押さえた和音を指定順序・音価で内部的なMIDIノート列に変換し、POLYBRISKの音源に渡す。
3. 両機能を単独でも同時でも使用できる。
4. DAWのBPM、演奏位置、再生・停止、オフラインバウンスに対応。
5. **Simple** はON/OFFと使いやすい基本操作、**Advanced** はパターンや細部を編集。
6. 設定をプリセットおよびDAWのプロジェクト状態へ完全保存。
7. パラメーターの自動化とプリセット切替でクリック・ノート張り付き・音量暴走を起こさない。

### 対象外

- ステップごとの自由な音程入力、コード進行生成、長大なフレーズシーケンス。
- Arpeggiatorの外部MIDI出力（初期版では音源内だけで発音）。
- パターンのドラッグ＆ドロップMIDI書き出し、MIDIファイル書き出し。
- 複数レーン・ポリリズム・確率シーケンス・Euclidean generator。
- サイドチェイン入力から動作するゲート。

---

## 2. 既存DSPへの統合

```text
Host MIDI (Note / CC64 / Pitch Bend / CC1)
       |
       +--> MIDI Input State (held keys, sustain, channel)
                         |
                 [ARPEGGIATOR]
                 ON: generated note events
                 OFF: original note events
                         |
                 Synth Voice Engine
            OSC A/B + Transient -> Filter -> Amp
                         |
                    Voice Sum
                         |
              Drive -> Chorus -> EQ -> Imager
                         |
                   [TRANCE GATE]
                         |
        +----------------+----------------+
        |                |                |
       Dry          Delay (wet)      Reverb (wet)
        +----------------+----------------+
                         |
                    Output Gain
                         |
                     Stereo Out
```

- **Arpeggiator** は音源の前段で内部ノートイベントを作る。ホストから来たPitch Bend / CC1等の連続制御は既存仕様どおり音源へ伝える。
- **Trance Gate** は全ボイスの合計に作用する **グローバル・ステレオ音量ゲート**。オシレーターやフィルターの発音状態は変更しない。
- GateをDelay / Reverbの**入力より前**に置く。これにより、ゲートで刻んだ音の残響・反復は自然に残る。
- 既存の6種類のFX（Drive / Chorus / EQ / Imager / Delay / Reverb）とは別の **RHYTHM** セクションにする。
- **FX BYPASS** は既存の6FXだけを対象とし、Arp/GateのON/OFFには影響しない。RHYTHM独立バイパスを持たせてもよい `[暫定]`。
- Gateはパターンを変えない限り音色のピッチを変更しない。Arpは原則として1ステップごとに1ノートを生成する。

---

## 3. Trance Gate — 機能仕様

### 3.1 パラメーター

| ID案 | 表示 | 仕様 | 初期値 |
|---|---|---|---|
| `gate.enabled` | On | Off / On | Off |
| `gate.steps` | Steps | 8 / 16 / 32 | 16 |
| `gate.rate` | Rate | 1/8, 1/16, 1/32, 1/8T, 1/16T, 1/64 | 1/32（当初は 1/16。既定のパターン Shifted Long と対で変更。D-044） |
| `gate.depth` | Depth | 0〜100% | 100% |
| `gate.smooth` | Smooth | 0.5〜30ms（ゲイン遷移の平滑化） | 3ms `[暫定]` |
| `gate.swing` | Swing | 0〜50% | 0% |
| `gate.pattern` | Pattern | 最大32ステップの音量値（0〜100%） | 16ステップ基本パターン |
| `gate.restart` | Restart | Transport / Note On | Transport |

- ステップ値は **音量値**。100%=完全に開く、0%=完全に閉じる、中間値でアクセント・濃淡を表現できる。
- 初期のパターン編集UIはステップ単位の **On/Off切替＋高さ調整** に対応。細かいカーブの手描き編集は不要。
- `Depth=0` はゲート処理なし、`Depth=100` で各ステップの音量がそのまま適用される。
- ステップごとの波形ゲートではなく、パターン境界に対して `Smooth` でクリックを避ける。深いゲートでもアタックが鈍くなりすぎない範囲にする。
- `gate.pattern` はホストに32個の自動化パラメーターとして公開する必須要件ではない。長さと値を **プリセット/プラグイン状態** に保存し、UIから編集する。実装後に個別ホスト自動化が必要なら別途追加。

### 3.2 ゲイン計算

ステップ音量 `s`（0..1）、Depth `d`（0..1）、スムージング後ゲイン `g`：

```text
targetGain = 1 - d * (1 - s)
g = smooth(targetGain, smoothMs)
outputL = inputL * g
outputR = inputR * g
```

- ステップごとのゲインは同じ値を左右に適用し、意図せずステレオ定位が動かないようにする。
- `Smooth` は新しいステップの先頭でレベル遷移に使用する。ゼロ長の急峻な切替は禁止。
- Gate OFFにしたときは短いクロスフェード/スムージングで常時1倍へ戻す。

### 3.3 初期収録パターン `[暫定]`

| パターン | 16ステップ例（1=ON、0=OFF） | 意図 |
|---|---|---|
| Straight 8ths | `1010101010101010` | 基本的な8分刻み |
| Pulse 16 | `1110111011101110` | 軽快な16分ゲート |
| Offbeat | `0101010101010101` | 裏拍アクセント |
| Syncopated | `1011010010110100` | シンコペーション |
| Build Up | `1000100010101111` | 徐々に密度を増やす |
| Full | `1111111111111111` | 常時開く（設定確認用） |

- 上の 6 つに加え、Straight / Trance / Fast 1/32 / Syncopated / Accent / Fill の各グループに計 39 パターン（ほかに Stereo グループ 7 つ）を収録する（D-039。一覧は `src/rhythm/GatePattern.h`）。Accent グループはステップの音量に中間値（25 / 50 / 75 %）を使う。メニューではグループごとに見出しを付ける。
- **ステレオ（D-040 で追加）**: `gate.stereo` を ON にすると、パターンを左・右の 2 段で持ち、チャンネルごとに別のゲインを掛ける。`gate.width`（0〜100 %）は左右の分離で、下げると両チャンネルとも「どちらかが開いていれば開く」形に近づく（0 % でリズムは残り、定位だけ中央に寄る）。Stereo が OFF のときは従来どおり左右共通で、定位は動かない。ステレオ用のパターンを 7 つ収録し、選ぶと Stereo が ON になる。
- 8/32ステップの場合は専用パターンまたは既定パターンの規則的な拡張・切り出しを用意する。
- パターン選択でユーザー編集内容が失われる場合はUI上でUndoまたは変更確認を設ける `[暫定]`。

---

## 4. Arpeggiator — 機能仕様

### 4.1 パラメーター

| ID案 | 表示 | 仕様 | 初期値 |
|---|---|---|---|
| `arp.enabled` | On | Off / On | Off |
| `arp.mode` | Mode | Up / Down / Up-Down / Random / As Played | Up |
| `arp.rate` | Rate | 1/4, 1/8, 1/16, 1/32, 1/8T, 1/16T | 1/16 |
| `arp.octaves` | Octaves | 1〜4 | 1 |
| `arp.gate` | Gate Length | 10〜100% of step duration | 65% |
| `arp.swing` | Swing | 0〜50% | 0% |
| `arp.velocity` | Velocity | 25〜200% of input velocity, clamp 1..127 | 100% |
| `arp.latch` | Latch | Off / On | Off |
| `arp.restart` | Restart | Transport / Note On | Note On `[暫定]` |

- 対象はホストから来たNote On/Off。Arp ON時は**元のNote Onを直接発音させず**、内部生成Note On/Offに置換する。
- Pitch Bend / CC1 / Sustain (CC64)などは通常の演奏コントロールとして引き続き有効。
- Arp内部で使う元のNote On Velocityを保持し、発音ノートごとにVelocity係数を適用する。ドロップさせず1〜127へクランプ。

### 4.2 モードの意味

- **Up**: 低い音から高い音へ巡回。
- **Down**: 高い音から低い音へ巡回。
- **Up-Down**: 上昇後に下降。両端の音を折り返しで重複発音しない。
- **Random**: 押鍵音群から擬似乱数で選択。**同じ初期状態・MIDI・タイミングでオフライン書き出しが再現可能**な決定的乱数を使う。
- **As Played**: Note Onされた順序を保持（同音の重複は物理的なNote IDまたは発音順序で管理）。
- Octaves=1は原音域のみ、Octaves=2は+1オクターブまで、以下+3オクターブまで拡張。MIDI 0..127の範囲外はスキップ。
- 1音しか押していなくても設定Rateで再トリガーする。

### 4.3 ノートとLatch / Sustainの扱い

- Arp OFF: 従来の直接演奏。Arp ON: 生成ノートのみ発音。
- Latch OFF: 鍵盤をすべて離し、CC64でも保持されていなければ、次のスケジューラ境界を待たずに生成ノートのNote Offを発行して停止する。
- Latch ON: 鍵盤を離しても最後の音集合を維持。新しくノートを押し始めたら新しい和音入力へ置き換える（新しい押鍵群を構築してから次の適切なステップ境界で反映）。
- Sustain (CC64): Latch OFF時は物理鍵盤を離してもペダルがONなら音集合を保持。OFF時は離鍵済みノートを削除する。Latch ON時はLatch動作を優先しつつ、CC64によるノート漏れがないよう整理する。
- **All Notes Off / All Sound Off / ホストReset**: Arpの生成ノートを必ず終了させ、必要に応じLatchと入力保持状態をリセット。
- Arp ON/OFF切替、モード・Rate切替、プリセットロード、DAW停止/再開、音声デバイスのサンプルレート変更時に**取り残されたノートを発生させない**。
- 初期版はMPE非対応。複数MIDIチャンネルを受けた場合はチャンネル間のノートを混合せず、少なくとも既定チャンネルで確実に動くよう実装する。マルチチャンネルのArp処理は `[暫定]` としてテスト範囲・制約を明記する。

### 4.4 発音イベント

- Arpノートは `Note On (pitch, velocity, sampleOffset)` と `Note Off (pitch, sampleOffset)` を**オーディオバッファ内の正しいサンプル位置**で生成する。
- 1ステップあたり基本1ノート。Step長×`Gate Length` の時点でNote Off（Amp EnvelopeはそのままRelease）。
- Step長100%で次のNote OnとNote Offが同じサンプルに重なる場合は、Note Off→Note Onの順序を明確にし、同音重複でも内部Note IDにより正しいボイスを終了する。
- 発音切替のグリッチ回避のための余分な固定レイテンシーは導入しない。

---

## 5. 共通クロック・Swing・DAW同期

### 5.1 音価と時刻

- ホストから BPM、再生状態、PPQ位置（利用可能な場合）を読み取る。
- 四分音符の秒数: `60 / BPM`。音価に応じてステップ時間を計算。
- 例: BPM=120で1/16音符は125ms。1/16T（16分3連）は約83.33ms。
- Hostの再生位置が取得できる場合は**PPQ位置を音楽的な位相の正本**として扱う。バッファ開始時点だけで大まかに判定せず、ブロック内イベント位置をサンプル精度で求める。
- PPQが取得できない場合やホスト停止中に鍵盤演奏する場合は内部クロックへフォールバック（BPM取得不可なら120 BPM）。
- オフラインレンダー時に音楽位置・テンポ変化・バッファ分割が変わっても、同じ入力で同じ出力になることを目指す。

### 5.2 Swing

- Swing=0で等間隔。Swing>0で**偶数番目のサブステップ開始を遅らせ、前後2ステップを1組として総時間を維持**する。
- 数学的には2ステップ合計時間を `2T` とし、前ステップ長 `T * (1 + k)`、後ステップ長 `T * (1 - k)` とする。`k = swingPercent / 100`、0〜0.5。
- ArpとGateはそれぞれSwing値を持ち、設定が違えば異なるリズムで動作してよい。
- Swing中もStepごとのGate Lengthはそのステップの実長に対する比率。

### 5.3 再生の再開始

- `Restart=Transport`: DAWの再生位置/小節グリッドを基準にする。タイムラインのシークやループでもパターンが意図せずドリフトしない。
- `Restart=Note On`: 新しいフレーズの最初のNote Onでパターン位相を再始動。すでに鍵盤を保持中の追加Note Onでは不要に位相リセットしない。
- DAW停止時の手弾きでは内部クロックによるNote On起点のスタートを使用。
- DAWシーク時のArpは必要に応じ生成ノートを終了し、新しい位置に同期。Gateゲインは短くスムーズに遷移。
- DAW停止時のArp音の扱いは `[暫定]`: 鍵盤保持中は内部クロックで演奏継続可能、停止イベントでノート張り付きが起きないことを優先。

---

## 6. Simple / Advanced UI

### Simpleモード

- 新しい **RHYTHM** セクションを追加。
- `GATE` トグル、パターン選択、Depthノブ。
- `ARP` トグル、Mode選択、Rate選択。
- ONのときだけ最低限のコントロールを表示し、他のパラメーターはAdvancedに集約。
- Simpleの4マクロはこれまでどおり**プリセットごとに名称・割当可能**。たとえば `Gate Depth`、`Arp Gate`、`Swing`など連続値をマクロ割当先にできる。

### Advancedモード

- `Rhythm` タブ内に **Gate** と **Arpeggiator** を配置。
- Gate: 8/16/32ステップの編集グリッド、ステップ音量、Rate、Depth、Smooth、Swing、Restart、パターン保存/ロード。
- Arp: Mode、Rate、Octaves、Gate Length、Swing、Velocity、Latch、Restart。
- 現在ステップのインジケータを設けてもよい。ただし描画更新を音声スレッドで行わない。
- Gateパターンを編集する操作と、Gateを動作させる処理を完全に分離する。

### マクロ割当・オートメーション

- `gate.depth`、`gate.smooth`、`gate.swing`、`arp.gate`、`arp.swing`、`arp.velocity` はマクロに割当可能な連続値。
- `gate.enabled`、`arp.enabled`、`arp.mode`、`arp.rate`、`arp.octaves`、`arp.latch`、`gate.steps`、パターン全体は離散値なので、既存 `SPEC.md` §10 に従い初期マクロ対象外。
- トグル/セレクタをDAWから自動化可能にする場合も、状態変化をステップ境界等の適切なタイミングで適用しグリッチを防ぐ。

---

## 7. パラメーター状態・プリセット・互換性

- `gate.*` と `arp.*` の**安定したID**を `docs/PARAMETER_IDS.md` に追加。
- Gateのパターン配列（最大32、0..1 float）、パターン名、ユーザー編集状態をホスト状態・プリセットへ保存。
- Arp全設定、Latch設定も保存。ただし**現在の物理押鍵、発音中Note、PPQカウンター、演奏中のLatch保持和音は保存しない**（復元時に発音を再開しない）。
- 読み込んだプリセットにArp/Gate項目がなければ両機能OFFとして復元し、既存プリセットとの互換性を保つ。
- ファクトリープリセットの総数目標は、この追加仕様では変更しない（当初 256。2026-11-22 に別の理由で 128 へ変更、D-060）。Arp を生かした音色は既存カテゴリ内に置く。**Gate を主役にした音色は、カテゴリ `Trance Gate` にまとめる**（2026-11-21 改訂、`docs/DECISIONS.md` D-042。当初は「既存カテゴリ内で追加」としていた）。
- 状態形式は現行 `schemaVersion: 1` を拡張してよい（未公開の仕様段階の場合）。既にリリース済み形式なら新バージョンと明示的なマイグレーションを用意する。
- プリセットの切替中はArp生成ノートを終了し、Gateを安全に再初期化する。Reverb/Delay tailは既存 `SPEC.md` の方針に従う。

### 例: 保存データの概念スキーマ

```json
{
  "parameters": {
    "arp.enabled": true,
    "arp.mode": "Up",
    "arp.rate": "1/16",
    "arp.octaves": 2,
    "arp.gate": 0.65,
    "arp.swing": 0.0,
    "arp.velocity": 1.0,
    "arp.latch": false,
    "gate.enabled": true,
    "gate.steps": 16,
    "gate.rate": "1/16",
    "gate.depth": 1.0,
    "gate.smooth": 3.0,
    "gate.swing": 0.0
  },
  "rhythm": {
    "gatePatternName": "Syncopated",
    "gatePattern": [1,0,1,1,0,1,0,0,1,0,1,1,0,1,0,0]
  }
}
```

※ 既存プリセットJSONのルートに上記のフィールドを統合する例であり、単独JSONを新しい全体スキーマにする指示ではない。`gate.pattern` と `rhythm.gatePattern` を二重に保持しないこと。

---

## 8. クラス構成（推奨）

```text
src/
├── rhythm/
│   ├── RhythmClock.h/.cpp       # BPM/PPQ/再生位置/ステップ境界/Swing
│   ├── TranceGate.h/.cpp        # オーディオゲイン処理
│   ├── GatePattern.h/.cpp       # immutableな8/16/32ステップパターン
│   ├── Arpeggiator.h/.cpp       # 入力ノート追跡・生成ノート管理
│   └── ArpNoteScheduler.h/.cpp  # サンプル精度のOn/Offスケジュール
├── state/
│   └── (既存ParameterRegistry/StateSerializer等を拡張)
├── ui/
│   ├── RhythmSimpleView.h/.cpp
│   └── RhythmAdvancedView.h/.cpp
└── tests/
    ├── rhythm/
    │   ├── TranceGateTests.cpp
    │   ├── ArpeggiatorTests.cpp
    │   └── RhythmClockTests.cpp
    └── fixtures/
```

- コード上で `*.h/.cpp` 表記は説明用で、実際には別ファイルを作成する。
- 音声処理スレッド内で `std::vector` を伸長する、文字列処理、ロック、JSON解析、動的確保は禁止。
- ステップ配列は固定長、イベントキューも固定容量等のリアルタイム安全な仕組みとする。
- 32/64/128/2048サンプルなどのブロック長に依存しないステップ判定。異なる長さのバッファにまたがるNote Offを確実にスケジュールする。
- UI編集やプリセット読込時のPatternはimmutableスナップショットとして安全に交換し、遷移時のクリックを抑制する。
- `juce::AudioPlayHead::PositionInfo` 等、採用するJUCEバージョンの公式APIを確認して実装する。非対応ホストでは安全なフォールバックを使う。

---

## 9. 実装順序（既存Phaseへの追加）

`SPEC.md` v0.2のPhase 0〜7は維持し、**Phase 4（FX）完了後、Phase 5（UI/プリセット）の前に Phase 4.5 — Rhythm を追加**する。

### Phase 4.5a — 共通クロックとTrance Gate

1. BPM/PPQ/バッファ内ステップ境界/Swingを扱う `RhythmClock` をテスト駆動で実装。
2. 8/16/32ステップ、Depth、Smooth、Gate ON/OFF。
3. Delay/Reverbより前の音声経路へ統合。
4. プリセット/ホスト状態の保存復元、先行して暫定的な検証UIまたはテスト制御を用意。
5. Done: 120BPM・1/16で8ステップ=1秒、テンポ変更・再生位置シークでも周期が崩れず、クリックなし。

### Phase 4.5b — Arpeggiator

1. 押鍵、離鍵、CC64、Latchの状態機械を実装。
2. Up/Down/Up-Down/Random/As PlayedとOctavesを実装。
3. Rate、Gate Length、Swing、Velocityを内部ノートイベントに反映。
4. ArpとGateの同時使用、ON/OFF切替、再生停止、DAWオフライン処理を検証。
5. Done: 発音順序の正確さ、ノート張り付きなし、ホストタイムラインと同期。

### Phase 5への追加

- SimpleのRHYTHM、AdvancedのRhythmタブ。
- プリセット検索・保存、パターン選択、ユーザーパターンの保存、マクロ割当。

### Phase 6への追加

- 既存の音色数の目標内（128。D-060）でGate/Arpを生かした音色を制作。
- BPM 90 / 120 / 140 / 180、SuperSAW/Brass/Stabで楽曲用途を試聴。

---

## 10. 自動テストと受け入れ条件

### Gate

1. 全ステップON、Depth=100%ではオーディオ振幅が変わらない（スムージングを除く）。
2. 全ステップOFF、Depth=100%ではステップ平滑化後の振幅がゼロに到達。
3. Depth=0でパターンに関係なくオーディオが通過。
4. 8/16/32ステップでパターンが正しく一周する。
5. Step値0.5で50%音量、左右同じゲイン。
6. Rate/Swing/Patternを演奏中変更してもNaN/Inf、突発的クリックなし。
7. Delay/ReverbのtailがGateの閉じたステップ中にも残る。

### Arp

1. C4/E4/G4を押した場合、UpはC4→E4→G4、Downは逆順、Up-Downは端を二重発音しない。
2. Octaves=2ならC4/E4/G4にC5/E5/G5が追加され、127を超える音程をスキップ。
3. Gate Length=50%でStep長の半分でNote Offを発行。
4. As Playedは押鍵順で、Randomは同条件で再現可能。
5. Latch OFFで全鍵離し後にArp停止、Latch ONで継続、新しい和音で置換。
6. CC64、Velocity=0のNote On、同音重複、ノート順序が壊れない。
7. Arp ON/OFF、プリセット変更、All Notes Off、ホスト停止/シークで生成ノートが残らない。
8. ArpとGateを同時にONにしても不正なノート・無音・過大レベルが出ない。

### Host/Timing

1. BPM120・1/16のステップは125ms、1/16Tは約83.33ms（境界誤差は実装上1サンプル程度）。
2. Swing=0で完全等間隔、Swing=50で2ステップ合計時間は変化しない。
3. 44.1/48/96kHz、32/128/512/2048sample、オフラインバウンスでテンポと発音順序が整合。
4. ループ、ジャンプ、DAW再生停止、再開、テンポチェンジでドリフトしない。
5. プリセット保存/読込・DAWプロジェクト再起動後、Arp/Gate設定とGateパターンが完全復元される（押鍵状態は復元しない）。
6. 既存のArp/Gate情報が無いプリセットは双方OFFで読み込める。
7. CPU使用量・コールバック最大処理時間を記録し、極端な負荷増がない。

---

## 11. Claude Codeへの作業指示

1. `SPEC.md` と `CLAUDE.md` を読み、本書を追加仕様として扱う。
2. `SPEC.md` の §0、§2、§9、§12、§13、§15、§16、§17 を本書と矛盾しないよう更新する。元の仕様を削除せず、差分を統合する。
3. `docs/ARCHITECTURE.md`、`docs/PARAMETER_IDS.md`、`docs/PRESET_SCHEMA.md`、`docs/TEST_PLAN.md`、`docs/DECISIONS.md` を更新する。
4. 既存Phase 0〜4の機能を壊さず、Phase 4.5a（Gate）→4.5b（Arp）→Phase 5 UI統合の順で実装する。
5. 各段階でユニットテスト、AU/VST3ビルド、Cubase動作確認手順を残す。
6. 未決定項目は本書の`[暫定]`値で実装し、確定した判断を `docs/DECISIONS.md` に記録する。
7. **追加機能として勝手に汎用シーケンサーや外部MIDI出力へ拡張しないこと。**
