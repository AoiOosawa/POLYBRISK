#include "PluginProcessor.h"
#include "TestHelpers.h"
#include "state/MacroEngine.h"
#include "state/Parameters.h"
#include "state/StateSerializer.h"

#include <cmath>

namespace
{
namespace ids = polybrisk::params::id;
namespace macro = polybrisk::macro;
using polybrisk::MacroMapping;
using polybrisk::test::TimedMidi;

void setParam (PolyBriskAudioProcessor& processor, const char* parameterId, float realValue)
{
    auto* param = processor.getValueTreeState().getParameter (parameterId);
    param->setValueNotifyingHost (param->convertTo0to1 (realValue));
}

float getParam (PolyBriskAudioProcessor& processor, const char* parameterId)
{
    return processor.getValueTreeState().getRawParameterValue (parameterId)->load();
}

/** 1 ブロックだけ処理して、マクロ適用後の実効値を更新する。 */
void runBlock (PolyBriskAudioProcessor& processor, const std::vector<TimedMidi>& events = {}, int blocks = 40)
{
    polybrisk::test::renderProcessor (processor, 48000.0, { 256 }, 256 * blocks, events);
}

float effective (PolyBriskAudioProcessor& processor, const char* parameterId)
{
    return processor.getEffectiveValue (polybrisk::params::indexOf (parameterId));
}

class MacroTests final : public juce::UnitTest
{
public:
    MacroTests() : juce::UnitTest ("Macros", "state") {}

    void runTest() override
    {
        beginTest ("delta(): 0.5 is neutral, 0 / 1 give deltaMin / deltaMax, curves keep the end points");
        for (const auto curve : { macro::Curve::linear, macro::Curve::easeIn, macro::Curve::easeOut })
        {
            const macro::Entry e { 0, 0, -0.25f, 0.4f, curve };
            expectEquals (macro::delta (e, 0.5f), 0.0f);
            expectWithinAbsoluteError (macro::delta (e, 0.0f), -0.25f, 1.0e-6f);
            expectWithinAbsoluteError (macro::delta (e, 1.0f), 0.4f, 1.0e-6f);
            expectWithinAbsoluteError (macro::delta (e, -3.0f), -0.25f, 1.0e-6f);  // 範囲外はクランプ
            expectWithinAbsoluteError (macro::delta (e, 9.0f), 0.4f, 1.0e-6f);

            // 単調
            float previous = macro::delta (e, 0.0f);
            for (float m = 0.0f; m <= 1.0f; m += 0.01f)
            {
                expect (macro::delta (e, m) >= previous - 1.0e-6f);
                previous = macro::delta (e, m);
            }
        }
        {
            const macro::Entry linear { 0, 0, -0.2f, 0.6f, macro::Curve::linear };
            expectWithinAbsoluteError (macro::delta (linear, 0.25f), -0.1f, 1.0e-6f);
            expectWithinAbsoluteError (macro::delta (linear, 0.75f), 0.3f, 1.0e-6f);
        }

        beginTest ("Registry normalisation matches the APVTS parameters (including skew)");
        {
            PolyBriskAudioProcessor processor;

            for (const auto& d : polybrisk::params::all())
            {
                auto* parameter = processor.getValueTreeState().getParameter (d.id);

                if (d.kind != polybrisk::params::Kind::continuous)
                    continue;

                for (const float n : { 0.0f, 0.1f, 0.37f, 0.5f, 0.82f, 1.0f })
                {
                    const float real = parameter->convertFrom0to1 (n);
                    expectWithinAbsoluteError (polybrisk::params::fromNormalised (d, n), real, std::max (1.0e-3f, std::abs (real) * 1.0e-4f), d.id);
                    expectWithinAbsoluteError (polybrisk::params::toNormalised (d, real), n, 1.0e-4f, d.id);
                }
            }
        }

        beginTest ("Macro 0.5 leaves the base value untouched; 0 / 1 apply the deltas in normalised space");
        {
            PolyBriskAudioProcessor processor;
            auto& meta = processor.getMetaState();
            setParam (processor, ids::filterResonance, 40.0f);
            setParam (processor, ids::filterCutoff, 1000.0f);
            expect (meta.setMapping (0, { ids::filterResonance, -0.3f, 0.25f, macro::Curve::linear }));
            expect (meta.setMapping (0, { ids::filterCutoff, -0.2f, 0.2f, macro::Curve::linear }));

            runBlock (processor);
            expectEquals (effective (processor, ids::filterResonance), getParam (processor, ids::filterResonance));
            expectEquals (effective (processor, ids::filterCutoff), getParam (processor, ids::filterCutoff));

            setParam (processor, ids::macros[0], 100.0f);
            runBlock (processor);
            expectWithinAbsoluteError (effective (processor, ids::filterResonance), 65.0f, 0.01f);

            // 非線形レンジ (Cutoff) は正規化空間で加算してから実値へ戻す
            const auto* cutoff = polybrisk::params::find (ids::filterCutoff);
            const float expectedUp = polybrisk::params::fromNormalised (*cutoff, polybrisk::params::toNormalised (*cutoff, 1000.0f) + 0.2f);
            expectWithinAbsoluteError (effective (processor, ids::filterCutoff), expectedUp, expectedUp * 0.001f);
            expect (expectedUp > 3000.0f);

            setParam (processor, ids::macros[0], 0.0f);
            runBlock (processor);
            expectWithinAbsoluteError (effective (processor, ids::filterResonance), 10.0f, 0.01f);

            setParam (processor, ids::macros[0], 75.0f);
            runBlock (processor);
            expectWithinAbsoluteError (effective (processor, ids::filterResonance), 52.5f, 0.01f);

            // Base 値 (ホストに見える値) はマクロで動かない
            expectWithinAbsoluteError (getParam (processor, ids::filterResonance), 40.0f, 1.0e-4f);
        }

        beginTest ("Several macros on one parameter add up, then clamp; one macro can drive several parameters");
        {
            PolyBriskAudioProcessor processor;
            auto& meta = processor.getMetaState();
            setParam (processor, ids::filterResonance, 50.0f);
            expect (meta.setMapping (0, { ids::filterResonance, -0.1f, 0.2f, macro::Curve::linear }));
            expect (meta.setMapping (1, { ids::filterResonance, -0.1f, 0.15f, macro::Curve::linear }));
            expect (meta.setMapping (2, { ids::filterResonance, -0.6f, 0.6f, macro::Curve::linear }));
            expect (meta.setMapping (0, { ids::filterDrive, 0.0f, 0.5f, macro::Curve::linear }));

            setParam (processor, ids::macros[0], 100.0f);
            setParam (processor, ids::macros[1], 100.0f);
            runBlock (processor);
            expectWithinAbsoluteError (effective (processor, ids::filterResonance), 85.0f, 0.01f);
            expectWithinAbsoluteError (effective (processor, ids::filterDrive), 50.0f, 0.01f);

            setParam (processor, ids::macros[2], 100.0f);
            runBlock (processor);
            expectEquals (effective (processor, ids::filterResonance), 100.0f);  // 0.5 + 0.2 + 0.15 + 0.6 → クランプ

            setParam (processor, ids::macros[0], 50.0f);
            setParam (processor, ids::macros[1], 50.0f);
            setParam (processor, ids::macros[2], 0.0f);
            runBlock (processor);
            expectEquals (effective (processor, ids::filterResonance), 0.0f);    // 0.5 - 0.6 → クランプ
        }

        beginTest ("Editing the base value keeps the mapping and moves the result without a jump");
        {
            PolyBriskAudioProcessor processor;
            auto& meta = processor.getMetaState();
            expect (meta.setMapping (0, { ids::filterResonance, -0.2f, 0.2f, macro::Curve::linear }));
            setParam (processor, ids::filterResonance, 30.0f);
            setParam (processor, ids::macros[0], 100.0f);
            runBlock (processor);
            expectWithinAbsoluteError (effective (processor, ids::filterResonance), 50.0f, 0.01f);

            // Advanced で Base 値を編集: 割当は残り、結果は Base の変化分だけ動く
            setParam (processor, ids::filterResonance, 31.0f);
            runBlock (processor);
            expectWithinAbsoluteError (effective (processor, ids::filterResonance), 51.0f, 0.01f);
            expectEquals (static_cast<int> (meta.getMacro (0).mappings.size()), 1);
            expectEquals (meta.getMacro (0).mappings[0].target, juce::String (ids::filterResonance));
            expectWithinAbsoluteError (getParam (processor, ids::filterResonance), 31.0f, 1.0e-4f);
        }

        beginTest ("Macro changes reach the sound (cutoff mapped to Macro 1)");
        {
            const auto level = [] (float macroValue)
            {
                PolyBriskAudioProcessor processor;
                setParam (processor, ids::oscA.wave, 5.0f);          // Sine, 440 Hz
                setParam (processor, ids::filterCharacter, 1.0f);    // Clean
                setParam (processor, ids::ampSustain, 100.0f);
                setParam (processor, ids::filterCutoff, 440.0f);
                processor.getMetaState().setMapping (0, { ids::filterCutoff, -0.3f, 0.3f, macro::Curve::linear });
                setParam (processor, ids::macros[0], macroValue);

                const auto out = polybrisk::test::renderProcessor (processor, 48000.0, { 256 }, 48000,
                                                                    { { 0, juce::MidiMessage::noteOn (1, 69, 1.0f) } });
                return juce::Decibels::gainToDecibels (polybrisk::test::peakLevel (out, 24000, 24000));
            };

            const float neutral = level (50.0f);
            expect (level (100.0f) > neutral + 2.5f);   // カットオフが開き、-3 dB の減衰がなくなる
            expect (level (0.0f) < neutral - 30.0f);    // カットオフが閉じる
        }

        beginTest ("Mapping rules: continuous targets only, at most 8 per macro, same target replaces");
        {
            PolyBriskAudioProcessor processor;
            auto& meta = processor.getMetaState();

            expect (! meta.setMapping (0, { ids::oscA.wave, 0.0f, 1.0f, macro::Curve::linear }));        // 選択
            expect (! meta.setMapping (0, { ids::oscA.enabled, 0.0f, 1.0f, macro::Curve::linear }));     // ON/OFF
            expect (! meta.setMapping (0, { ids::oscA.unisonVoices, 0.0f, 1.0f, macro::Curve::linear })); // 整数
            expect (! meta.setMapping (0, { ids::macros[1], 0.0f, 1.0f, macro::Curve::linear }));        // マクロ自身
            expect (! meta.setMapping (0, { "does.not.exist", 0.0f, 1.0f, macro::Curve::linear }));
            expect (! meta.setMapping (7, { ids::filterCutoff, 0.0f, 1.0f, macro::Curve::linear }));

            const char* targets[] = { ids::filterCutoff, ids::filterResonance, ids::filterDrive, ids::filterEnvAmount,
                                      ids::ampAttack, ids::ampDecay, ids::ampSustain, ids::ampRelease, ids::oscA.detune };
            for (int i = 0; i < 8; ++i)
                expect (meta.setMapping (1, { targets[i], -0.1f, 0.1f, macro::Curve::linear }));
            expect (! meta.setMapping (1, { targets[8], -0.1f, 0.1f, macro::Curve::linear }));            // 9 個目は不可

            expect (meta.setMapping (1, { ids::filterCutoff, -9.0f, 9.0f, macro::Curve::easeIn }));       // 上書き + クランプ
            const auto config = meta.getMacro (1);
            expectEquals (static_cast<int> (config.mappings.size()), 8);
            expectEquals (config.mappings[0].deltaMin, -1.0f);
            expectEquals (config.mappings[0].deltaMax, 1.0f);

            meta.removeMapping (1, ids::filterCutoff);
            expectEquals (static_cast<int> (meta.getMacro (1).mappings.size()), 7);
            meta.clearMacro (1);
            expectEquals (static_cast<int> (meta.getMacro (1).mappings.size()), 0);
        }

        beginTest ("State roundtrip keeps macro values, names, mappings and CC assignments");
        {
            PolyBriskAudioProcessor source;
            auto& meta = source.getMetaState();
            meta.setMacroName (0, "Punch");
            meta.setMacroName (3, juce::String::fromUTF8 ("明るさ"));
            meta.setMacroCc (0, 74);
            meta.setMacroCc (2, 21);
            meta.setMapping (0, { ids::filterEnvAmount, -0.25f, 0.3f, macro::Curve::linear });
            meta.setMapping (0, { ids::transientAmount, -0.1f, 0.25f, macro::Curve::easeOut });
            meta.setMapping (3, { ids::filterCutoff, -0.4f, 0.35f, macro::Curve::easeIn });
            setParam (source, ids::macros[0], 80.0f);
            setParam (source, ids::macros[3], 15.0f);
            setParam (source, ids::bendUp, 12.0f);
            setParam (source, ids::bendMode, 1.0f);
            setParam (source, ids::vibratoDepth, 60.0f);
            setParam (source, ids::filterCutoff, 1500.0f);

            juce::MemoryBlock block;
            source.getStateInformation (block);

            PolyBriskAudioProcessor restored;
            restored.setStateInformation (block.getData(), static_cast<int> (block.getSize()));

            for (const auto& d : polybrisk::params::all())
                expectWithinAbsoluteError (getParam (restored, d.id), getParam (source, d.id), std::max (1.0e-3f, std::abs (getParam (source, d.id)) * 1.0e-5f), d.id);

            for (int m = 0; m < macro::numMacros; ++m)
            {
                const auto a = source.getMetaState().getMacro (m);
                const auto b = restored.getMetaState().getMacro (m);
                expectEquals (b.name, a.name);
                expectEquals (b.cc, a.cc);
                expectEquals (static_cast<int> (b.mappings.size()), static_cast<int> (a.mappings.size()));

                for (size_t i = 0; i < std::min (a.mappings.size(), b.mappings.size()); ++i)
                {
                    expectEquals (b.mappings[i].target, a.mappings[i].target);
                    expectEquals (b.mappings[i].deltaMin, a.mappings[i].deltaMin);
                    expectEquals (b.mappings[i].deltaMax, a.mappings[i].deltaMax);
                    expect (b.mappings[i].curve == a.mappings[i].curve);
                }
            }

            // 復元直後から同じ実効値・同じ音になる
            runBlock (source);
            runBlock (restored);
            expectEquals (effective (restored, ids::filterCutoff), effective (source, ids::filterCutoff));
            expect (effective (source, ids::filterCutoff) < 1000.0f);

            // 2 回目の保存結果は 1 回目とバイト単位で一致する
            juce::MemoryBlock second;
            restored.getStateInformation (second);
            expect (second == block);
        }

        beginTest ("Old / damaged macro data: missing META resets macros, invalid entries are skipped");
        {
            PolyBriskAudioProcessor processor;
            auto& apvts = processor.getValueTreeState();
            auto& meta = processor.getMetaState();
            meta.setMacroName (0, "Keep?");
            meta.setMapping (0, { ids::filterCutoff, -0.1f, 0.1f, macro::Curve::linear });

            auto tree = polybrisk::state::toTree (apvts, meta);
            auto macros = tree.getChildWithName (polybrisk::state::metaType).getChildWithName (polybrisk::MetaState::macrosType);
            auto first = macros.getChild (0);

            juce::ValueTree bogus { "MAPPING" };
            bogus.setProperty ("target", "osc.a.wave", nullptr);       // 離散値は不可
            first.appendChild (bogus, nullptr);
            juce::ValueTree unknown { "MAPPING" };
            unknown.setProperty ("target", "removed.parameter", nullptr);
            first.appendChild (unknown, nullptr);
            juce::ValueTree wild { "MAPPING" };
            wild.setProperty ("target", juce::String (ids::filterDrive), nullptr);
            wild.setProperty ("deltaMin", std::numeric_limits<double>::quiet_NaN(), nullptr);
            wild.setProperty ("deltaMax", 55.0, nullptr);
            wild.setProperty ("curve", 99, nullptr);
            first.appendChild (wild, nullptr);
            first.setProperty ("cc", 64, nullptr);                      // 予約済みの CC
            macros.getChild (1).setProperty ("index", 42, nullptr);     // 範囲外のマクロ番号

            expect (polybrisk::state::fromTree (tree, apvts, meta));
            const auto config = meta.getMacro (0);
            expectEquals (config.name, juce::String ("Keep?"));
            expectEquals (config.cc, -1);
            expectEquals (static_cast<int> (config.mappings.size()), 2);
            expectEquals (config.mappings[1].deltaMin, 0.0f);
            expectEquals (config.mappings[1].deltaMax, 1.0f);
            expect (config.mappings[1].curve == macro::Curve::linear);
            expectEquals (meta.getMacro (1).name, juce::String ("Macro 2"));

            // META のない Phase 2 以前の状態: マクロは初期状態に戻る
            tree.removeChild (tree.getChildWithName (polybrisk::state::metaType), nullptr);
            expect (polybrisk::state::fromTree (tree, apvts, meta));
            expectEquals (meta.getMacro (0).name, juce::String ("Macro 1"));
            expectEquals (static_cast<int> (meta.getMacro (0).mappings.size()), 0);
        }

        beginTest ("MIDI CC assigned to a macro moves it; the host parameter follows");
        {
            PolyBriskAudioProcessor processor;
            auto& meta = processor.getMetaState();
            setParam (processor, ids::filterResonance, 50.0f);
            meta.setMapping (1, { ids::filterResonance, -0.5f, 0.5f, macro::Curve::linear });
            meta.setMacroCc (1, 74);

            runBlock (processor, { { 0, juce::MidiMessage::controllerEvent (1, 74, 127) } });
            expectWithinAbsoluteError (effective (processor, ids::filterResonance), 100.0f, 0.01f);
            expectWithinAbsoluteError (getParam (processor, ids::macros[1]), 50.0f, 1.0e-4f);     // まだ同期前

            processor.syncFromAudioThread();
            expectWithinAbsoluteError (getParam (processor, ids::macros[1]), 100.0f, 1.0e-3f);
            runBlock (processor);
            expectWithinAbsoluteError (effective (processor, ids::filterResonance), 100.0f, 0.01f);

            // その後ホスト / UI がマクロを動かせば、そちらが優先される
            setParam (processor, ids::macros[1], 50.0f);
            runBlock (processor);
            expectWithinAbsoluteError (effective (processor, ids::filterResonance), 50.0f, 1.0e-4f);

            // 割り当てていない CC、予約済みの CC1 / CC64 はマクロを動かさない
            runBlock (processor, { { 0, juce::MidiMessage::controllerEvent (1, 75, 127) },
                                   { 0, juce::MidiMessage::controllerEvent (1, 1, 127) },
                                   { 0, juce::MidiMessage::controllerEvent (1, 64, 127) } });
            processor.syncFromAudioThread();
            expectWithinAbsoluteError (getParam (processor, ids::macros[1]), 50.0f, 1.0e-4f);
        }

        beginTest ("MIDI Learn: next assignable CC is bound; reserved CCs are ignored; a CC belongs to one macro");
        {
            PolyBriskAudioProcessor processor;
            auto& meta = processor.getMetaState();

            expect (! macro::isAssignableCc (1));
            expect (! macro::isAssignableCc (64));
            expect (! macro::isAssignableCc (0));
            expect (! macro::isAssignableCc (120));
            expect (macro::isAssignableCc (2) && macro::isAssignableCc (119));

            processor.beginMidiLearn (2);
            expectEquals (processor.getMidiLearnTarget(), 2);

            runBlock (processor, { { 0, juce::MidiMessage::controllerEvent (1, 1, 100) },
                                   { 10, juce::MidiMessage::controllerEvent (1, 64, 127) },
                                   { 20, juce::MidiMessage::controllerEvent (1, 123, 0) } });
            processor.syncFromAudioThread();
            expectEquals (processor.getMidiLearnTarget(), 2);   // 予約済みでは確定しない
            expectEquals (meta.getMacro (2).cc, -1);

            runBlock (processor, { { 0, juce::MidiMessage::controllerEvent (1, 20, 64) } });
            processor.syncFromAudioThread();
            expectEquals (processor.getMidiLearnTarget(), -1);
            expectEquals (meta.getMacro (2).cc, 20);

            // 同じ CC を別のマクロに学習させると、元のマクロからは外れる
            processor.beginMidiLearn (0);
            runBlock (processor, { { 0, juce::MidiMessage::controllerEvent (1, 20, 10) } });
            processor.syncFromAudioThread();
            expectEquals (meta.getMacro (0).cc, 20);
            expectEquals (meta.getMacro (2).cc, -1);

            processor.beginMidiLearn (1);
            processor.cancelMidiLearn();
            expectEquals (processor.getMidiLearnTarget(), -1);
        }
    }
};

MacroTests macroTests;
} // namespace
