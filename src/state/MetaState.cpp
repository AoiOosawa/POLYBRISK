#include "state/MetaState.h"

#include "state/Parameters.h"

namespace polybrisk
{

const juce::Identifier MetaState::macrosType { "MACROS" };
const juce::Identifier MetaState::rhythmType { "RHYTHM" };
const juce::Identifier MetaState::presetType { "PRESET" };

namespace
{
const juce::Identifier macroType { "MACRO" };
const juce::Identifier mappingType { "MAPPING" };
const juce::Identifier indexProperty { "index" };
const juce::Identifier nameProperty { "name" };
const juce::Identifier ccProperty { "cc" };
const juce::Identifier targetProperty { "target" };
const juce::Identifier deltaMinProperty { "deltaMin" };
const juce::Identifier deltaMaxProperty { "deltaMax" };
const juce::Identifier curveProperty { "curve" };

bool validIndex (int index) noexcept { return index >= 0 && index < macro::numMacros; }

float finiteOr (double value, float fallback) noexcept
{
    return std::isfinite (value) ? static_cast<float> (value) : fallback;
}
} // namespace

MetaState::MetaState()
{
    reset();
}

void MetaState::notifyChanged()
{
    if (onChange != nullptr)
        onChange();         // 同期: オーディオスレッド用のスナップショットをすぐ作り直す

    sendChangeMessage();    // 非同期: UI の再描画
}

juce::String MetaState::defaultMacroName (int index)
{
    return "Macro " + juce::String (index + 1);
}

bool MetaState::sanitise (MacroMapping& mapping)
{
    const auto* definition = params::find (mapping.target);

    if (definition == nullptr || ! definition->macroAssignable)
        return false;

    mapping.deltaMin = juce::jlimit (-1.0f, 1.0f, mapping.deltaMin);
    mapping.deltaMax = juce::jlimit (-1.0f, 1.0f, mapping.deltaMax);

    if (static_cast<int> (mapping.curve) < 0 || static_cast<int> (mapping.curve) > static_cast<int> (macro::Curve::easeOut))
        mapping.curve = macro::Curve::linear;

    return true;
}

MacroConfig MetaState::getMacro (int index) const
{
    const juce::ScopedLock sl (lock);
    return validIndex (index) ? macros[static_cast<size_t> (index)] : MacroConfig {};
}

void MetaState::setMacroName (int index, const juce::String& name)
{
    if (! validIndex (index))
        return;

    {
        const juce::ScopedLock sl (lock);
        macros[static_cast<size_t> (index)].name = name.trim().substring (0, 32);
    }

    notifyChanged();
}

void MetaState::setMacroCc (int index, int cc)
{
    if (! validIndex (index))
        return;

    {
        const juce::ScopedLock sl (lock);
        const int value = macro::isAssignableCc (cc) ? cc : -1;

        if (value >= 0)
            for (auto& m : macros)
                if (m.cc == value)
                    m.cc = -1;

        macros[static_cast<size_t> (index)].cc = value;
    }

    notifyChanged();
}

bool MetaState::setMapping (int index, const MacroMapping& mapping)
{
    if (! validIndex (index))
        return false;

    MacroMapping clean = mapping;
    if (! sanitise (clean))
        return false;

    {
        const juce::ScopedLock sl (lock);
        auto& mappings = macros[static_cast<size_t> (index)].mappings;
        bool replaced = false;

        for (auto& existing : mappings)
            if (existing.target == clean.target)
            {
                existing = clean;
                replaced = true;
            }

        if (! replaced)
        {
            if (static_cast<int> (mappings.size()) >= macro::maxMappingsPerMacro)
                return false;

            mappings.push_back (clean);
        }
    }

    notifyChanged();
    return true;
}

void MetaState::removeMapping (int index, const juce::String& target)
{
    if (! validIndex (index))
        return;

    {
        const juce::ScopedLock sl (lock);
        auto& mappings = macros[static_cast<size_t> (index)].mappings;
        mappings.erase (std::remove_if (mappings.begin(), mappings.end(),
                                        [&target] (const MacroMapping& m) { return m.target == target; }),
                        mappings.end());
    }

    notifyChanged();
}

void MetaState::clearMacro (int index)
{
    if (! validIndex (index))
        return;

    {
        const juce::ScopedLock sl (lock);
        macros[static_cast<size_t> (index)] = { defaultMacroName (index), -1, {} };
    }

    notifyChanged();
}

void MetaState::reset()
{
    {
        const juce::ScopedLock sl (lock);
        const auto& factory = factoryGatePatterns[static_cast<size_t> (defaultFactoryGatePattern)];
        gatePattern = makeGatePattern (factory);
        gatePatternName = factory.name;
        gatePatternEdited = false;
        presetInfo = {};
        presetInfo.name = "Init";
        dirty = false;

        for (int i = 0; i < macro::numMacros; ++i)
            macros[static_cast<size_t> (i)] = { defaultMacroName (i), -1, {} };
    }

    notifyChanged();
}

macro::Snapshot MetaState::buildSnapshot() const
{
    const juce::ScopedLock sl (lock);
    macro::Snapshot snapshot;

    for (int i = 0; i < macro::numMacros; ++i)
    {
        const auto& config = macros[static_cast<size_t> (i)];
        snapshot.cc[static_cast<size_t> (i)] = config.cc;

        for (const auto& mapping : config.mappings)
        {
            const int parameterIndex = params::indexOf (mapping.target);

            if (parameterIndex < 0 || snapshot.numEntries >= macro::maxEntries)
                continue;

            snapshot.entries[static_cast<size_t> (snapshot.numEntries++)] =
                { parameterIndex, i, mapping.deltaMin, mapping.deltaMax, mapping.curve };
        }
    }

    return snapshot;
}

juce::ValueTree MetaState::toTree() const
{
    const juce::ScopedLock sl (lock);
    juce::ValueTree tree { macrosType };

    for (int i = 0; i < macro::numMacros; ++i)
    {
        const auto& config = macros[static_cast<size_t> (i)];
        juce::ValueTree m { macroType };
        m.setProperty (indexProperty, i + 1, nullptr);
        m.setProperty (nameProperty, config.name, nullptr);
        m.setProperty (ccProperty, config.cc, nullptr);

        for (const auto& mapping : config.mappings)
        {
            juce::ValueTree child { mappingType };
            child.setProperty (targetProperty, mapping.target, nullptr);
            child.setProperty (deltaMinProperty, static_cast<double> (mapping.deltaMin), nullptr);
            child.setProperty (deltaMaxProperty, static_cast<double> (mapping.deltaMax), nullptr);
            child.setProperty (curveProperty, static_cast<int> (mapping.curve), nullptr);
            m.appendChild (child, nullptr);
        }

        tree.appendChild (m, nullptr);
    }

    return tree;
}

void MetaState::fromTree (const juce::ValueTree& tree)
{
    std::array<MacroConfig, macro::numMacros> loaded;

    for (int i = 0; i < macro::numMacros; ++i)
        loaded[static_cast<size_t> (i)] = { defaultMacroName (i), -1, {} };

    if (tree.isValid() && tree.hasType (macrosType))
    {
        for (const auto& m : tree)
        {
            const int index = static_cast<int> (m.getProperty (indexProperty, 0)) - 1;

            if (! m.hasType (macroType) || ! validIndex (index))
                continue;

            auto& config = loaded[static_cast<size_t> (index)];
            const auto name = m.getProperty (nameProperty, defaultMacroName (index)).toString().trim().substring (0, 32);
            config.name = name.isNotEmpty() ? name : defaultMacroName (index);

            const int cc = m.getProperty (ccProperty, -1);
            config.cc = macro::isAssignableCc (cc) ? cc : -1;
            config.mappings.clear();

            for (const auto& child : m)
            {
                if (! child.hasType (mappingType) || static_cast<int> (config.mappings.size()) >= macro::maxMappingsPerMacro)
                    continue;

                MacroMapping mapping;
                mapping.target = child.getProperty (targetProperty).toString();
                mapping.deltaMin = finiteOr (child.getProperty (deltaMinProperty, 0.0), 0.0f);
                mapping.deltaMax = finiteOr (child.getProperty (deltaMaxProperty, 0.0), 0.0f);
                mapping.curve = static_cast<macro::Curve> (static_cast<int> (child.getProperty (curveProperty, 0)));

                bool duplicate = false;
                for (const auto& existing : config.mappings)
                    duplicate = duplicate || existing.target == mapping.target;

                if (! duplicate && sanitise (mapping))
                    config.mappings.push_back (mapping);
            }
        }

        // 同じ CC が複数のマクロに付いていたら、先のものだけを残す
        for (size_t i = 0; i < loaded.size(); ++i)
            for (size_t j = i + 1; j < loaded.size(); ++j)
                if (loaded[i].cc >= 0 && loaded[j].cc == loaded[i].cc)
                    loaded[j].cc = -1;
    }

    {
        const juce::ScopedLock sl (lock);
        macros = loaded;
    }

    notifyChanged();
}

// ---------------------------------------------------------------------------
// Trance Gate のパターン
// ---------------------------------------------------------------------------
GatePattern MetaState::getGatePattern() const
{
    const juce::ScopedLock sl (lock);
    return gatePattern;
}

juce::String MetaState::getGatePatternName() const
{
    const juce::ScopedLock sl (lock);
    return gatePatternName;
}

bool MetaState::isGatePatternEdited() const
{
    const juce::ScopedLock sl (lock);
    return gatePatternEdited;
}

void MetaState::setGatePattern (const GatePattern& pattern, const juce::String& name, bool edited)
{
    {
        const juce::ScopedLock sl (lock);

        const auto tidy = [] (float v) { return std::isfinite (v) ? juce::jlimit (0.0f, 1.0f, v) : 1.0f; };

        for (size_t i = 0; i < gatePattern.values.size(); ++i)
        {
            gatePattern.values[i] = tidy (pattern.values[i]);
            gatePattern.right[i] = tidy (pattern.right[i]);
        }

        gatePatternName = name.trim().substring (0, 48);
        gatePatternEdited = edited;
    }

    notifyChanged();
}

void MetaState::setGateStep (int step, float value, int lane)
{
    if (step < 0 || step >= maxGateSteps || ! std::isfinite (value))
        return;

    {
        const juce::ScopedLock sl (lock);
        const float limited = juce::jlimit (0.0f, 1.0f, value);

        if (lane != 1)
            gatePattern.values[static_cast<size_t> (step)] = limited;
        if (lane != 0)
            gatePattern.right[static_cast<size_t> (step)] = limited;

        gatePatternEdited = true;
    }

    notifyChanged();
}

void MetaState::setFactoryGatePattern (int factoryIndex)
{
    if (factoryIndex < 0 || factoryIndex >= static_cast<int> (factoryGatePatterns.size()))
        return;

    const auto& factory = factoryGatePatterns[static_cast<size_t> (factoryIndex)];
    setGatePattern (makeGatePattern (factory), factory.name, false);
}

juce::ValueTree MetaState::rhythmToTree() const
{
    const juce::ScopedLock sl (lock);
    juce::ValueTree tree { rhythmType };
    tree.setProperty ("gatePatternName", gatePatternName, nullptr);
    tree.setProperty ("gatePatternEdited", gatePatternEdited, nullptr);

    const auto join = [] (const std::array<float, maxGateSteps>& lane)
    {
        juce::StringArray values;
        for (const float v : lane)
            values.add (juce::String (static_cast<double> (v), 4));
        return values.joinIntoString (",");
    };

    tree.setProperty ("gatePattern", join (gatePattern.values), nullptr);

    // 右チャンネルは、左と違うときだけ保存する (ない場合は左と同じ)
    if (gatePattern.isStereo())
        tree.setProperty ("gatePatternRight", join (gatePattern.right), nullptr);

    return tree;
}

void MetaState::rhythmFromTree (const juce::ValueTree& tree)
{
    const auto& factory = factoryGatePatterns[static_cast<size_t> (defaultFactoryGatePattern)];
    GatePattern pattern = makeGatePattern (factory);
    juce::String name = factory.name;
    bool edited = false;

    if (tree.isValid() && tree.hasType (rhythmType) && tree.hasProperty ("gatePattern"))
    {
        const auto parse = [] (const juce::String& text, std::array<float, maxGateSteps>& lane)
        {
            juce::StringArray tokens;
            tokens.addTokens (text, ",", "");

            for (int i = 0; i < maxGateSteps; ++i)
            {
                float value = 1.0f;     // 不足分・数値でない要素は「開」

                if (i < tokens.size())
                {
                    const auto token = tokens[i].trim();
                    if (token.isNotEmpty() && token.containsOnly ("0123456789.-+eE"))
                    {
                        const double parsed = token.getDoubleValue();
                        if (std::isfinite (parsed))
                            value = juce::jlimit (0.0f, 1.0f, static_cast<float> (parsed));
                    }
                }

                lane[static_cast<size_t> (i)] = value;
            }
        };

        parse (tree.getProperty ("gatePattern").toString(), pattern.values);

        if (tree.hasProperty ("gatePatternRight"))
            parse (tree.getProperty ("gatePatternRight").toString(), pattern.right);
        else
            pattern.right = pattern.values;

        name = tree.getProperty ("gatePatternName", "User").toString();
        edited = tree.getProperty ("gatePatternEdited", false);
    }

    setGatePattern (pattern, name, edited);
}

// ---------------------------------------------------------------------------
// プリセット情報 / Dirty / UI モード
// ---------------------------------------------------------------------------
PresetInfo MetaState::getPresetInfo() const
{
    const juce::ScopedLock sl (lock);
    return presetInfo;
}

void MetaState::setPresetInfo (const PresetInfo& info)
{
    {
        const juce::ScopedLock sl (lock);
        presetInfo = info;
    }

    sendChangeMessage();
}

bool MetaState::isDirty() const
{
    const juce::ScopedLock sl (lock);
    return dirty;
}

void MetaState::setDirty (bool shouldBeDirty)
{
    {
        const juce::ScopedLock sl (lock);

        if (dirty == shouldBeDirty)
            return;

        dirty = shouldBeDirty;
    }

    sendChangeMessage();
}

bool MetaState::isAdvancedView() const
{
    const juce::ScopedLock sl (lock);
    return advancedView;
}

void MetaState::setAdvancedView (bool advanced)
{
    {
        const juce::ScopedLock sl (lock);
        advancedView = advanced;
    }

    sendChangeMessage();
}

juce::ValueTree MetaState::presetToTree() const
{
    const juce::ScopedLock sl (lock);
    juce::ValueTree tree { presetType };
    tree.setProperty ("id", presetInfo.id, nullptr);
    tree.setProperty ("name", presetInfo.name, nullptr);
    tree.setProperty ("category", presetInfo.category, nullptr);
    tree.setProperty ("author", presetInfo.author, nullptr);
    tree.setProperty ("description", presetInfo.description, nullptr);
    tree.setProperty ("tags", presetInfo.tags.joinIntoString ("|"), nullptr);
    tree.setProperty ("factory", presetInfo.factory, nullptr);
    tree.setProperty ("dirty", dirty, nullptr);
    tree.setProperty ("advancedView", advancedView, nullptr);
    return tree;
}

void MetaState::presetFromTree (const juce::ValueTree& tree)
{
    PresetInfo info;
    info.name = "Init";
    bool wasDirty = false;
    bool advanced = false;

    {
        const juce::ScopedLock sl (lock);
        advanced = advancedView;    // 項目がなければ、いまの表示のままにする
    }

    if (tree.isValid() && tree.hasType (presetType))
    {
        info.id = tree.getProperty ("id").toString().substring (0, 64);
        info.name = tree.getProperty ("name", "Init").toString().substring (0, 64);
        info.category = tree.getProperty ("category").toString().substring (0, 64);
        info.author = tree.getProperty ("author").toString().substring (0, 64);
        info.description = tree.getProperty ("description").toString().substring (0, 512);
        // プリセットファイルと同じ上限 (16 個、各 32 文字) をホストの状態にも掛ける
        juce::StringArray tokens;
        tokens.addTokens (tree.getProperty ("tags").toString().substring (0, 16 * 33), "|", "");

        for (const auto& token : tokens)
            if (token.trim().isNotEmpty() && info.tags.size() < 16)
                info.tags.addIfNotAlreadyThere (token.trim().substring (0, 32));
        info.factory = tree.getProperty ("factory", false);
        wasDirty = tree.getProperty ("dirty", false);
        advanced = tree.getProperty ("advancedView", advanced);
    }

    {
        const juce::ScopedLock sl (lock);
        presetInfo = info;
        dirty = wasDirty;
        advancedView = advanced;
    }

    sendChangeMessage();
}

} // namespace polybrisk
