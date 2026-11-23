#include "PluginProcessor.h"
#include "state/Parameters.h"
#include "synth/Oscillator.h"

#include <set>
#include <string>

namespace
{
class ParameterRegistryTests final : public juce::UnitTest
{
public:
    ParameterRegistryTests() : juce::UnitTest ("Parameter registry", "state") {}

    void runTest() override
    {
        const auto definitions = polybrisk::params::all();

        beginTest ("IDs are unique, well-formed and have sane ranges");
        {
            std::set<std::string> seen;

            for (const auto& d : definitions)
            {
                const juce::String parameterId (d.id);
                expect (seen.insert (d.id).second, "duplicate id " + parameterId);
                expect (parameterId.containsOnly ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789."), parameterId);
                expect (! parameterId.startsWithChar ('.') && ! parameterId.endsWithChar ('.'), parameterId);
                expect (d.min < d.max, parameterId);
                expect (d.defaultValue >= d.min && d.defaultValue <= d.max, parameterId);

                if (d.kind == polybrisk::params::Kind::choice)
                {
                    expect (! d.choices.empty(), parameterId);
                    expectEquals (static_cast<int> (d.max), static_cast<int> (d.choices.size()) - 1, parameterId);
                }

                if (d.kind == polybrisk::params::Kind::integer)
                {
                    expectWithinAbsoluteError (d.min, std::round (d.min), 0.0f, parameterId);
                    expectWithinAbsoluteError (d.max, std::round (d.max), 0.0f, parameterId);
                    expectWithinAbsoluteError (d.defaultValue, std::round (d.defaultValue), 0.0f, parameterId);
                }

                // マクロ割当の対象は連続値パラメーターのみ (SPEC §10.1)
                // (マクロ自身は連続値だが、割当先にはしない)
                // Output Gain も割当先にしない (ユーザーの音量をプリセットのマクロが動かさない。D-068)
                const bool isMacro = parameterId.startsWith ("macro.");
                const bool isUserOnly = parameterId == juce::String (polybrisk::params::id::outputGain);
                expect (d.macroAssignable == (d.kind == polybrisk::params::Kind::continuous && ! isMacro && ! isUserOnly), parameterId);

                if (d.skewCentre != 0.0f)
                    expect (d.skewCentre > d.min && d.skewCentre < d.max, parameterId);

                expect (polybrisk::params::find (d.id) == &d);
            }

            expect (polybrisk::params::find ("does.not.exist") == nullptr);

            // プロセッサーはパラメーターごとの値を 256 個の固定長配列に持つ (PolyBriskAudioProcessor::maxParameters)
            expect (definitions.size() <= 256, "parameter count exceeds the processor's fixed-size arrays");
        }

        beginTest ("APVTS exposes exactly the registry, with matching defaults");
        {
            PolyBriskAudioProcessor processor;
            auto& apvts = processor.getValueTreeState();

            expectEquals (processor.getParameters().size(), static_cast<int> (definitions.size()));

            for (const auto& d : definitions)
            {
                auto* param = apvts.getParameter (d.id);
                expect (param != nullptr, d.id);

                if (param != nullptr)
                {
                    expectWithinAbsoluteError (param->convertFrom0to1 (param->getDefaultValue()), d.defaultValue, 1.0e-4f);
                    expectWithinAbsoluteError (apvts.getRawParameterValue (d.id)->load(), d.defaultValue, 1.0e-4f);
                }
            }
        }

        beginTest ("Wave choice order matches the DSP Waveform enum");
        {
            for (const auto* waveId : { polybrisk::params::id::oscA.wave, polybrisk::params::id::oscB.wave })
            {
                const auto* wave = polybrisk::params::find (waveId);
                expect (wave != nullptr);

                if (wave == nullptr)
                    continue;

                expectEquals (static_cast<int> (wave->choices.size()), 7);
                expectEquals (juce::String (wave->choices[static_cast<size_t> (polybrisk::Waveform::pulse)]), juce::String ("Pulse"));
                expectEquals (juce::String (wave->choices[static_cast<size_t> (polybrisk::Waveform::square)]), juce::String ("Square"));
                expectEquals (juce::String (wave->choices[static_cast<size_t> (polybrisk::Waveform::triangle)]), juce::String ("Triangle"));
                expectEquals (juce::String (wave->choices[static_cast<size_t> (polybrisk::Waveform::saw)]), juce::String ("Saw"));
                expectEquals (juce::String (wave->choices[static_cast<size_t> (polybrisk::Waveform::superSaw)]), juce::String ("SuperSAW"));
                expectEquals (juce::String (wave->choices[static_cast<size_t> (polybrisk::Waveform::sine)]), juce::String ("Sine"));
                expectEquals (juce::String (wave->choices[static_cast<size_t> (polybrisk::Waveform::noise)]), juce::String ("Noise"));
            }
        }

        beginTest ("SPEC defaults: OSC A on, OSC B off at 70%, Retrigger, unison 1");
        {
            namespace ids = polybrisk::params::id;
            const auto def = [] (const char* parameterId) { return polybrisk::params::find (parameterId)->defaultValue; };

            expectEquals (def (ids::oscA.enabled), 1.0f);
            expectEquals (def (ids::oscB.enabled), 0.0f);
            expectEquals (def (ids::oscA.level), 100.0f);
            expectEquals (def (ids::oscB.level), 70.0f);
            expectEquals (def (ids::oscA.phaseMode), 1.0f);
            expectEquals (def (ids::oscA.unisonVoices), 1.0f);
            expectEquals (def (ids::oscA.detune), 35.0f);
            expectEquals (def (ids::oscA.spread), 50.0f);
            expectEquals (def (ids::oscA.pulseWidth), 50.0f);
        }

        beginTest ("docs/PARAMETER_IDS.md lists every implemented parameter ID");
        {
            const juce::File doc = juce::File (POLYBRISK_SOURCE_DIR).getChildFile ("docs/PARAMETER_IDS.md");
            expect (doc.existsAsFile(), "missing " + doc.getFullPathName());

            const auto text = doc.loadFileAsString();
            const auto implemented = text.fromFirstOccurrenceOf ("<!-- implemented:begin -->", false, false)
                                         .upToFirstOccurrenceOf ("<!-- implemented:end -->", false, false);
            expect (implemented.isNotEmpty(), "implemented section markers not found");

            for (const auto& d : definitions)
                expect (implemented.contains ("`" + juce::String (d.id) + "`"), "not documented: " + juce::String (d.id));

            // 実装済み表の行数がレジストリと一致する (ドキュメントだけに残った古い ID を検出)
            int rows = 0;
            for (const auto& line : juce::StringArray::fromLines (implemented))
                if (line.trimStart().startsWith ("| `"))
                    ++rows;

            expectEquals (rows, static_cast<int> (definitions.size()));
        }
    }
};

ParameterRegistryTests parameterRegistryTests;
} // namespace
