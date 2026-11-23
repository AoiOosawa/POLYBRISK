#include "PluginProcessor.h"
#include "TestHelpers.h"
#include "state/Parameters.h"
#include "state/PresetManager.h"
#include "state/PresetSerializer.h"

#include <set>

namespace
{
namespace ids = polybrisk::params::id;
namespace preset = polybrisk::preset;
using polybrisk::PresetManager;
using polybrisk::test::TimedMidi;

/** テストごとの一時フォルダー (ユーザーの実際のプリセットフォルダーには触れない)。 */
struct TempRoot
{
    TempRoot() : folder (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("polybrisk_test_" + juce::Uuid().toString()))
    {
        folder.createDirectory();
    }

    ~TempRoot() { folder.deleteRecursively(); }

    juce::File folder;
};

void setParam (PolyBriskAudioProcessor& processor, const char* parameterId, float realValue)
{
    auto* param = processor.getValueTreeState().getParameter (parameterId);
    param->setValueNotifyingHost (param->convertTo0to1 (realValue));
}

float getParam (PolyBriskAudioProcessor& processor, const char* parameterId)
{
    return processor.getValueTreeState().getRawParameterValue (parameterId)->load();
}

int indexOfPreset (PresetManager& manager, const juce::String& name)
{
    for (int i = 0; i < manager.getNumPresets(); ++i)
        if (manager.getEntry (i).info.name == name)
            return i;

    return -1;
}

juce::String jsonOf (PolyBriskAudioProcessor& processor, const juce::String& name = "Test")
{
    polybrisk::PresetInfo info;
    info.id = "test-id";
    info.name = name;
    info.category = "SuperSAW";
    info.tags = { "Bright", "Wide" };
    info.description = "A test preset";
    return preset::toJson (processor.getValueTreeState(), processor.getMetaState(), info);
}

bool applyJson (PolyBriskAudioProcessor& processor, const juce::String& json, bool keepPerformance = false)
{
    preset::Parsed parsed;
    return preset::parse (json, parsed).wasOk()
        && preset::apply (parsed, processor.getValueTreeState(), processor.getMetaState(), keepPerformance);
}

class PresetTests final : public juce::UnitTest
{
public:
    PresetTests() : juce::UnitTest ("Presets", "state") {}

    void runTest() override
    {
        TempRoot temp;

        beginTest ("JSON roundtrip restores every parameter, the macros and the gate pattern");
        {
            PolyBriskAudioProcessor source (temp.folder);
            int index = 0;
            for (const auto& d : polybrisk::params::all())
            {
                auto* param = source.getValueTreeState().getParameter (d.id);
                param->setValueNotifyingHost (0.12f + 0.76f * static_cast<float> ((index * 41) % 100) / 100.0f);
                ++index;
            }

            auto& meta = source.getMetaState();
            meta.setMacroName (0, "Punch");
            meta.setMacroCc (1, 74);
            meta.setMapping (0, { ids::filterEnvAmount, -0.25f, 0.3f, polybrisk::macro::Curve::linear });
            meta.setMapping (0, { ids::transientAmount, -0.1f, 0.25f, polybrisk::macro::Curve::easeOut });
            meta.setMapping (3, { ids::gateDepth, -0.5f, 0.0f, polybrisk::macro::Curve::easeIn });
            meta.setFactoryGatePattern (3);
            meta.setGateStep (5, 0.4f);

            const auto json = jsonOf (source);

            PolyBriskAudioProcessor restored (temp.folder);
            expect (applyJson (restored, json));

            for (const auto& d : polybrisk::params::all())
            {
                // Output Gain はプリセットに含めない (D-055)。読んだ側の値 (ここでは既定の 0 dB) のまま
                const float expected = juce::String (d.id) == ids::outputGain ? 0.0f : getParam (source, d.id);
                expectWithinAbsoluteError (getParam (restored, d.id), expected, std::abs (expected) * 2.0e-5f + 1.0e-4f, d.id);
            }

            for (int m = 0; m < 4; ++m)
            {
                const auto a = meta.getMacro (m);
                const auto b = restored.getMetaState().getMacro (m);
                expectEquals (b.name, a.name);
                expectEquals (b.cc, a.cc);
                expectEquals (static_cast<int> (b.mappings.size()), static_cast<int> (a.mappings.size()));

                for (size_t i = 0; i < std::min (a.mappings.size(), b.mappings.size()); ++i)
                {
                    expectEquals (b.mappings[i].target, a.mappings[i].target);
                    expectWithinAbsoluteError (b.mappings[i].deltaMin, a.mappings[i].deltaMin, 1.0e-4f);
                    expectWithinAbsoluteError (b.mappings[i].deltaMax, a.mappings[i].deltaMax, 1.0e-4f);
                    expect (b.mappings[i].curve == a.mappings[i].curve);
                }
            }

            expect (restored.getMetaState().getGatePattern() == meta.getGatePattern());
            expectEquals (restored.getMetaState().getGatePatternName(), juce::String ("Syncopated"));
            expect (restored.getMetaState().isGatePatternEdited());

            const auto info = restored.getMetaState().getPresetInfo();
            expectEquals (info.name, juce::String ("Test"));
            expectEquals (info.category, juce::String ("SuperSAW"));
            expectEquals (info.tags.joinIntoString (","), juce::String ("Bright,Wide"));
            expect (! restored.getMetaState().isDirty());

            // もう一度書き出して読み直しても、値は変わらない (世代を重ねてもずれていかない)
            PolyBriskAudioProcessor second (temp.folder);
            expect (applyJson (second, jsonOf (restored)));
            for (const auto& d : polybrisk::params::all())
                expectWithinAbsoluteError (getParam (second, d.id), getParam (restored, d.id),
                                           std::abs (getParam (restored, d.id)) * 2.0e-5f + 1.0e-4f, d.id);
        }

        beginTest ("JSON is human readable: choices by name, toggles as booleans, gate pattern only under rhythm");
        {
            PolyBriskAudioProcessor processor (temp.folder);
            setParam (processor, ids::oscA.wave, 1.0f);
            setParam (processor, ids::filterCharacter, 2.0f);
            const auto json = jsonOf (processor);

            juce::var root;
            expect (juce::JSON::parse (json, root).wasOk());
            expectEquals (static_cast<int> (root["schemaVersion"]), 1);
            expectEquals (root["parameters"]["osc.a.wave"].toString(), juce::String ("SuperSAW"));
            expectEquals (root["parameters"]["filter.character"].toString(), juce::String ("Punch"));
            expect (root["parameters"]["osc.a.enabled"].isBool());
            expect (static_cast<bool> (root["parameters"]["osc.a.enabled"]));
            expectWithinAbsoluteError (static_cast<double> (root["parameters"]["amp.attack"]), 2.0, 1.0e-6);
            expect (root["parameters"]["gate.pattern"].isVoid() || root["parameters"]["gate.pattern"].isUndefined());
            expectEquals (root["rhythm"]["gatePattern"].getArray()->size(), 32);
            expectEquals (root["macros"].getArray()->size(), 4);
        }

        beginTest ("Broken or unsupported files are rejected and leave the current sound untouched");
        {
            PolyBriskAudioProcessor processor (temp.folder);
            setParam (processor, ids::filterCutoff, 1234.0f);
            const auto good = jsonOf (processor);

            const juce::String bad[] = {
                {},
                "not json at all",
                "[1, 2, 3]",
                "{ \"parameters\": {} }",                                           // schemaVersion なし
                "{ \"schemaVersion\": 2, \"parameters\": {} }",                     // 新しすぎる
                "{ \"schemaVersion\": 0, \"parameters\": {} }",
                "{ \"schemaVersion\": \"1\", \"parameters\": {} }",
                "{ \"schemaVersion\": 1 }",                                         // parameters なし
                "{ \"schemaVersion\": 1, \"parameters\": [1, 2] }",
                good.substring (0, good.length() / 2),                              // 途中で切れている
            };

            for (const auto& json : bad)
            {
                preset::Parsed parsed;
                expect (preset::parse (json, parsed).failed());
                expect (! applyJson (processor, json));
                expectWithinAbsoluteError (getParam (processor, ids::filterCutoff), 1234.0f, 0.01f);
            }
        }

        beginTest ("Lenient reading: unknown fields ignored, missing -> default, out of range clamped, choice by index, bad values -> default");
        {
            PolyBriskAudioProcessor processor (temp.folder);
            setParam (processor, ids::ampRelease, 5000.0f);
            setParam (processor, ids::gateEnabled, 1.0f);
            setParam (processor, ids::arpEnabled, 1.0f);
            processor.getMetaState().setMapping (0, { ids::filterCutoff, -0.1f, 0.1f, polybrisk::macro::Curve::linear });

            const juce::String json = R"({
                "schemaVersion": 1, "name": "Lenient", "futureField": { "x": 1 },
                "parameters": {
                    "filter.cutoff": 99999999, "filter.resonance": -5, "osc.a.wave": 3, "osc.b.wave": "square",
                    "filter.mode": "No Such Mode", "amp.attack": "NaN", "amp.decay": "12.5", "osc.b.enabled": true,
                    "removed.parameter": 1, "transient.enabled": 1
                },
                "macros": [ { "index": 2, "name": "Width", "value": 0.8,
                              "mappings": [ { "target": "osc.a.spread", "deltaMin": -0.2, "deltaMax": 0.3, "curve": "easeOut" },
                                            { "target": "osc.a.wave", "deltaMin": 0, "deltaMax": 1 },
                                            { "target": "nope" } ] },
                            { "index": 9, "name": "Ignored" }, "garbage" ]
            })";

            expect (applyJson (processor, json));
            expectEquals (getParam (processor, ids::filterCutoff), 20000.0f);
            expectEquals (getParam (processor, ids::filterResonance), 0.0f);
            expectEquals (getParam (processor, ids::oscA.wave), 3.0f);
            expectEquals (getParam (processor, ids::oscB.wave), 3.0f);
            expectEquals (getParam (processor, ids::filterMode), 1.0f);        // 既定 (LP24)
            expectWithinAbsoluteError (getParam (processor, ids::ampAttack), 2.0f, 1.0e-4f);
            expectWithinAbsoluteError (getParam (processor, ids::ampDecay), 12.5f, 1.0e-3f);
            expectEquals (getParam (processor, ids::oscB.enabled), 1.0f);
            expectEquals (getParam (processor, ids::transientEnabled), 1.0f);
            expectWithinAbsoluteError (getParam (processor, ids::ampRelease), 200.0f, 1.0e-3f);   // JSON にない → 既定値

            // Gate / Arp の項目がないプリセットは、両方 OFF で読み込まれる
            expectEquals (getParam (processor, ids::gateEnabled), 0.0f);
            expectEquals (getParam (processor, ids::arpEnabled), 0.0f);
            expectEquals (processor.getMetaState().getGatePatternName(), juce::String ("Shifted Long"));

            // マクロ: 不正な割当は読み飛ばし、"value" (0..1) も受け付ける。JSON にないマクロは初期状態
            const auto width = processor.getMetaState().getMacro (1);
            expectEquals (width.name, juce::String ("Width"));
            expectEquals (static_cast<int> (width.mappings.size()), 1);
            expect (width.mappings[0].curve == polybrisk::macro::Curve::easeOut);
            expectWithinAbsoluteError (getParam (processor, ids::macros[1]), 80.0f, 1.0e-3f);
            expectEquals (static_cast<int> (processor.getMetaState().getMacro (0).mappings.size()), 0);
            expectEquals (processor.getMetaState().getMacro (0).name, juce::String ("Macro 1"));
        }

        beginTest ("Factory presets: every file is valid, complete and distinct");
        {
            const int count = PresetManager::getNumEmbeddedPresets();
            expect (count >= 64, "only " + juce::String (count) + " factory presets");

            std::set<juce::String> names, presetIds;
            std::map<juce::String, int> perCategory;

            for (int i = 0; i < count; ++i)
            {
                const auto file = PresetManager::getEmbeddedPresetFileName (i);
                const auto json = PresetManager::getEmbeddedPresetJson (i);
                juce::var root;
                expect (juce::JSON::parse (json, root).wasOk(), file);

                preset::Parsed parsed;
                expect (preset::parse (json, parsed).wasOk(), file);

                // メタデータ (SPEC §11.2)
                expect (parsed.info.id.isNotEmpty(), file + ": id");
                expect (parsed.info.name.isNotEmpty() && parsed.info.name != "Untitled", file + ": name");
                expect (parsed.info.description.length() >= 20, file + ": description");
                expect (parsed.info.tags.size() >= 2, file + ": tags");
                expect (PresetManager::standardCategories().contains (parsed.info.category), file + ": category " + parsed.info.category);
                expect (names.insert (parsed.info.name).second, file + ": duplicate name");
                expect (presetIds.insert (parsed.info.id).second, file + ": duplicate id");
                ++perCategory[parsed.info.category];

                // parameters: 登録済みの ID だけを使い、値は範囲内。選択肢は正しい名前
                if (const auto* parameters = root["parameters"].getDynamicObject())
                {
                    expect (parameters->getProperties().size() >= 12, file + ": too few parameters to be a designed sound");

                    for (const auto& property : parameters->getProperties())
                    {
                        const auto id = property.name.toString();
                        const auto* d = polybrisk::params::find (id);
                        expect (d != nullptr, file + ": unknown parameter " + id);

                        if (d == nullptr)
                            continue;

                        if (property.value.isString())
                        {
                            bool known = false;
                            for (const auto* choiceName : d->choices)
                                known = known || property.value.toString() == choiceName;
                            expect (known, file + ": " + id + " has unknown choice " + property.value.toString());
                        }
                        else if (! property.value.isBool())
                        {
                            const auto value = static_cast<double> (property.value);
                            expect (value >= d->min && value <= d->max, file + ": " + id + " out of range");
                        }
                    }
                }

                // 4 マクロすべてに名前と有効な割当がある
                const auto* macros = root["macros"].getArray();
                expect (macros != nullptr && macros->size() == 4, file + ": macros");

                if (macros != nullptr)
                {
                    std::set<juce::String> macroNames;

                    for (const auto& m : *macros)
                    {
                        expect (m["name"].toString().isNotEmpty() && ! m["name"].toString().startsWith ("Macro "), file + ": macro name");
                        expect (macroNames.insert (m["name"].toString()).second, file + ": duplicate macro name");
                        const auto* mappings = m["mappings"].getArray();
                        expect (mappings != nullptr && mappings->size() >= 1 && mappings->size() <= 8, file + ": mappings");

                        if (mappings != nullptr)
                            for (const auto& mapping : *mappings)
                            {
                                const auto* d = polybrisk::params::find (mapping["target"].toString());
                                expect (d != nullptr && d->macroAssignable, file + ": macro target " + mapping["target"].toString());
                                const double lo = mapping["deltaMin"], hi = mapping["deltaMax"];
                                expect (lo >= -1.0 && lo <= 1.0 && hi >= -1.0 && hi <= 1.0, file + ": delta range");
                                expect (std::abs (lo) + std::abs (hi) > 0.01, file + ": mapping does nothing");
                            }
                    }
                }
            }

            juce::String summary;
            for (const auto& [category, number] : perCategory)
                summary << category << " " << number << ", ";
            logMessage ("  " + juce::String (count) + " factory presets: " + summary.dropLastCharacters (2));
        }

        beginTest ("Factory presets: each one loads, sounds, stays below full scale and no two sound the same");
        {
            PolyBriskAudioProcessor probe (temp.folder);
            const int count = probe.getPresetManager().getNumFactoryPresets();
            std::vector<juce::AudioBuffer<float>> renders;

            for (int i = 0; i < count; ++i)
            {
                PolyBriskAudioProcessor processor (temp.folder);
                auto& manager = processor.getPresetManager();
                expect (manager.load (i).wasOk());
                const auto name = manager.getEntry (i).info.name;
                expectEquals (processor.getMetaState().getPresetInfo().name, name);
                expect (processor.getMetaState().getPresetInfo().factory);
                expect (! processor.getMetaState().isDirty());

                // Bass カテゴリは単音で弾く前提で音量をそろえているので、低い 1 音で鳴らす (D-054)。ほかは 5 声の和音
                const bool bass = manager.getEntry (i).info.category == "Bass";
                const std::vector<int> notes = bass ? std::vector<int> { 36 } : std::vector<int> { 48, 55, 60, 64, 67 };

                std::vector<TimedMidi> events;
                for (const int note : notes)
                {
                    events.push_back ({ 0, juce::MidiMessage::noteOn (1, note, 0.8f) });
                    events.push_back ({ 36000, juce::MidiMessage::noteOff (1, note) });
                }

                auto out = polybrisk::test::renderProcessor (processor, 48000.0, { 256 }, 72000, events, 128.0);
                const float peak = polybrisk::test::peakLevel (out);
                expect (polybrisk::test::allFinite (out), name);
                expect (peak > 0.03f, name + " is nearly silent");
                expect (peak < 0.98f, name + " hits the limiter: " + juce::String (peak));
                expect (std::abs (polybrisk::test::dcOffset (out.getReadPointer (0), 36000)) < 0.02, name);
                renders.push_back (std::move (out));
            }

            // 水増しの検出: どの 2 つを取っても、波形として十分に違う
            for (size_t a = 0; a < renders.size(); ++a)
                for (size_t b = a + 1; b < renders.size(); ++b)
                {
                    double difference = 0.0, energy = 0.0;
                    for (int s = 0; s < 36000; ++s)
                    {
                        const double x = renders[a].getSample (0, s), y = renders[b].getSample (0, s);
                        difference += (x - y) * (x - y);
                        energy += x * x + y * y;
                    }
                    expect (difference / std::max (energy, 1.0e-12) > 0.05,
                            "presets " + juce::String (static_cast<int> (a) + 1) + " and " + juce::String (static_cast<int> (b) + 1) + " sound almost identical");
                }
        }

        beginTest ("Factory presets: the library follows the planned category split (SPEC 11.1, D-042, D-054)");
        {
            PolyBriskAudioProcessor processor (temp.folder);
            auto& manager = processor.getPresetManager();
            const auto countIn = [&] (const char* category) { return static_cast<int> (manager.filter ({}, category, false).size()); };

            expectEquals (manager.getNumFactoryPresets(), 128);
            expectEquals (countIn ("SuperSAW"), 18);
            expectEquals (countIn ("Synth Brass"), 18);
            expectEquals (countIn ("Stab / Chord Hit"), 18);
            expectEquals (countIn ("Bright Poly"), 12);
            expectEquals (countIn ("Synth Pluck"), 10);
            expectEquals (countIn ("Lead / Hook"), 12);
            expectEquals (countIn ("Bass"), 8);
            expectEquals (countIn ("Pad / Strings"), 10);
            expectEquals (countIn ("Octave / Layered"), 4);
            expectEquals (countIn ("Trance Gate"), 18);

            // Factory はカテゴリ内で収録順に並ぶ (D-043)。Trance Gate の先頭は代表の音色
            expect (manager.getEntry (manager.filter ({}, "Trance Gate", false).front()).info.name == "Supersaw Gate");
            expect (manager.getEntry (manager.filter ({}, "SuperSAW", false).front()).info.name == "Anthem Saw");

            // タグでの絞り込み: 指定したタグをすべて持つ音色だけが残る。一覧は使用数の多い順
            const auto tags = manager.getTags();
            expect (tags.size() >= 10);
            expect (tags.contains ("Gate") && tags.contains ("Bright"));

            int previousCount = std::numeric_limits<int>::max();
            for (const auto& tag : tags)
            {
                const auto withTag = manager.filter ({}, {}, false, { tag });
                expect (! withTag.empty(), tag);
                expect (static_cast<int> (withTag.size()) <= previousCount, tag);
                previousCount = static_cast<int> (withTag.size());

                for (const int i : withTag)
                    expect (manager.getEntry (i).info.tags.contains (tag, true), tag);
            }

            // Factory のタグは整理した語彙に収まる (D-047): 24 種類、どれも 3 音色以上、先頭は明るさのタグ
            expectEquals (tags.size(), 24);
            for (const auto& tag : tags)
                expect (manager.filter ({}, {}, false, { tag }).size() >= 3, tag);

            for (const int i : manager.filter ({}, {}, false))
            {
                const auto& info = manager.getEntry (i).info;
                expect (juce::StringArray ({ "Bright", "Warm", "Dark" }).contains (info.tags[0]), info.name);
            }

            const auto gate = manager.filter ({}, {}, false, { "Gate" });
            const auto gateStereo = manager.filter ({}, {}, false, { "gate", "STEREO" });   // 大文字小文字は区別しない
            expectEquals (static_cast<int> (gate.size()), 18);
            expect (! gateStereo.empty() && gateStereo.size() < gate.size());
            for (const int i : gateStereo)
                expect (manager.getEntry (i).info.tags.contains ("Gate") && manager.getEntry (i).info.tags.contains ("Stereo"));

            expect (manager.filter ({}, "SuperSAW", false, { "Gate" }).empty());            // カテゴリと組み合わせる
            expect (manager.filter ({}, {}, false, { "No Such Tag" }).empty());

            // Trance Gate カテゴリの音色は、すべて Gate が ON
            for (const int i : manager.filter ({}, "Trance Gate", false))
            {
                expect (manager.load (i).wasOk());
                expect (processor.getValueTreeState().getRawParameterValue ("gate.enabled")->load() >= 0.5f, manager.getEntry (i).info.name);
            }
        }

        beginTest ("Gate / Arp presets stay in time and well-behaved at 90 / 120 / 140 / 180 BPM");
        {
            PolyBriskAudioProcessor probe (temp.folder);
            std::vector<int> rhythmPresets;
            for (int i = 0; i < probe.getPresetManager().getNumFactoryPresets(); ++i)
            {
                expect (probe.getPresetManager().load (i).wasOk());
                if (getParam (probe, ids::gateEnabled) >= 0.5f || getParam (probe, ids::arpEnabled) >= 0.5f)
                    rhythmPresets.push_back (i);
            }

            expect (rhythmPresets.size() >= 6, "only " + juce::String (static_cast<int> (rhythmPresets.size())) + " presets use Gate / Arp");

            for (const int index : rhythmPresets)
            {
                for (const double bpm : { 90.0, 120.0, 140.0, 180.0 })
                {
                    PolyBriskAudioProcessor processor (temp.folder);
                    expect (processor.getPresetManager().load (index).wasOk());
                    const auto name = processor.getPresetManager().getEntry (index).info.name + " @ " + juce::String (bpm);

                    // 2 小節押さえて離す
                    const int hold = static_cast<int> (8.0 * 60.0 / bpm * 48000.0);
                    std::vector<TimedMidi> events;
                    for (const int note : { 57, 60, 64 })
                    {
                        events.push_back ({ 0, juce::MidiMessage::noteOn (1, note, 0.8f) });
                        events.push_back ({ hold, juce::MidiMessage::noteOff (1, note) });
                    }

                    const auto out = polybrisk::test::renderProcessor (processor, 48000.0, { 256 }, hold + 24000, events, bpm);
                    expect (polybrisk::test::allFinite (out), name);
                    expect (polybrisk::test::peakLevel (out) < 0.98f, name);

                    // 1 拍ごとのレベルを見る: どの拍にも音があり (刻みが止まらない)、2 小節目は 1 小節目と同じ形で繰り返す
                    const int beat = static_cast<int> (60.0 / bpm * 48000.0);
                    for (int b = 0; b < 8; ++b)
                        expect (polybrisk::test::peakLevel (out, b * beat, beat) > 0.01f, name + " beat " + juce::String (b));
                }
            }
        }

        beginTest ("Changing presets while notes are held stays finite and below full scale");
        {
            PolyBriskAudioProcessor processor (temp.folder);
            auto& manager = processor.getPresetManager();
            processor.setRateAndBufferSizeDetails (48000.0, 256);
            processor.prepareToPlay (48000.0, 256);
            juce::AudioBuffer<float> buffer (2, 256);
            float worst = 0.0f;
            bool finite = true;

            for (int i = 0; i < manager.getNumFactoryPresets(); ++i)
            {
                expect (manager.load (i).wasOk());

                for (int block = 0; block < 30; ++block)
                {
                    juce::MidiBuffer midi;
                    if (block == 0)
                        for (const int note : { 48, 55, 60, 64, 67, 72 })
                            midi.addEvent (juce::MidiMessage::noteOn (1, note, 1.0f), 0);   // 離さずに次のプリセットへ進む (スティールも起きる)

                    processor.processBlock (buffer, midi);

                    for (int ch = 0; ch < 2; ++ch)
                        for (int s = 0; s < 256; ++s)
                        {
                            const float x = buffer.getSample (ch, s);
                            finite = finite && std::isfinite (x);
                            worst = std::max (worst, std::abs (x));
                        }
                }
            }

            processor.releaseResources();
            expect (finite);
            expect (worst <= 1.0f, "peak " + juce::String (worst));
        }

        beginTest ("Preset manager: listing, search, category filter, next / previous");
        {
            PolyBriskAudioProcessor processor (temp.folder);
            auto& manager = processor.getPresetManager();
            const int factory = manager.getNumFactoryPresets();
            expect (factory >= 24);
            expectEquals (manager.getNumPresets(), factory);
            expectEquals (manager.getCurrentIndex(), -1);

            expect (manager.getCategories().contains ("SuperSAW"));
            expect (manager.filter ({}, {}, false).size() == static_cast<size_t> (factory));

            for (const int i : manager.filter ({}, "Synth Brass", false))
                expectEquals (manager.getEntry (i).info.category, juce::String ("Synth Brass"));
            expect (manager.filter ({}, "Synth Brass", false).size() >= 3);

            const auto found = manager.filter ("  anthem SAW ", {}, false);     // 大文字小文字・空白を無視、語の AND
            expectEquals (static_cast<int> (found.size()), 1);
            expect (manager.filter ("zzzz-no-such-preset", {}, false).empty());
            expect (! manager.filter ("arp", {}, false).empty());               // タグ・説明も検索対象
            expect (manager.filter ({}, {}, true).empty());                     // お気に入りはまだない

            expect (manager.loadAdjacent (1).wasOk());
            expectEquals (manager.getCurrentIndex(), 0);
            expect (manager.loadAdjacent (-1).wasOk());
            expectEquals (manager.getCurrentIndex(), factory - 1);              // 端では反対側へ回る
            expect (manager.loadAdjacent (1).wasOk());
            expectEquals (manager.getCurrentIndex(), 0);
            expect (manager.load (-1).failed());
            expect (manager.load (9999).failed());
        }

        beginTest ("User presets: save, reload, overwrite, rename, delete; Factory presets are read-only");
        {
            TempRoot root;
            PolyBriskAudioProcessor processor (root.folder);
            auto& manager = processor.getPresetManager();
            const int factory = manager.getNumFactoryPresets();

            expect (manager.load (0).wasOk());
            setParam (processor, ids::filterCutoff, 3333.0f);
            processor.getMetaState().setMacroName (0, "Mine");
            processor.syncFromAudioThread();
            expect (processor.getMetaState().isDirty());        // 編集したら Dirty

            expect (manager.saveUser ("", "SuperSAW", {}, {}).failed());
            expect (manager.saveUser ("My Saw / Test:1", "SuperSAW", { "Bright" }, "Edited copy").wasOk());
            expect (! processor.getMetaState().isDirty());
            expectEquals (manager.getNumPresets(), factory + 1);
            expect (manager.getUserPresetFolder().getNumberOfChildFiles (juce::File::findFiles, "*.json") == 1);

            const int userIndex = manager.getCurrentIndex();
            expect (userIndex >= factory);
            expect (! manager.getEntry (userIndex).info.factory);
            const auto userId = manager.getEntry (userIndex).info.id;

            // 別の音色に切り替えてから読み直すと、保存した内容に戻る
            expect (manager.load (3).wasOk());
            expect (std::abs (getParam (processor, ids::filterCutoff) - 3333.0f) > 1.0f);
            expect (manager.load (userIndex).wasOk());
            expectWithinAbsoluteError (getParam (processor, ids::filterCutoff), 3333.0f, 0.5f);
            expectEquals (processor.getMetaState().getMacro (0).name, juce::String ("Mine"));
            expectEquals (processor.getMetaState().getPresetInfo().description, juce::String ("Edited copy"));

            // 同名で保存 → 上書き (件数は増えず、ID は変わらない)
            setParam (processor, ids::filterCutoff, 4444.0f);
            expect (manager.saveUser ("My Saw / Test:1", "SuperSAW", { "Bright" }, "Edited copy").wasOk());
            expectEquals (manager.getNumPresets(), factory + 1);
            expectEquals (manager.getEntry (manager.getCurrentIndex()).info.id, userId);

            // 新しいインスタンスからも見える
            {
                PolyBriskAudioProcessor other (root.folder);
                expectEquals (other.getPresetManager().getNumPresets(), factory + 1);
                expect (other.getPresetManager().load (other.getPresetManager().getNumPresets() - 1).wasOk());
                expectWithinAbsoluteError (getParam (other, ids::filterCutoff), 4444.0f, 0.5f);
            }

            expect (manager.renameUser (manager.getCurrentIndex(), "Renamed Saw").wasOk());
            expectEquals (processor.getMetaState().getPresetInfo().name, juce::String ("Renamed Saw"));
            expectEquals (static_cast<int> (manager.filter ("renamed", {}, false).size()), 1);
            expect (manager.renameUser (0, "Nope").failed());       // Factory は変更不可
            expect (manager.deleteUser (0).failed());

            expect (manager.deleteUser (manager.getCurrentIndex()).wasOk());
            expectEquals (manager.getNumPresets(), factory);
            expect (manager.getUserPresetFolder().getNumberOfChildFiles (juce::File::findFiles, "*.json") == 0);
        }

        beginTest ("Import / export, and broken files in the user folder are skipped");
        {
            TempRoot root;
            PolyBriskAudioProcessor processor (root.folder);
            auto& manager = processor.getPresetManager();
            const int factory = manager.getNumFactoryPresets();

            expect (manager.load (6).wasOk());
            setParam (processor, ids::filterResonance, 77.0f);
            const auto exported = root.folder.getChildFile ("exported.json");
            expect (manager.exportCurrent (exported).wasOk());
            expect (exported.existsAsFile());

            expect (manager.importFile (exported).wasOk());
            expectEquals (manager.getNumPresets(), factory + 1);
            expect (manager.load (manager.getNumPresets() - 1).wasOk());
            expectWithinAbsoluteError (getParam (processor, ids::filterResonance), 77.0f, 0.01f);

            const auto broken = root.folder.getChildFile ("broken.json");
            broken.replaceWithText ("{ this is not json");
            expect (manager.importFile (broken).failed());
            expect (manager.importFile (root.folder.getChildFile ("missing.json")).failed());
            expectEquals (manager.getNumPresets(), factory + 1);

            // User フォルダーに壊れたファイルが紛れ込んでも、一覧から外れるだけで落ちない
            manager.getUserPresetFolder().getChildFile ("junk.json").replaceWithText ("\x01\x02 garbage");
            manager.getUserPresetFolder().getChildFile ("future.json").replaceWithText ("{ \"schemaVersion\": 99, \"parameters\": {} }");
            manager.rescan();
            expectEquals (manager.getNumPresets(), factory + 1);
        }

        beginTest ("Save overwrites the loaded file, ids stay unique, and names cannot collide or leave the folder (D-068)");
        {
            TempRoot root;
            PolyBriskAudioProcessor processor (root.folder);
            auto& manager = processor.getPresetManager();
            const int factory = manager.getNumFactoryPresets();
            const auto folder = manager.getUserPresetFolder();
            const auto userFiles = [&] { return folder.getNumberOfChildFiles (juce::File::findFiles, "*.json"); };

            const auto indexOfFile = [&] (const juce::String& fileName)
            {
                for (int i = 0; i < manager.getNumPresets(); ++i)
                    if (manager.getEntry (i).file.getFileName() == fileName)
                        return i;
                return -1;
            };

            const auto idsAreUnique = [&]
            {
                juce::StringArray seen;
                for (int i = 0; i < manager.getNumPresets(); ++i)
                {
                    if (seen.contains (manager.getEntry (i).info.id))
                        return false;
                    seen.add (manager.getEntry (i).info.id);
                }
                return true;
            };

            // --- 同じ名前・同じ ID のファイルが 2 つある (手でコピーした場合)。Save は読み込んだ方だけを書き換える ---
            expect (manager.load (0).wasOk());
            setParam (processor, ids::filterCutoff, 1111.0f);
            expect (manager.saveUser ("Lead", "Lead / Hook", {}, {}).wasOk());
            expect (folder.getChildFile ("Lead.json").copyFileTo (folder.getChildFile ("copy of lead.json")));
            manager.rescan();
            expectEquals (manager.getNumPresets(), factory + 2);
            expect (idsAreUnique());

            const int copy = indexOfFile ("copy of lead.json");
            expect (copy >= 0);
            expect (manager.load (copy).wasOk());
            expectEquals (manager.getCurrentIndex(), copy);
            setParam (processor, ids::filterCutoff, 5555.0f);
            expect (manager.saveCurrent().wasOk());
            expectEquals (userFiles(), 2);
            expectEquals (manager.getEntry (manager.getCurrentIndex()).file.getFileName(), juce::String ("copy of lead.json"));

            expect (manager.load (indexOfFile ("Lead.json")).wasOk());
            expectWithinAbsoluteError (getParam (processor, ids::filterCutoff), 1111.0f, 0.5f);     // 元のファイルは変わっていない
            expect (manager.load (indexOfFile ("copy of lead.json")).wasOk());
            expectWithinAbsoluteError (getParam (processor, ids::filterCutoff), 5555.0f, 0.5f);

            // Factory の音色と Init は Save では上書きできない
            expect (manager.load (0).wasOk());
            expect (manager.saveCurrent().failed());
            expect (manager.loadInit().wasOk());
            expect (manager.saveCurrent().failed());
            expectEquals (userFiles(), 2);

            // --- 同じファイルを 2 回取り込んでも、Factory を書き出して取り込んでも、ID は重ならない ---
            expect (manager.load (2).wasOk());
            const auto factoryId = manager.getEntry (2).info.id;
            const auto exported = root.folder.getChildFile ("exported.json");
            expect (manager.exportCurrent (exported).wasOk());
            expect (manager.importFile (exported).wasOk());
            expect (manager.importFile (exported).wasOk());
            expectEquals (manager.getNumPresets(), factory + 4);
            expect (idsAreUnique());

            for (int i = factory; i < manager.getNumPresets(); ++i)
                expect (manager.getEntry (i).info.id != factoryId);

            // 取り込んだ音色を読むと、その行が「現在の音色」になり、次へ進むと別の行へ移る (同じ行に留まらない)
            const int firstUser = factory;
            expect (manager.load (firstUser).wasOk());
            expectEquals (manager.getCurrentIndex(), firstUser);
            juce::Array<int> visited;
            for (int i = 0; i < 4; ++i)
            {
                visited.addIfNotAlreadyThere (manager.getCurrentIndex());
                expect (manager.loadAdjacent (1).wasOk());
            }
            expectEquals (visited.size(), 4);

            // --- 名前は違うのにファイル名が重なる場合は、上書きせず別のファイルにする ---
            const int before = userFiles();
            expect (manager.saveUser ("AB", "Bass", {}, {}).wasOk());
            expect (manager.saveUser ("A/B", "Bass", {}, {}).wasOk());      // "/" は落ちて "AB" になる
            expect (manager.saveUser ("???", "Bass", {}, {}).wasOk());      // 使える文字がない → Untitled
            expect (manager.saveUser ("Untitled", "Bass", {}, {}).wasOk());
            expectEquals (userFiles(), before + 4);
            expect (idsAreUnique());

            // 同じ名前なら上書き (件数は増えない)
            expect (manager.saveUser ("A/B", "Bass", {}, {}).wasOk());
            expectEquals (userFiles(), before + 4);

            // --- フォルダーの外を指す名前でも、User フォルダーの直下にしか書かない ---
            const int beforeOdd = userFiles();
            for (const char* name : { "~Lead", "../../evil", "..", ".hidden", "~/x", "a/../../b" })
                expect (manager.saveUser (name, "Bass", {}, {}).wasOk(), name);

            expectEquals (userFiles(), beforeOdd + 6);
            expect (! root.folder.getChildFile ("evil.json").exists());
            expect (! root.folder.getParentDirectory().getChildFile ("evil.json").exists());
            expect (! folder.getParentDirectory().getChildFile ("b.json").exists());

            for (int i = factory; i < manager.getNumPresets(); ++i)
            {
                expect (manager.getEntry (i).file.getParentDirectory() == folder);
                expect (! manager.getEntry (i).file.getFileName().startsWithChar ('.'));
                expect (! manager.getEntry (i).file.getFileName().startsWithChar ('~'));
            }
        }

        beginTest ("Untrusted presets cannot move the user's Output Gain through a macro, and odd values are rejected (D-068)");
        {
            TempRoot root;
            PolyBriskAudioProcessor processor (root.folder);
            auto& manager = processor.getPresetManager();
            setParam (processor, ids::outputGain, -20.0f);

            const auto file = root.folder.getChildFile ("loud.json");
            file.replaceWithText (R"({ "schemaVersion": 1, "name": "Loud", "tags": ["A|B", "Bright"],
                                       "parameters": { "macro.1": 100, "output.gain": 12 },
                                       "macros": [ { "index": 1, "name": "X", "cc": 1e300, "mappings": [
                                           { "target": "output.gain", "deltaMin": 0, "deltaMax": 1 },
                                           { "target": "filter.cutoff", "deltaMin": 0, "deltaMax": 0.2, "curve": 1e300 } ] } ] })");
            expect (manager.importFile (file).wasOk());
            expect (manager.load (manager.getNumPresets() - 1).wasOk());

            expectWithinAbsoluteError (getParam (processor, ids::outputGain), -20.0f, 0.01f);
            const auto macro = processor.getMetaState().getMacro (0);
            expectEquals (static_cast<int> (macro.mappings.size()), 1);                 // output.gain への割当は捨てられる
            expectEquals (macro.mappings.front().target, juce::String (ids::filterCutoff));
            expectEquals (macro.cc, -1);

            // 実効値でも Output Gain は動かない
            juce::AudioBuffer<float> buffer (2, 256);
            juce::MidiBuffer midi;
            processor.setRateAndBufferSizeDetails (48000.0, 256);
            processor.prepareToPlay (48000.0, 256);
            processor.processBlock (buffer, midi);
            expectWithinAbsoluteError (processor.getEffectiveValue (polybrisk::params::indexOf (ids::outputGain)), -20.0f, 0.01f);

            // "|" はホストの状態でタグの区切りに使うので、タグからは落とす
            expect (processor.getMetaState().getPresetInfo().tags.contains ("AB"));
            expect (! processor.getMetaState().getPresetInfo().tags.joinIntoString (" ").containsChar ('|'));

            // 整数でない / 桁外れの schemaVersion は受け付けない
            for (const char* version : { "1.9", "1e300", "-1e300", "0", "2" })
            {
                file.replaceWithText ("{ \"schemaVersion\": " + juce::String (version) + ", \"name\": \"V\", \"parameters\": {} }");
                expect (manager.importFile (file).failed(), version);
            }
        }

        beginTest ("Settings shared by several instances are merged, not overwritten (D-068)");
        {
            TempRoot root;
            PolyBriskAudioProcessor a (root.folder), b (root.folder);
            const auto idX = a.getPresetManager().getEntry (1).info.id;
            const auto idY = a.getPresetManager().getEntry (2).info.id;

            a.getPresetManager().setFavourite (idX, true);
            a.getPresetManager().setPerformanceLock (true);
            b.getPresetManager().setFavourite (idY, true);       // b は a の変更を知らない状態から書く

            PolyBriskAudioProcessor c (root.folder);
            expect (c.getPresetManager().isFavourite (idX));
            expect (c.getPresetManager().isFavourite (idY));
            expect (c.getPresetManager().getPerformanceLock());

            // b も、次に音色を読むときには Lock を取り込んでいる (ベンド幅を保つ)
            setParam (b, ids::bendUp, 7.0f);
            expect (b.getPresetManager().load (0).wasOk());
            expect (b.getPresetManager().getPerformanceLock());
            expectWithinAbsoluteError (getParam (b, ids::bendUp), 7.0f, 0.01f);

            // a で保存した音色は、b で refresh すると見える (ブラウザーを開くときに呼ぶ)
            const int before = b.getPresetManager().getNumPresets();
            expect (a.getPresetManager().saveUser ("From A", "Bass", {}, {}).wasOk());
            expectEquals (b.getPresetManager().getNumPresets(), before);
            b.getPresetManager().refresh();
            expectEquals (b.getPresetManager().getNumPresets(), before + 1);
        }

        beginTest ("Favourites and Performance Lock are user settings and persist across instances");
        {
            TempRoot root;
            juce::String favouriteId;

            {
                PolyBriskAudioProcessor processor (root.folder);
                auto& manager = processor.getPresetManager();
                favouriteId = manager.getEntry (4).info.id;
                expect (! manager.isFavourite (favouriteId));
                manager.setFavourite (favouriteId, true);
                manager.setFavourite (manager.getEntry (9).info.id, true);
                manager.setFavourite (manager.getEntry (9).info.id, false);
                manager.setPerformanceLock (true);
                expectEquals (static_cast<int> (manager.filter ({}, {}, true).size()), 1);
            }

            PolyBriskAudioProcessor processor (root.folder);
            auto& manager = processor.getPresetManager();
            expect (manager.isFavourite (favouriteId));
            expect (manager.getPerformanceLock());
            expectEquals (static_cast<int> (manager.filter ({}, {}, true).size()), 1);

            // Performance Lock: プリセットを切り替えても Bend Range と Vibrato の設定を保つ
            setParam (processor, ids::bendUp, 12.0f);
            setParam (processor, ids::vibratoDepth, 123.0f);
            setParam (processor, ids::vibratoDelay, 777.0f);
            const int popBrass = indexOfPreset (manager, "Pop Brass");   // Vibrato の設定 (Depth 25) を持っている
            expect (manager.load (popBrass).wasOk());
            expectEquals (getParam (processor, ids::bendUp), 12.0f);
            expectWithinAbsoluteError (getParam (processor, ids::vibratoDepth), 123.0f, 0.01f);
            expectWithinAbsoluteError (getParam (processor, ids::vibratoDelay), 777.0f, 0.5f);

            manager.setPerformanceLock (false);
            expect (manager.load (popBrass).wasOk());
            expectEquals (getParam (processor, ids::bendUp), 2.0f);
            expectWithinAbsoluteError (getParam (processor, ids::vibratoDepth), 25.0f, 0.01f);
        }

        beginTest ("Init gives an empty sound: defaults everywhere, no macro mappings, default gate pattern, Output Gain untouched (D-057)");
        {
            TempRoot root;
            PolyBriskAudioProcessor processor (root.folder);
            PolyBriskAudioProcessor fresh (root.folder);     // 何も読んでいない、既定の状態
            auto& manager = processor.getPresetManager();

            expect (manager.load (indexOfPreset (manager, "Supersaw Gate")).wasOk());
            setParam (processor, ids::outputGain, -6.0f);
            setParam (processor, ids::filterCutoff, 333.0f);
            processor.syncFromAudioThread();

            expect (manager.loadInit().wasOk());
            processor.syncFromAudioThread();

            for (const auto& d : polybrisk::params::all())
            {
                const float expected = juce::String (d.id) == ids::outputGain ? -6.0f : d.defaultValue;
                expectWithinAbsoluteError (getParam (processor, d.id), expected, std::abs (expected) * 2.0e-5f + 1.0e-4f, d.id);
            }

            for (int m = 0; m < 4; ++m)
            {
                expect (processor.getMetaState().getMacro (m).mappings.empty());
                expectEquals (processor.getMetaState().getMacro (m).name, fresh.getMetaState().getMacro (m).name);
                expectEquals (processor.getMetaState().getMacro (m).cc, fresh.getMetaState().getMacro (m).cc);
            }

            expect (processor.getMetaState().getGatePattern() == fresh.getMetaState().getGatePattern());
            expectEquals (processor.getMetaState().getGatePatternName(), fresh.getMetaState().getGatePatternName());
            expectEquals (processor.getMetaState().getPresetInfo().name, juce::String ("Init"));
            expect (! processor.getMetaState().getPresetInfo().factory);
            expect (! processor.getMetaState().isDirty());
            expectEquals (manager.getCurrentIndex(), -1);
            expectEquals (manager.getNumFactoryPresets(), 128);      // 一覧には載らない

            // 音が出る (OSC A のソー 1 本)
            juce::AudioBuffer<float> buffer (2, 4800);
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, 0.8f), 0);
            processor.prepareToPlay (48000.0, 4800);
            processor.processBlock (buffer, midi);
            expect (buffer.getMagnitude (0, 4800) > 0.01f);
        }

        beginTest ("Output Gain belongs to the user: presets neither store nor change it; Preset Level carries the per-preset volume (D-055)");
        {
            TempRoot root;
            PolyBriskAudioProcessor processor (root.folder);
            auto& manager = processor.getPresetManager();

            // Factory の音色を切り替えても、Output Gain は変わらない。Preset Level は音色ごとに変わる
            setParam (processor, ids::outputGain, -7.5f);
            expect (manager.load (indexOfPreset (manager, "Anthem Saw")).wasOk());
            expectWithinAbsoluteError (getParam (processor, ids::outputGain), -7.5f, 1.0e-4f);
            const float anthemLevel = getParam (processor, ids::presetLevel);
            expect (manager.load (indexOfPreset (manager, "Octave Bounce Bass")).wasOk());
            expectWithinAbsoluteError (getParam (processor, ids::outputGain), -7.5f, 1.0e-4f);
            expect (std::abs (getParam (processor, ids::presetLevel) - anthemLevel) > 1.0f);

            // Output Gain を動かしても音色は Dirty にならない。Preset Level を動かすと Dirty になる
            processor.syncFromAudioThread();
            expect (! processor.getMetaState().isDirty());
            setParam (processor, ids::outputGain, -3.0f);
            processor.syncFromAudioThread();
            expect (! processor.getMetaState().isDirty());
            setParam (processor, ids::presetLevel, 5.0f);
            processor.syncFromAudioThread();
            expect (processor.getMetaState().isDirty());

            // 書き出した JSON は preset.level を持ち、output.gain を持たない。読み戻すと Preset Level だけが戻る
            const auto json = jsonOf (processor);
            expect (json.contains ("preset.level"));
            expect (! json.contains ("output.gain"));
            setParam (processor, ids::presetLevel, 0.0f);
            setParam (processor, ids::outputGain, 2.0f);
            expect (applyJson (processor, json));
            expectWithinAbsoluteError (getParam (processor, ids::presetLevel), 5.0f, 1.0e-4f);
            expectWithinAbsoluteError (getParam (processor, ids::outputGain), 2.0f, 1.0e-4f);

            // 古いプリセットに output.gain が入っていても無視する
            expect (applyJson (processor, json.replace ("\"preset.level\"", "\"output.gain\": -40.0, \"preset.level\"")));
            expectWithinAbsoluteError (getParam (processor, ids::outputGain), 2.0f, 1.0e-4f);

            // ホストの状態 (DAW のプロジェクト) には Output Gain も保存される
            juce::MemoryBlock block;
            processor.getStateInformation (block);
            PolyBriskAudioProcessor restored (root.folder);
            restored.setStateInformation (block.getData(), static_cast<int> (block.getSize()));
            expectWithinAbsoluteError (getParam (restored, ids::outputGain), 2.0f, 1.0e-4f);
            expectWithinAbsoluteError (getParam (restored, ids::presetLevel), 5.0f, 1.0e-4f);
        }

        beginTest ("Host state keeps the preset name, the dirty flag and the view mode; the sound does not need the preset file");
        {
            TempRoot root;
            juce::MemoryBlock block;

            {
                PolyBriskAudioProcessor source (root.folder);
                expect (source.getPresetManager().load (indexOfPreset (source.getPresetManager(), "Bright Pop Stab")).wasOk());
                setParam (source, ids::filterCutoff, 2222.0f);
                source.syncFromAudioThread();
                source.getMetaState().setAdvancedView (true);
                source.getStateInformation (block);
            }

            TempRoot elsewhere;     // プリセットファイルのない別の環境
            PolyBriskAudioProcessor restored (elsewhere.folder);
            restored.setStateInformation (block.getData(), static_cast<int> (block.getSize()));

            const auto info = restored.getMetaState().getPresetInfo();
            expectEquals (info.name, juce::String ("Bright Pop Stab"));
            expectEquals (info.category, juce::String ("Stab / Chord Hit"));
            expect (info.factory);
            expect (restored.getMetaState().isDirty());
            expect (restored.getMetaState().isAdvancedView());
            expectWithinAbsoluteError (getParam (restored, ids::filterCutoff), 2222.0f, 0.5f);
            expectEquals (restored.getMetaState().getMacro (0).name, juce::String ("Punch"));

            restored.syncFromAudioThread();
            expect (restored.getMetaState().isDirty());
        }

        beginTest ("Switching presets ends arpeggiator notes: nothing keeps playing after the keys are released");
        {
            TempRoot root;
            PolyBriskAudioProcessor processor (root.folder);
            auto& manager = processor.getPresetManager();
            const int arpPreset = manager.filter ("Arp Pulse Stab", {}, false).at (0);
            expect (manager.load (arpPreset).wasOk());
            setParam (processor, ids::arpLatch, 1.0f);
            setParam (processor, ids::delayEnabled, 0.0f);
            setParam (processor, ids::reverbEnabled, 0.0f);

            processor.setRateAndBufferSizeDetails (48000.0, 256);
            processor.prepareToPlay (48000.0, 256);
            juce::AudioBuffer<float> buffer (2, 256);

            const auto run = [&] (int blocks, const juce::MidiMessage* message = nullptr)
            {
                float peak = 0.0f;
                for (int b = 0; b < blocks; ++b)
                {
                    juce::MidiBuffer midi;
                    if (b == 0 && message != nullptr)
                        midi.addEvent (*message, 0);
                    processor.processBlock (buffer, midi);
                    peak = std::max (peak, buffer.getMagnitude (0, 256));
                }
                return peak;
            };

            const auto on = juce::MidiMessage::noteOn (1, 60, 0.9f);
            const auto off = juce::MidiMessage::noteOff (1, 60);
            expect (run (40, &on) > 0.01f);
            run (4, &off);
            expect (run (200) > 0.01f);     // Latch で鳴り続けている

            // 別の (Arp を使わない) プリセットへ切り替える → Latch の和音は捨てられ、音は消える
            expect (manager.load (manager.filter ("Tiny Tight Stab", {}, false).at (0)).wasOk());
            run (300);
            expectEquals (run (50), 0.0f);
            processor.releaseResources();
        }
    }
};

PresetTests presetTests;
} // namespace
