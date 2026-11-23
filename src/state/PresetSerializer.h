#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "state/MetaState.h"

/** プリセット JSON (schemaVersion 1) の読み書き。形式は docs/PRESET_SCHEMA.md。
    JSON は状態ツリー (StateSerializer と同じ POLYBRISK_STATE) に変換してから適用するので、
    ホスト状態の復元とプリセットの読込は同じ経路を通る (SPEC §11.3 / §12)。メッセージスレッド専用。 */
namespace polybrisk::preset
{

inline constexpr int currentSchemaVersion = 1;

/** 現在の全状態を JSON テキストにする。info はメタデータ (ID・名前・カテゴリなど)。 */
juce::String toJson (juce::AudioProcessorValueTreeState& apvts, const MetaState& meta, const PresetInfo& info);

struct Parsed
{
    PresetInfo info;
    juce::ValueTree state;   // POLYBRISK_STATE。パラメーターは JSON にあったものだけを含む
};

/** JSON テキストを検証して解釈する。壊れたファイル・未対応の schemaVersion はエラーを返す (状態は変更しない)。
    未知のフィールドは無視する。 */
juce::Result parse (const juce::String& jsonText, Parsed& result);

/** メタデータだけを読む (ブラウザーの一覧用)。 */
juce::Result parseInfo (const juce::String& jsonText, PresetInfo& info);

/** 解釈済みのプリセットを適用する。keepPerformance が true なら、Pitch Bend Range と Vibrato の設定を保持する
    (Performance Lock。SPEC §12)。 */
bool apply (const Parsed& parsed, juce::AudioProcessorValueTreeState& apvts, MetaState& meta, bool keepPerformance);

} // namespace polybrisk::preset
