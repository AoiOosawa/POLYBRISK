#include "PluginProcessor.h"
#include "TestHelpers.h"
#include "state/Parameters.h"

namespace
{
using polybrisk::test::TimedMidi;
namespace ids = polybrisk::params::id;

void setParam (PolyBriskAudioProcessor& processor, const char* parameterId, float realValue)
{
    auto* param = processor.getValueTreeState().getParameter (parameterId);
    jassert (param != nullptr);
    param->setValueNotifyingHost (param->convertTo0to1 (realValue));
}

/** テスト用の短いエンベロープ (既定の Release 200 ms では無音確認に時間がかかるため)。 */
void useShortEnvelope (PolyBriskAudioProcessor& processor)
{
    setParam (processor, ids::ampAttack, 1.0f);
    setParam (processor, ids::ampDecay, 20.0f);
    setParam (processor, ids::ampSustain, 100.0f);
    setParam (processor, ids::ampRelease, 10.0f);
}

class ProcessorMidiTests final : public juce::UnitTest
{
public:
    ProcessorMidiTests() : juce::UnitTest ("Processor MIDI", "midi") {}

    void runTest() override
    {
        beginTest ("Output meter: reports the peak since the last read, then starts again from zero (D-071)");
        {
            PolyBriskAudioProcessor processor;
            processor.setRateAndBufferSizeDetails (48000.0, 256);
            processor.prepareToPlay (48000.0, 256);
            juce::AudioBuffer<float> buffer (2, 256);
            juce::MidiBuffer midi;

            processor.processBlock (buffer, midi);
            expectEquals (processor.takeOutputPeak (0), 0.0f);      // 無音
            expectEquals (processor.takeOutputPeak (1), 0.0f);

            midi.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 0);
            float loudest = 0.0f;

            for (int block = 0; block < 40; ++block)
            {
                processor.processBlock (buffer, midi);
                midi.clear();
                loudest = std::max (loudest, buffer.getMagnitude (0, 0, 256));
            }

            // 読むまでの間のいちばん大きいブロックのピークが残っている
            const float peak = processor.takeOutputPeak (0);
            expectGreaterThan (peak, 0.01f);
            expectWithinAbsoluteError (peak, loudest, 1.0e-6f);
            expectGreaterThan (processor.takeOutputPeak (1), 0.01f);

            // 読んだら 0 に戻る
            expectEquals (processor.takeOutputPeak (0), 0.0f);
        }

        beginTest ("Bus layout: stereo out, no input");
        {
            PolyBriskAudioProcessor processor;
            expectEquals (processor.getTotalNumInputChannels(), 0);
            expectEquals (processor.getTotalNumOutputChannels(), 2);
            expect (processor.acceptsMidi());
            expect (! processor.producesMidi());
        }

        beginTest ("Note on at a sample offset starts exactly at that offset");
        {
            PolyBriskAudioProcessor processor;
            const int offset = 100;
            const auto out = polybrisk::test::renderProcessor (
                processor, 48000.0, { 512 }, 2048,
                { { offset, juce::MidiMessage::noteOn (1, 69, 1.0f) } });

            expectEquals (polybrisk::test::peakLevel (out, 0, offset), 0.0f);
            expect (polybrisk::test::peakLevel (out, offset, 256) > 0.01f);
        }

        beginTest ("Default patch: correct pitch through the processor; centred (L == R)");
        {
            PolyBriskAudioProcessor processor;
            const auto out = polybrisk::test::renderProcessor (
                processor, 48000.0, { 256 }, 48000,
                { { 0, juce::MidiMessage::noteOn (1, 69, 0.8f) } });

            const double f = polybrisk::test::estimateFrequency (out.getReadPointer (0), 48000, 48000.0);
            expectWithinAbsoluteError (f, 440.0, 0.25);

            bool identical = true;
            for (int i = 0; i < out.getNumSamples(); ++i)
                identical = identical && juce::exactlyEqual (out.getSample (0, i), out.getSample (1, i));
            expect (identical);
        }

        beginTest ("Note off and velocity-0 note on both silence the note");
        {
            for (const bool useVelocityZero : { false, true })
            {
                PolyBriskAudioProcessor processor;
                useShortEnvelope (processor);

                // Velocity 0 の Note On を生バイトで作る (0x90 nn 00)
                const auto off = useVelocityZero ? juce::MidiMessage (0x90, 60, 0)
                                                 : juce::MidiMessage::noteOff (1, 60);

                const auto out = polybrisk::test::renderProcessor (
                    processor, 48000.0, { 128 }, 24000,
                    { { 0, juce::MidiMessage::noteOn (1, 60, 1.0f) }, { 9600, off } });

                expect (polybrisk::test::peakLevel (out, 4800, 4800) > 0.05f);
                expectEquals (polybrisk::test::peakLevel (out, 9600 + 2400, 24000 - 12000), 0.0f);
            }
        }

        beginTest ("Sustain pedal (CC64) through MIDI");
        {
            PolyBriskAudioProcessor processor;
            useShortEnvelope (processor);

            const auto out = polybrisk::test::renderProcessor (
                processor, 48000.0, { 128 }, 48000,
                { { 0, juce::MidiMessage::noteOn (1, 60, 1.0f) },
                  { 2400, juce::MidiMessage::controllerEvent (1, 64, 127) },
                  { 4800, juce::MidiMessage::noteOff (1, 60) },
                  { 24000, juce::MidiMessage::controllerEvent (1, 64, 0) } });

            expect (polybrisk::test::peakLevel (out, 12000, 9600) > 0.05f);     // ペダルで保持
            expectEquals (polybrisk::test::peakLevel (out, 26400, 9600), 0.0f); // ペダルを離すと消える
        }

        beginTest ("All Notes Off (CC123) and All Sound Off (CC120) silence");
        {
            for (const int controller : { 123, 120 })
            {
                PolyBriskAudioProcessor processor;
                useShortEnvelope (processor);

                const auto out = polybrisk::test::renderProcessor (
                    processor, 48000.0, { 128 }, 24000,
                    { { 0, juce::MidiMessage::noteOn (1, 60, 1.0f) },
                      { 100, juce::MidiMessage::noteOn (1, 67, 1.0f) },
                      { 9600, juce::MidiMessage::controllerEvent (1, controller, 0) } });

                expect (polybrisk::test::peakLevel (out, 4800, 4800) > 0.05f);
                expectEquals (polybrisk::test::peakLevel (out, 12000, 12000), 0.0f);
            }
        }

        beginTest ("Parameters reach the engine (OSC B, unison, tuning)");
        {
            PolyBriskAudioProcessor processor;
            setParam (processor, ids::oscA.wave, 5.0f); // Sine
            setParam (processor, ids::oscA.octave, 1.0f);
            setParam (processor, ids::oscA.semitone, 7.0f);

            const auto tuned = polybrisk::test::renderProcessor (
                processor, 48000.0, { 256 }, 48000, { { 0, juce::MidiMessage::noteOn (1, 57, 1.0f) } });
            const double expected = 220.0 * std::exp2 (19.0 / 12.0);
            expectWithinAbsoluteError (polybrisk::test::estimateFrequency (tuned.getReadPointer (0), 48000, 48000.0), expected, expected * 0.0005);

            PolyBriskAudioProcessor wide;
            setParam (wide, ids::oscA.wave, 1.0f); // SuperSAW
            setParam (wide, ids::oscA.unisonVoices, 7.0f);
            setParam (wide, ids::oscA.spread, 100.0f);
            setParam (wide, ids::oscB.enabled, 1.0f);

            const auto out = polybrisk::test::renderProcessor (
                wide, 48000.0, { 256 }, 48000, { { 0, juce::MidiMessage::noteOn (1, 57, 1.0f) } });

            float diff = 0.0f;
            for (int i = 0; i < 48000; ++i)
                diff = std::max (diff, std::abs (out.getSample (0, i) - out.getSample (1, i)));
            expect (diff > 0.05f);
        }

        beginTest ("Sample rate x block size matrix: 16 voices x 2 OSC x 7 unison, finite, bounded, block-size independent");
        {
            const std::vector<std::vector<int>> blockPatterns {
                { 32 }, { 64 }, { 128 }, { 256 }, { 512 }, { 1024 }, { 2048 },
                { 1, 37, 512, 3, 64, 2048, 129 } // 可変ブロック長
            };

            for (const double sampleRate : { 44100.0, 48000.0, 88200.0, 96000.0 })
            {
                juce::AudioBuffer<float> reference;

                for (const auto& pattern : blockPatterns)
                {
                    PolyBriskAudioProcessor processor;
                    for (const auto* o : { &ids::oscA, &ids::oscB })
                    {
                        setParam (processor, o->enabled, 1.0f);
                        setParam (processor, o->wave, 1.0f);
                        setParam (processor, o->unisonVoices, 7.0f);
                    }
                    setParam (processor, ids::oscB.octave, 1.0f);
                    setParam (processor, ids::ampRelease, 50.0f);

                    const int total = static_cast<int> (sampleRate * 0.4);
                    std::vector<TimedMidi> events;

                    // 18 音 (16 音を超えてスティールが起きる)、極端な音域、ペダル、同時刻イベントを含む
                    for (int i = 0; i < 18; ++i)
                        events.push_back ({ 10 + i * 3, juce::MidiMessage::noteOn (1, i == 0 ? 0 : (i == 1 ? 127 : 36 + i * 4), 0.9f) });

                    events.push_back ({ total / 4, juce::MidiMessage::controllerEvent (1, 64, 127) });
                    for (int i = 2; i < 10; ++i)
                        events.push_back ({ total / 4 + 5, juce::MidiMessage::noteOff (1, 36 + i * 4) });
                    events.push_back ({ total / 2, juce::MidiMessage::controllerEvent (1, 64, 0) });
                    for (int i = 0; i < 6; ++i)
                        events.push_back ({ total / 2 + 333, juce::MidiMessage::noteOn (1, 50 + i * 5, 1.0f) });
                    events.push_back ({ (total * 3) / 4, juce::MidiMessage::allNotesOff (1) });

                    const auto out = polybrisk::test::renderProcessor (processor, sampleRate, pattern, total, events);

                    expect (polybrisk::test::allFinite (out));
                    expect (polybrisk::test::peakLevel (out) <= 1.0f);
                    expect (polybrisk::test::peakLevel (out) > 0.01f);

                    if (reference.getNumSamples() == 0)
                    {
                        reference = out;
                    }
                    else
                    {
                        // ブロック分割に依存せず同一の出力になること
                        float maxDiff = 0.0f;
                        for (int ch = 0; ch < 2; ++ch)
                            for (int i = 0; i < total; ++i)
                                maxDiff = std::max (maxDiff, std::abs (out.getSample (ch, i) - reference.getSample (ch, i)));

                        expect (maxDiff < 1.0e-6f, "block-size dependent output, diff " + juce::String (maxDiff));
                    }
                }
            }
        }

        beginTest ("prepareToPlay again (sample-rate change, stop/restart) clears hanging notes");
        {
            PolyBriskAudioProcessor processor;
            const std::vector<TimedMidi> events { { 0, juce::MidiMessage::noteOn (1, 60, 1.0f) } };
            const auto first = polybrisk::test::renderProcessor (processor, 44100.0, { 256 }, 4410, events);
            expect (polybrisk::test::peakLevel (first) > 0.01f);

            const auto second = polybrisk::test::renderProcessor (processor, 96000.0, { 512 }, 9600, {});
            expectEquals (polybrisk::test::peakLevel (second), 0.0f);
        }

        beginTest ("Empty and mono-channel buffers do not crash");
        {
            PolyBriskAudioProcessor processor;
            processor.prepareToPlay (48000.0, 512);

            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 0);

            juce::AudioBuffer<float> empty (2, 0);
            processor.processBlock (empty, midi);

            juce::AudioBuffer<float> mono (1, 64);
            processor.processBlock (mono, midi);
            expect (polybrisk::test::allFinite (mono));
        }
    }
};

ProcessorMidiTests processorMidiTests;
} // namespace
