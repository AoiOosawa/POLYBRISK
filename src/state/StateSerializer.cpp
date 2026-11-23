#include "state/StateSerializer.h"

#include "state/Parameters.h"

namespace polybrisk::state
{

namespace
{
const juce::Identifier paramType { "PARAM" };
const juce::Identifier idProperty { "id" };
const juce::Identifier valueProperty { "value" };
} // namespace

juce::ValueTree toTree (juce::AudioProcessorValueTreeState& apvts, const MetaState& meta)
{
    juce::ValueTree root { rootType };
    root.setProperty (versionProperty, currentStateVersion, nullptr);

    // APVTS 内部のツリー形式には依存せず、レジストリの ID と実値 (非正規化) で保存する
    juce::ValueTree parameters { parametersType };

    for (const auto& d : params::all())
    {
        if (const auto* raw = apvts.getRawParameterValue (d.id))
        {
            juce::ValueTree p { paramType };
            p.setProperty (idProperty, juce::String (d.id), nullptr);
            p.setProperty (valueProperty, static_cast<double> (raw->load()), nullptr);
            parameters.appendChild (p, nullptr);
        }
    }

    root.appendChild (parameters, nullptr);

    juce::ValueTree metaTree { metaType };
    metaTree.appendChild (meta.toTree(), nullptr);
    metaTree.appendChild (meta.rhythmToTree(), nullptr);
    metaTree.appendChild (meta.presetToTree(), nullptr);
    root.appendChild (metaTree, nullptr);
    return root;
}

bool fromTree (const juce::ValueTree& tree, juce::AudioProcessorValueTreeState& apvts, MetaState& meta)
{
    if (! tree.isValid() || ! tree.hasType (rootType))
        return false;

    const int version = tree.getProperty (versionProperty, 0);

    // 将来 version のマイグレーションはここに追加する。未対応 (新しすぎる / 不正) は拒否。
    if (version < 1 || version > currentStateVersion)
        return false;

    const auto parameters = tree.getChildWithName (parametersType);
    if (! parameters.isValid())
        return false;

    for (const auto& d : params::all())
    {
        auto* param = apvts.getParameter (d.id);
        if (param == nullptr)
            continue;

        float value = d.defaultValue; // 欠落したパラメーターは既定値を補完

        const auto child = parameters.getChildWithProperty (idProperty, juce::String (d.id));
        if (child.isValid())
        {
            const double stored = child.getProperty (valueProperty, static_cast<double> (d.defaultValue));
            if (std::isfinite (stored))
                value = static_cast<float> (stored);
        }

        value = juce::jlimit (d.min, d.max, value);
        param->setValueNotifyingHost (param->convertTo0to1 (value));
    }

    // META がない (古い) 状態では、マクロは初期状態に戻る
    meta.fromTree (tree.getChildWithName (metaType).getChildWithName (MetaState::macrosType));

    // RHYTHM がない状態では既定のパターンに戻る (Gate / Arp 自体は、パラメーターの既定値が OFF なので OFF になる)
    meta.rhythmFromTree (tree.getChildWithName (metaType).getChildWithName (MetaState::rhythmType));
    meta.presetFromTree (tree.getChildWithName (metaType).getChildWithName (MetaState::presetType));
    return true;
}

void toBinary (juce::AudioProcessorValueTreeState& apvts, const MetaState& meta, juce::MemoryBlock& destination)
{
    if (const auto xml = toTree (apvts, meta).createXml())
        juce::AudioProcessor::copyXmlToBinary (*xml, destination);
}

bool fromBinary (const void* data, int sizeInBytes, juce::AudioProcessorValueTreeState& apvts, MetaState& meta)
{
    if (data == nullptr || sizeInBytes <= 0)
        return false;

    const auto xml = juce::AudioProcessor::getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr)
        return false;

    return fromTree (juce::ValueTree::fromXml (*xml), apvts, meta);
}

} // namespace polybrisk::state
