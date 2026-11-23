#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "state/MetaState.h"

/** ホスト state とプリセットが共有する状態シリアライズ (SPEC §11.3 / §12)。
    メッセージスレッド専用。オーディオスレッドから呼ばないこと。

    ツリー構造:
      <POLYBRISK_STATE stateVersion="1">
        <PARAMETERS> ... APVTS state ... </PARAMETERS>
        <META>
          <MACROS> <MACRO index name cc> <MAPPING target deltaMin deltaMax curve/> ... </MACRO> x4 </MACROS>
          <RHYTHM gatePatternName gatePatternEdited gatePattern="1,0,1,..."/>
        </META>
      </POLYBRISK_STATE>
*/
namespace polybrisk::state
{

inline constexpr int currentStateVersion = 1;

inline const juce::Identifier rootType { "POLYBRISK_STATE" };
inline const juce::Identifier versionProperty { "stateVersion" };
inline const juce::Identifier parametersType { "PARAMETERS" };
inline const juce::Identifier metaType { "META" };

/** 現在の全状態を 1 つのツリーにまとめる。 */
juce::ValueTree toTree (juce::AudioProcessorValueTreeState& apvts, const MetaState& meta);

/** ツリーから状態を復元する。構造が不正・未対応 version の場合は何も変更せず false を返す。
    未知のフィールドは無視し、欠落したパラメーターは既定値に、欠落した META は初期状態に戻す。 */
bool fromTree (const juce::ValueTree& tree, juce::AudioProcessorValueTreeState& apvts, MetaState& meta);

void toBinary (juce::AudioProcessorValueTreeState& apvts, const MetaState& meta, juce::MemoryBlock& destination);
bool fromBinary (const void* data, int sizeInBytes, juce::AudioProcessorValueTreeState& apvts, MetaState& meta);

} // namespace polybrisk::state
