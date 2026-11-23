# POLYBRISK — Preset Schema（schemaVersion 1）

プリセットは JSON ファイル。Factory と User で同じ形式を使う（SPEC §11.3）。
読み書きは `src/state/PresetSerializer.*`、一覧・保存・お気に入りは `src/state/PresetManager.*`。
内容の検査は `tests/state/PresetTests.cpp`。

## 置き場所

| 種類 | 場所 | 備考 |
|---|---|---|
| Factory | `presets/factory/*.json` をバイナリに組み込み（`juce_add_binary_data`） | 読み取り専用。定義の元は `tools/make_factory_presets.py` |
| User | `~/Library/Application Support/POLYBRISK/Presets/User/*.json` | Save As / Import で作成 |
| ユーザー設定 | `~/Library/Application Support/POLYBRISK/settings.json` | お気に入り（プリセット ID の一覧）と Performance Lock。**プリセットファイルには含めない** |

## 互換性のルール

- `schemaVersion` は必須。1 未満・現在より新しい値・数値でないものは**読み込みを拒否**する（現在の音色は変えない）。
- `parameters` は必須（オブジェクト）。それ以外の項目は省略できる。
- **未知のフィールドは無視**する。**JSON にないパラメーターは既定値**になる（疎なファイルでよい）。
- 範囲外の値はクランプ、数値として読めない値はそのパラメーターの既定値。
- 存在しない Parameter ID、離散値への割当など、不正なマクロ割当は読み飛ばす。
- 未公開の間は、項目の追加で `schemaVersion` を上げない。公開後に形式を変えるときは新しい番号とマイグレーションを用意する。
- 1 MB を超えるファイルは読まない。
- 読込は JSON を状態ツリー（`POLYBRISK_STATE`）に変換してから `StateSerializer` で適用する。**ホスト状態の復元と同じ経路**を通る。

## ルートの構成

```json
{
  "schemaVersion": 1,
  "id": "0d6f2a2e-…",
  "name": "Bright Pop Stab",
  "category": "Stab / Chord Hit",
  "tags": ["Bright", "Tight", "Punchy"],
  "author": "POLYBRISK",
  "description": "Snappy chord stab: punch filter, fast envelope and a noise + pitch transient.",
  "parameters": {
    "osc.a.wave": "SuperSAW",
    "osc.a.unisonVoices": 5,
    "osc.b.enabled": true,
    "filter.character": "Punch",
    "filter.cutoff": 700,
    "amp.decay": 260
  },
  "macros": [
    { "index": 1, "name": "Punch", "cc": null,
      "mappings": [
        { "target": "filter.envAmount", "deltaMin": -0.25, "deltaMax": 0.3, "curve": "linear" },
        { "target": "transient.amount", "deltaMin": -0.1, "deltaMax": 0.25, "curve": "linear" }
      ] }
  ],
  "rhythm": {
    "gatePatternName": "Syncopated",
    "gatePatternEdited": false,
    "gatePattern": [1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0]
  }
}
```

| 項目 | 型 | 内容 |
|---|---|---|
| `id` | 文字列 | プリセットの識別子（UUID）。お気に入りと「現在のプリセット」の判定に使う。空、または一覧の別の音色と重なっている場合は、読込側がファイル名から作る（`user:<ファイル名>`）。Import は、重なる ID を新しい UUID に付け替えてからコピーする（D-068） |
| `name` / `category` / `author` / `description` | 文字列 | 64 文字まで（説明は 512 文字まで）。カテゴリは SPEC §11.1 の 10 個が標準 |
| `tags` | 文字列の配列 | 16 個まで。検索の対象 |
| `parameters` | オブジェクト | キーは `docs/PARAMETER_IDS.md` の Parameter ID |
| `macros` | 配列 | マクロ 1〜4 の名前・CC・割当 |
| `rhythm` | オブジェクト | Trance Gate のパターン |

## parameters の値

| 種別 | 書き出し | 読み込みで受け付けるもの |
|---|---|---|
| 連続値 | 実値（%、ms、Hz、dB、cent、s）。有効 6 桁 | 数値、数値の文字列 |
| 整数 | 整数 | 同上 |
| ON/OFF | `true` / `false` | 真偽値、0 / 1 |
| 選択肢 | **選択肢の名前**（例 `"SuperSAW"`、`"1/8 D"`） | 名前（大文字小文字を区別しない）、インデックス |

- 割合は % の実値（`65`）。SPEC §11.4 や追加仕様 §7 の例にある 0..1 表記（`0.65`）は使わない（D-023）。
- `gate.steps` は名前 `"8"` / `"16"` / `"32"`。
- マクロの現在値は `parameters` の `macro.1`〜`macro.4`（%、50 がニュートラル）。
- **`output.gain` は書かない**。全体の音量はユーザーのもので、プリセットを読んでも変えない（入っていても無視する）。マクロの割当先にもできない（`macros[].mappings[].target` に書いてあっても捨てる。D-068）。音色ごとの音量は `preset.level`（dB）に書く（D-055）。

## macros

- `index`: 1〜4。配列にないマクロは初期状態（名前 `Macro N`、割当なし）。
- `name`: 32 文字まで。
- `cc`: MIDI Learn で割り当てた CC 番号（2〜119、64 を除く）。なければ `null`。
- `mappings`: 最大 8。`target`（連続値パラメーターの ID）、`deltaMin` / `deltaMax`（−1〜+1。正規化した範囲に対するオフセット。マクロ 0 % / 100 % のとき）、`curve`（`"linear"` / `"easeIn"` / `"easeOut"`）。
- 概念スキーマ（SPEC §11.4）にある `"value"`（0..1）も読める。`parameters` に `macro.N` があればそちらを優先する。

## rhythm

- **Gate パターンはここだけに保存する**（`parameters` に `gate.pattern` を置かない）。
- `gatePattern`: 0..1 の数値を 32 個まで。不足は 1.0（開）で補い、範囲外はクランプ、数値でない要素は 1.0。先頭の `gate.steps` 個が使われる。
- `gatePatternRight`（省略可）: 右チャンネル用のパターン。形式は `gatePattern` と同じ。**ない場合は左（`gatePattern`）と同じ**になる。左右が同じパターンのときは書き出さない。`gate.stereo` が ON のときだけ使われる（D-040）。
- `gatePatternName`: 表示用の名前。`gatePatternEdited`: ファクトリーパターンから編集したか。
- `rhythm` がないプリセットは既定のパターン（Shifted Long。D-044）になり、`gate.rate` がなければ既定値の 1/32 になる。`gate.*` / `arp.*` がなければ、既定値により **Gate・Arp とも OFF**。

## 保存しないもの

現在の物理押鍵、発音中のノート、クロックの位置、Latch で保持中の和音、MIDI Learn の待機状態、表示モード（Simple / Advanced はホスト状態にだけ保存する）。

## ホスト状態との関係

ホスト状態（`getStateInformation`）は同じ内容に加えて、現在のプリセット情報（ID・名前・カテゴリ・タグ・説明・Factory かどうか）、Dirty フラグ、表示モードを持つ。プリセットファイルがない環境でも、DAW プロジェクトだけで音色が再現される（テストで確認）。

## Factory プリセットの作り方

1. `tools/make_factory_presets.py` に、音色ごとの設定を人が書く（乱数や数値ずらしによる生成は行わない）。
2. `python3 tools/make_factory_presets.py` で `presets/factory/*.json` を書き出す。
3. `python3 tools/level_presets.py` で音量を実測し、`preset.level` だけを調整する（結果は `tools/preset_levels.json`）。
4. `scripts/test.sh` で検査する（ID・範囲・選択肢名・マクロ・重複・レンダリング結果）。
5. `scripts/render_presets.sh` で試聴用の WAV を書き出して聴く。
