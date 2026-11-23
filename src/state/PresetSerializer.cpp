#include "state/PresetSerializer.h"

#include "state/Parameters.h"
#include "state/StateSerializer.h"

#include <cmath>

namespace polybrisk::preset
{

namespace
{
const char* curveName (macro::Curve curve)
{
    switch (curve)
    {
        case macro::Curve::easeIn:  return "easeIn";
        case macro::Curve::easeOut: return "easeOut";
        case macro::Curve::linear:
        default:                    return "linear";
    }
}

/** JSON の数値を int にする。範囲外・非有限の double をそのままキャストすると未定義動作なので、先に確かめる。 */
int toInt (const juce::var& v, int fallback)
{
    if (! (v.isInt() || v.isInt64() || v.isDouble() || v.isBool()))
        return fallback;

    const double d = static_cast<double> (v);
    return std::isfinite (d) && d >= -1.0e9 && d <= 1.0e9 ? static_cast<int> (d) : fallback;
}

int curveFromVar (const juce::var& v)
{
    if (v.isString())
    {
        const auto name = v.toString();
        if (name.equalsIgnoreCase ("easeIn"))  return static_cast<int> (macro::Curve::easeIn);
        if (name.equalsIgnoreCase ("easeOut")) return static_cast<int> (macro::Curve::easeOut);
        return static_cast<int> (macro::Curve::linear);
    }

    return toInt (v, static_cast<int> (macro::Curve::linear));
}

/** float 由来の端数 (12000.000977 など) を出さないよう、有効 6 桁に丸める。 */
double roundSignificant (double value)
{
    if (value == 0.0 || ! std::isfinite (value))
        return 0.0;

    const double scale = std::pow (10.0, 5.0 - std::floor (std::log10 (std::abs (value))));
    return std::round (value * scale) / scale;
}

juce::String tidy (const juce::var& v, int maxLength)
{
    return v.isString() ? v.toString().trim().substring (0, maxLength) : juce::String();
}

bool readInfo (const juce::var& root, PresetInfo& info)
{
    info = {};
    info.id = tidy (root["id"], 64);
    info.name = tidy (root["name"], 64);
    info.category = tidy (root["category"], 64);
    info.author = tidy (root["author"], 64);
    info.description = tidy (root["description"], 512);

    if (const auto* tags = root["tags"].getArray())
        for (const auto& tag : *tags)
            if (tag.isString() && tag.toString().trim().isNotEmpty() && info.tags.size() < 16)
                info.tags.addIfNotAlreadyThere (tag.toString().removeCharacters ("|").trim().substring (0, 32));   // "|" はホストの状態での区切り

    if (info.name.isEmpty())
        info.name = "Untitled";

    return true;
}

juce::Result checkRoot (const juce::String& jsonText, juce::var& root)
{
    const auto result = juce::JSON::parse (jsonText, root);

    if (result.failed())
        return juce::Result::fail ("Not valid JSON: " + result.getErrorMessage());

    if (root.getDynamicObject() == nullptr)
        return juce::Result::fail ("The preset is not a JSON object");

    const auto version = root["schemaVersion"];

    if (! (version.isInt() || version.isInt64() || version.isDouble()))
        return juce::Result::fail ("schemaVersion is missing");

    const int v = toInt (version, 0);

    if (std::abs (static_cast<double> (version) - static_cast<double> (v)) > 1.0e-9)
        return juce::Result::fail ("schemaVersion is not an integer");

    if (v < 1 || v > currentSchemaVersion)
        return juce::Result::fail ("Unsupported schemaVersion: " + juce::String (v));

    return juce::Result::ok();
}

/** JSON の値をパラメーターの実値に変換する。解釈できなければ false (そのパラメーターは既定値になる)。 */
bool toRealValue (const params::Definition& d, const juce::var& value, double& real)
{
    if (value.isBool())
    {
        real = static_cast<bool> (value) ? 1.0 : 0.0;
        return true;
    }

    if (value.isString())
    {
        const auto text = value.toString().trim();

        // 選択肢は名前で書ける (人が読め、選択肢の追加にも強い)
        if (d.kind == params::Kind::choice)
            for (size_t i = 0; i < d.choices.size(); ++i)
                if (text.equalsIgnoreCase (d.choices[i]))
                {
                    real = static_cast<double> (i);
                    return true;
                }

        if (text.isNotEmpty() && text.containsOnly ("0123456789.-+eE"))
        {
            real = text.getDoubleValue();
            return std::isfinite (real);
        }

        return false;
    }

    if (value.isInt() || value.isInt64() || value.isDouble())
    {
        real = static_cast<double> (value);
        return std::isfinite (real);
    }

    return false;
}
} // namespace

juce::String toJson (juce::AudioProcessorValueTreeState& apvts, const MetaState& meta, const PresetInfo& info)
{
    auto* root = new juce::DynamicObject();
    juce::var rootVar (root);

    root->setProperty ("schemaVersion", currentSchemaVersion);
    root->setProperty ("id", info.id);
    root->setProperty ("name", info.name);
    root->setProperty ("category", info.category);

    juce::Array<juce::var> tags;
    for (const auto& tag : info.tags)
        tags.add (tag);
    root->setProperty ("tags", tags);
    root->setProperty ("author", info.author);
    root->setProperty ("description", info.description);

    // parameters: キーは Parameter ID。連続値・整数は実値、ON/OFF は true/false、選択肢は名前
    auto* parameters = new juce::DynamicObject();

    for (const auto& d : params::all())
    {
        const auto* raw = apvts.getRawParameterValue (d.id);
        if (raw == nullptr || juce::String (d.id) == params::id::outputGain)   // Output Gain は音色に含めない (D-055)
            continue;

        const float value = raw->load();

        switch (d.kind)
        {
            case params::Kind::toggle:
                parameters->setProperty (d.id, value >= 0.5f);
                break;

            case params::Kind::choice:
            {
                const auto choice = static_cast<size_t> (juce::jlimit (0, static_cast<int> (d.choices.size()) - 1, juce::roundToInt (value)));
                parameters->setProperty (d.id, juce::String (d.choices[choice]));
                break;
            }

            case params::Kind::integer:
                parameters->setProperty (d.id, juce::roundToInt (value));
                break;

            case params::Kind::continuous:
            default:
                parameters->setProperty (d.id, roundSignificant (static_cast<double> (value)));
                break;
        }
    }

    root->setProperty ("parameters", juce::var (parameters));

    // macros: 現在値は parameters の macro.N に入っているので、ここには名前・CC・割当だけを書く
    juce::Array<juce::var> macros;

    for (int i = 0; i < macro::numMacros; ++i)
    {
        const auto config = meta.getMacro (i);
        auto* m = new juce::DynamicObject();
        m->setProperty ("index", i + 1);
        m->setProperty ("name", config.name);
        m->setProperty ("cc", config.cc >= 0 ? juce::var (config.cc) : juce::var());

        juce::Array<juce::var> mappings;
        for (const auto& mapping : config.mappings)
        {
            auto* entry = new juce::DynamicObject();
            entry->setProperty ("target", mapping.target);
            entry->setProperty ("deltaMin", juce::String (static_cast<double> (mapping.deltaMin), 4).getDoubleValue());
            entry->setProperty ("deltaMax", juce::String (static_cast<double> (mapping.deltaMax), 4).getDoubleValue());
            entry->setProperty ("curve", curveName (mapping.curve));
            mappings.add (juce::var (entry));
        }

        m->setProperty ("mappings", mappings);
        macros.add (juce::var (m));
    }

    root->setProperty ("macros", macros);

    // rhythm: Gate パターンはここだけに保存する (parameters には置かない)
    auto* rhythm = new juce::DynamicObject();
    rhythm->setProperty ("gatePatternName", meta.getGatePatternName());
    rhythm->setProperty ("gatePatternEdited", meta.isGatePatternEdited());

    const auto gatePattern = meta.getGatePattern();
    const auto toArray = [] (const std::array<float, maxGateSteps>& lane)
    {
        juce::Array<juce::var> values;
        for (const float v : lane)
            values.add (juce::String (static_cast<double> (v), 4).getDoubleValue());
        return values;
    };

    rhythm->setProperty ("gatePattern", toArray (gatePattern.values));

    // 右チャンネルは、左と違うときだけ書く (ない場合は左と同じ)
    if (gatePattern.isStereo())
        rhythm->setProperty ("gatePatternRight", toArray (gatePattern.right));
    root->setProperty ("rhythm", juce::var (rhythm));

    return juce::JSON::toString (rootVar);
}

juce::Result parseInfo (const juce::String& jsonText, PresetInfo& info)
{
    juce::var root;
    const auto result = checkRoot (jsonText, root);

    if (result.failed())
        return result;

    readInfo (root, info);
    return juce::Result::ok();
}

juce::Result parse (const juce::String& jsonText, Parsed& result)
{
    juce::var root;
    const auto check = checkRoot (jsonText, root);

    if (check.failed())
        return check;

    const auto* parameters = root["parameters"].getDynamicObject();

    if (parameters == nullptr)
        return juce::Result::fail ("parameters is missing");

    readInfo (root, result.info);

    // --- 状態ツリーへ変換する (以降は StateSerializer と同じ規則: 欠落 → 既定値、範囲外 → クランプ) ---
    juce::ValueTree tree { state::rootType };
    tree.setProperty (state::versionProperty, state::currentStateVersion, nullptr);

    juce::ValueTree parameterTree { state::parametersType };
    const auto& properties = parameters->getProperties();

    for (const auto& d : params::all())
    {
        const juce::Identifier key { d.id };
        double real = 0.0;

        if (properties.contains (key) && toRealValue (d, properties[key], real))
        {
            juce::ValueTree p { "PARAM" };
            p.setProperty ("id", juce::String (d.id), nullptr);
            p.setProperty ("value", real, nullptr);
            parameterTree.appendChild (p, nullptr);
        }
    }

    tree.appendChild (parameterTree, nullptr);

    juce::ValueTree metaTree { state::metaType };
    juce::ValueTree macrosTree { MetaState::macrosType };

    if (const auto* macros = root["macros"].getArray())
    {
        for (const auto& m : *macros)
        {
            if (m.getDynamicObject() == nullptr)
                continue;

            const int index = toInt (m["index"], 0);
            juce::ValueTree macroTree { "MACRO" };
            macroTree.setProperty ("index", index, nullptr);

            if (m["name"].isString())
                macroTree.setProperty ("name", m["name"].toString(), nullptr);

            macroTree.setProperty ("cc", toInt (m["cc"], -1), nullptr);

            if (const auto* mappings = m["mappings"].getArray())
            {
                for (const auto& mapping : *mappings)
                {
                    if (mapping.getDynamicObject() == nullptr)
                        continue;

                    juce::ValueTree child { "MAPPING" };
                    child.setProperty ("target", mapping["target"].toString(), nullptr);
                    child.setProperty ("deltaMin", static_cast<double> (mapping["deltaMin"]), nullptr);
                    child.setProperty ("deltaMax", static_cast<double> (mapping["deltaMax"]), nullptr);
                    child.setProperty ("curve", curveFromVar (mapping["curve"]), nullptr);
                    macroTree.appendChild (child, nullptr);
                }
            }

            macrosTree.appendChild (macroTree, nullptr);

            // 概念スキーマ (SPEC §11.4) の "value" (0..1) も受け付ける。parameters の macro.N があればそちらを優先
            if (index >= 1 && index <= macro::numMacros && (m["value"].isDouble() || m["value"].isInt()))
            {
                const juce::String macroId = params::id::macros[index - 1];

                if (! parameterTree.getChildWithProperty ("id", macroId).isValid())
                {
                    juce::ValueTree p { "PARAM" };
                    p.setProperty ("id", macroId, nullptr);
                    p.setProperty ("value", static_cast<double> (m["value"]) * 100.0, nullptr);
                    parameterTree.appendChild (p, nullptr);
                }
            }
        }
    }

    metaTree.appendChild (macrosTree, nullptr);

    if (root["rhythm"].getDynamicObject() != nullptr)
    {
        const auto rhythm = root["rhythm"];

        if (const auto* pattern = rhythm["gatePattern"].getArray())
        {
            const auto join = [] (const juce::Array<juce::var>& array)
            {
                juce::StringArray values;
                for (const auto& v : array)
                    values.add ((v.isDouble() || v.isInt() || v.isInt64() || v.isBool()) ? juce::String (static_cast<double> (v), 4) : juce::String ("x"));
                return values.joinIntoString (",");
            };

            juce::ValueTree rhythmTree { MetaState::rhythmType };
            rhythmTree.setProperty ("gatePattern", join (*pattern), nullptr);

            if (const auto* rightLane = rhythm["gatePatternRight"].getArray())
                rhythmTree.setProperty ("gatePatternRight", join (*rightLane), nullptr);

            rhythmTree.setProperty ("gatePatternName", tidy (rhythm["gatePatternName"], 48).isNotEmpty() ? tidy (rhythm["gatePatternName"], 48) : juce::String ("User"), nullptr);
            rhythmTree.setProperty ("gatePatternEdited", static_cast<bool> (rhythm["gatePatternEdited"]), nullptr);
            metaTree.appendChild (rhythmTree, nullptr);
        }
    }

    tree.appendChild (metaTree, nullptr);
    result.state = tree;
    return juce::Result::ok();
}

bool apply (const Parsed& parsed, juce::AudioProcessorValueTreeState& apvts, MetaState& meta, bool keepPerformance)
{
    auto tree = parsed.state.createCopy();

    // Output Gain はユーザーが決める全体の音量なので、プリセットを読んでも必ずいまの値を保持する (D-055)。
    // Performance Lock が ON のときは、Pitch Bend Range と Vibrato の設定も保持する
    std::vector<const char*> keptIds { params::id::outputGain };

    if (keepPerformance)
        keptIds.insert (keptIds.end(), { params::id::bendUp, params::id::bendDown, params::id::bendMode,
                                         params::id::vibratoDepth, params::id::vibratoRate, params::id::vibratoDelay });

    {
        auto parameterTree = tree.getChildWithName (state::parametersType);

        for (const auto* lockedId : keptIds)
        {
            auto existing = parameterTree.getChildWithProperty ("id", juce::String (lockedId));

            if (! existing.isValid())
            {
                existing = juce::ValueTree { "PARAM" };
                existing.setProperty ("id", juce::String (lockedId), nullptr);
                parameterTree.appendChild (existing, nullptr);
            }

            if (const auto* raw = apvts.getRawParameterValue (lockedId))
                existing.setProperty ("value", static_cast<double> (raw->load()), nullptr);
        }
    }

    const bool advanced = meta.isAdvancedView();   // 表示モードはプリセットで変えない

    if (! state::fromTree (tree, apvts, meta))
        return false;

    meta.setPresetInfo (parsed.info);
    meta.setAdvancedView (advanced);
    meta.setDirty (false);
    return true;
}

} // namespace polybrisk::preset
