#include "PluginProcessor.h"
#include "TestHelpers.h"
#include "state/Parameters.h"
#include "state/StateSerializer.h"

namespace
{
class StateTests final : public juce::UnitTest
{
public:
    StateTests() : juce::UnitTest ("State", "state") {}

    void runTest() override
    {
        namespace ids = polybrisk::params::id;

        beginTest ("Host state roundtrip restores every parameter");
        {
            PolyBriskAudioProcessor source;
            setValue (source, ids::oscA.wave, 5.0f);   // Sine
            setValue (source, ids::outputGain, -7.5f);

            juce::MemoryBlock block;
            source.getStateInformation (block);
            expect (block.getSize() > 0);

            PolyBriskAudioProcessor restored;
            restored.setStateInformation (block.getData(), static_cast<int> (block.getSize()));

            for (const auto& d : polybrisk::params::all())
                expectWithinAbsoluteError (getValue (restored, d.id), getValue (source, d.id), 1.0e-4f);

            expectWithinAbsoluteError (getValue (restored, ids::oscA.wave), 5.0f, 1.0e-6f);
            expectWithinAbsoluteError (getValue (restored, ids::outputGain), -7.5f, 1.0e-4f);

            // 再保存しても同一のバイト列になる
            juce::MemoryBlock second;
            restored.getStateInformation (second);
            expect (second == block);
        }

        beginTest ("Every parameter survives a roundtrip with non-default values");
        {
            PolyBriskAudioProcessor source;
            int index = 0;

            for (const auto& d : polybrisk::params::all())
            {
                // パラメーターごとに異なる、既定値ではない正規化値を設定する
                auto* param = source.getValueTreeState().getParameter (d.id);
                const float normalised = 0.15f + 0.7f * static_cast<float> ((index * 37) % 100) / 100.0f;
                param->setValueNotifyingHost (normalised);
                ++index;
            }

            juce::MemoryBlock block;
            source.getStateInformation (block);

            PolyBriskAudioProcessor restored;
            restored.setStateInformation (block.getData(), static_cast<int> (block.getSize()));

            int changedFromDefault = 0;
            for (const auto& d : polybrisk::params::all())
            {
                const float expected = getValue (source, d.id);
                expectWithinAbsoluteError (getValue (restored, d.id), expected, std::abs (expected) * 1.0e-5f + 1.0e-4f, d.id);

                if (std::abs (expected - d.defaultValue) > 1.0e-3f)
                    ++changedFromDefault;
            }

            // テスト自体が既定値のままになっていないこと
            // (ON/OFF は 2 値なので、既定値と同じ側になるものがある)
            expect (changedFromDefault >= static_cast<int> (polybrisk::params::all().size()) * 3 / 4);
        }

        beginTest ("Restored state renders the same audio");
        {
            PolyBriskAudioProcessor source;
            setValue (source, ids::oscA.wave, 1.0f);
            setValue (source, ids::oscA.unisonVoices, 7.0f);
            setValue (source, ids::oscB.enabled, 1.0f);
            setValue (source, ids::oscB.wave, 2.0f);
            setValue (source, ids::oscB.pulseWidth, 30.0f);
            setValue (source, ids::ampAttack, 20.0f);
            setValue (source, ids::outputGain, -12.0f);

            juce::MemoryBlock block;
            source.getStateInformation (block);

            PolyBriskAudioProcessor restored;
            restored.setStateInformation (block.getData(), static_cast<int> (block.getSize()));

            const std::vector<polybrisk::test::TimedMidi> events { { 0, juce::MidiMessage::noteOn (1, 64, 0.9f) } };
            const auto a = polybrisk::test::renderProcessor (source, 48000.0, { 256 }, 4800, events);
            const auto b = polybrisk::test::renderProcessor (restored, 48000.0, { 256 }, 4800, events);

            float maxDiff = 0.0f;
            for (int i = 0; i < 4800; ++i)
                maxDiff = std::max (maxDiff, std::abs (a.getSample (0, i) - b.getSample (0, i)));

            expect (maxDiff < 1.0e-6f);
            expect (polybrisk::test::peakLevel (a) > 0.01f);
        }

        beginTest ("Corrupt / empty / foreign state is rejected without changing parameters");
        {
            PolyBriskAudioProcessor processor;
            setValue (processor, ids::outputGain, -3.0f);

            processor.setStateInformation (nullptr, 0);

            const char garbage[] = "this is not a plugin state \x01\x02\x03\xff";
            processor.setStateInformation (garbage, static_cast<int> (sizeof (garbage)));

            juce::Random random (1234);
            juce::MemoryBlock noise (4096);
            for (size_t i = 0; i < noise.getSize(); ++i)
                noise[i] = static_cast<char> (random.nextInt (256));
            processor.setStateInformation (noise.getData(), static_cast<int> (noise.getSize()));

            juce::MemoryBlock foreign;
            juce::XmlElement other ("SOME_OTHER_PLUGIN");
            juce::AudioProcessor::copyXmlToBinary (other, foreign);
            processor.setStateInformation (foreign.getData(), static_cast<int> (foreign.getSize()));

            // 正常な state を途中で切ったもの
            juce::MemoryBlock valid;
            processor.getStateInformation (valid);
            processor.setStateInformation (valid.getData(), static_cast<int> (valid.getSize() / 2));

            expectWithinAbsoluteError (getValue (processor, ids::outputGain), -3.0f, 1.0e-4f);
        }

        beginTest ("Unsupported stateVersion is rejected");
        {
            PolyBriskAudioProcessor processor;
            auto& apvts = processor.getValueTreeState();
            setValue (processor, ids::outputGain, -3.0f);

            auto tree = polybrisk::state::toTree (apvts, processor.getMetaState());
            tree.getChildWithName (polybrisk::state::parametersType)
                .getChildWithProperty ("id", juce::String (ids::outputGain))
                .setProperty ("value", 6.0, nullptr);

            auto future = tree.createCopy();
            future.setProperty (polybrisk::state::versionProperty, polybrisk::state::currentStateVersion + 1, nullptr);
            expect (! polybrisk::state::fromTree (future, apvts, processor.getMetaState()));

            auto missing = tree.createCopy();
            missing.removeProperty (polybrisk::state::versionProperty, nullptr);
            expect (! polybrisk::state::fromTree (missing, apvts, processor.getMetaState()));

            expectWithinAbsoluteError (getValue (processor, ids::outputGain), -3.0f, 1.0e-4f);

            expect (polybrisk::state::fromTree (tree, apvts, processor.getMetaState()));
            expectWithinAbsoluteError (getValue (processor, ids::outputGain), 6.0f, 1.0e-4f);
        }

        beginTest ("Missing parameters fall back to defaults; unknown fields are ignored; values are clamped");
        {
            PolyBriskAudioProcessor processor;
            auto& apvts = processor.getValueTreeState();
            setValue (processor, ids::oscA.wave, 5.0f);
            setValue (processor, ids::outputGain, -20.0f);

            juce::ValueTree tree { polybrisk::state::rootType };
            tree.setProperty (polybrisk::state::versionProperty, 1, nullptr);
            tree.setProperty ("futureProperty", "ignored", nullptr);

            juce::ValueTree parameters { polybrisk::state::parametersType };
            parameters.appendChild (makeParam ("output.gain", 999.0), nullptr);          // 範囲外
            parameters.appendChild (makeParam ("some.future.parameter", 0.5), nullptr);  // 未知
            parameters.appendChild (makeParam ("osc.b.unisonVoices", -40.0), nullptr);   // 範囲外 (下限)
            parameters.appendChild (makeParam ("amp.release", std::numeric_limits<double>::quiet_NaN()), nullptr); // 非有限
            tree.appendChild (parameters, nullptr);
            tree.appendChild (juce::ValueTree { "FUTURE_SECTION" }, nullptr);

            expect (polybrisk::state::fromTree (tree, apvts, processor.getMetaState()));

            const auto* wave = polybrisk::params::find (ids::oscA.wave);
            const auto* gain = polybrisk::params::find (ids::outputGain);
            expect (wave != nullptr && gain != nullptr);

            expectWithinAbsoluteError (getValue (processor, ids::oscA.wave), wave->defaultValue, 1.0e-6f);
            expectWithinAbsoluteError (getValue (processor, ids::outputGain), gain->max, 1.0e-4f);
            expectWithinAbsoluteError (getValue (processor, ids::oscB.unisonVoices), 1.0f, 1.0e-6f);
            expectWithinAbsoluteError (getValue (processor, ids::ampRelease), polybrisk::params::find (ids::ampRelease)->defaultValue, 1.0e-3f);

            for (const auto& d : polybrisk::params::all())
                if (juce::String (d.id) != ids::outputGain && juce::String (d.id) != ids::oscB.unisonVoices)
                    expectWithinAbsoluteError (getValue (processor, d.id), d.defaultValue, 1.0e-3f, d.id);
        }
    }

private:
    static juce::ValueTree makeParam (const juce::String& parameterId, double value)
    {
        juce::ValueTree p { "PARAM" };
        p.setProperty ("id", parameterId, nullptr);
        p.setProperty ("value", value, nullptr);
        return p;
    }

    static void setValue (PolyBriskAudioProcessor& processor, const char* parameterId, float realValue)
    {
        auto* param = processor.getValueTreeState().getParameter (parameterId);
        jassert (param != nullptr);
        param->setValueNotifyingHost (param->convertTo0to1 (realValue));
    }

    static float getValue (PolyBriskAudioProcessor& processor, const char* parameterId)
    {
        return processor.getValueTreeState().getRawParameterValue (parameterId)->load();
    }
};

StateTests stateTests;
} // namespace
