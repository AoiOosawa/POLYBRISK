#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "rhythm/GatePattern.h"
#include "state/MacroEngine.h"

#include <array>
#include <functional>
#include <vector>

namespace polybrisk
{

/** 現在の音色がどのプリセットに由来するか (SPEC §11.2)。 */
struct PresetInfo
{
    juce::String id;            // UUID など。空なら「プリセットなし (Init)」
    juce::String name;
    juce::String category;
    juce::String author;
    juce::String description;
    juce::StringArray tags;
    bool factory = false;
};

struct MacroMapping
{
    juce::String target;        // Parameter ID (連続値のみ)
    float deltaMin = 0.0f;      // -1..+1 (正規化オフセット)
    float deltaMax = 0.0f;
    macro::Curve curve = macro::Curve::linear;
};

struct MacroConfig
{
    juce::String name;
    int cc = -1;
    std::vector<MacroMapping> mappings;
};

/** ホストパラメーターにしない状態 (SPEC §12): マクロ名・割当・CC。
    スレッドセーフ (内部でロック)。オーディオスレッドからは使わず、buildSnapshot() の結果だけを渡す。
    変更は onChange (同期) と ChangeBroadcaster (非同期、UI 用) で通知する。 */
class MetaState final : public juce::ChangeBroadcaster
{
public:
    MetaState();

    MacroConfig getMacro (int index) const;
    void setMacroName (int index, const juce::String& name);
    void setMacroCc (int index, int cc);   // 同じ CC を持つ他のマクロからは外す。-1 で解除

    /** 割当を追加する。対象が連続値でない・上限超過なら false。同じ対象が既にあれば上書きする。 */
    bool setMapping (int index, const MacroMapping& mapping);
    void removeMapping (int index, const juce::String& target);
    void clearMacro (int index);

    /** すべて初期状態 (名前 "Macro N"、割当なし、CC なし) に戻す。 */
    void reset();

    macro::Snapshot buildSnapshot() const;

    // --- Trance Gate のパターン (ホストパラメーターにはしない。追加仕様 §3.1 / §7) ---
    GatePattern getGatePattern() const;
    juce::String getGatePatternName() const;
    bool isGatePatternEdited() const;

    /** パターン全体を差し替える (ファクトリーパターンの選択・プリセット読込)。 */
    void setGatePattern (const GatePattern& pattern, const juce::String& name, bool edited);

    /** 1 ステップだけ変える (UI の編集)。「編集済み」になる。 */
    /** lane: 0 = 左、1 = 右、-1 = 両方 (Stereo が OFF のときの編集)。 */
    void setGateStep (int step, float value, int lane = -1);

    void setFactoryGatePattern (int factoryIndex);

    juce::ValueTree rhythmToTree() const;

    /** 項目がなければ既定のパターンに戻す。長さの不足は 1.0 (開) で補い、範囲外はクランプする。 */
    void rhythmFromTree (const juce::ValueTree& tree);

    static const juce::Identifier rhythmType;

    // --- プリセット情報 / Dirty / UI モード ---
    PresetInfo getPresetInfo() const;
    void setPresetInfo (const PresetInfo& info);
    bool isDirty() const;
    void setDirty (bool shouldBeDirty);
    bool isAdvancedView() const;
    void setAdvancedView (bool advanced);

    juce::ValueTree presetToTree() const;
    void presetFromTree (const juce::ValueTree& tree);

    static const juce::Identifier presetType;

    juce::ValueTree toTree() const;

    /** 不正な項目は読み飛ばし、欠落は初期状態で補う。 */
    void fromTree (const juce::ValueTree& tree);

    /** 変更のたびに、変更したスレッドで同期的に呼ばれる (プロセッサーがスナップショットを作り直す)。 */
    std::function<void()> onChange;

    static juce::String defaultMacroName (int index);
    static const juce::Identifier macrosType;

private:
    static bool sanitise (MacroMapping& mapping);
    void notifyChanged();

    mutable juce::CriticalSection lock;
    std::array<MacroConfig, macro::numMacros> macros;
    GatePattern gatePattern;
    juce::String gatePatternName;
    bool gatePatternEdited = false;
    PresetInfo presetInfo;
    bool dirty = false;
    bool advancedView = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MetaState)
};

} // namespace polybrisk
