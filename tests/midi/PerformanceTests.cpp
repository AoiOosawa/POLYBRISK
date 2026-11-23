#include "PluginProcessor.h"
#include "TestHelpers.h"
#include "state/Parameters.h"
#include "synth/SynthEngine.h"

#include <cmath>
#include <vector>

namespace
{
using polybrisk::EngineParams;
using polybrisk::SynthEngine;
using polybrisk::Waveform;
using polybrisk::test::TimedMidi;
namespace ids = polybrisk::params::id;

EngineParams sinePatch()
{
    EngineParams p;
    p.oscA.waveform = Waveform::sine;
    p.amp = { 0.001f, 0.05f, 1.0f, 0.02f };
    return p;
}

juce::AudioBuffer<float> render (SynthEngine& engine, int numSamples)
{
    juce::AudioBuffer<float> out (2, numSamples);
    for (int pos = 0; pos < numSamples; pos += 128)
        engine.render (out.getWritePointer (0, pos), out.getWritePointer (1, pos), std::min (128, numSamples - pos));
    return out;
}

/** 立ち上がりゼロクロスの間隔から、周期ごとの瞬時周波数を求める。 */
std::vector<std::pair<double, double>> instantaneousFrequency (const float* x, int n, double sampleRate)
{
    std::vector<double> crossings;
    for (int i = 1; i < n; ++i)
        if (x[i - 1] < 0.0f && x[i] >= 0.0f)
            crossings.push_back ((i - 1) + static_cast<double> (-x[i - 1]) / static_cast<double> (x[i] - x[i - 1]));

    std::vector<std::pair<double, double>> result; // (時刻 [s], 周波数 [Hz])
    for (size_t i = 1; i < crossings.size(); ++i)
        result.push_back ({ 0.5 * (crossings[i] + crossings[i - 1]) / sampleRate, sampleRate / (crossings[i] - crossings[i - 1]) });
    return result;
}

double centsBetween (double f, double reference) { return 1200.0 * std::log2 (f / reference); }

void setParam (PolyBriskAudioProcessor& processor, const char* parameterId, float realValue)
{
    auto* param = processor.getValueTreeState().getParameter (parameterId);
    param->setValueNotifyingHost (param->convertTo0to1 (realValue));
}

class PerformanceTests final : public juce::UnitTest
{
public:
    PerformanceTests() : juce::UnitTest ("Performance (bend / vibrato)", "midi") {}

    void runTest() override
    {
        const double sr = 48000.0;
        const int note = 69;
        const double base = SynthEngine::midiNoteToHz (note);

        beginTest ("14-bit pitch wheel maps to exactly -1 / 0 / +1");
        expectEquals (SynthEngine::pitchWheelToNormalised (0), -1.0f);
        expectEquals (SynthEngine::pitchWheelToNormalised (8192), 0.0f);
        expectEquals (SynthEngine::pitchWheelToNormalised (16383), 1.0f);
        expectWithinAbsoluteError (SynthEngine::pitchWheelToNormalised (4096), -0.5f, 1.0e-6f);

        beginTest ("Pitch Bend: ranges 0 / 2 / 12 / 48 semitones reach the exact interval at both ends");
        for (const double rate : { 44100.0, 48000.0, 96000.0 })
        {
            for (const int range : { 0, 2, 12, 48 })
            {
                for (const float bend : { -1.0f, 0.0f, 1.0f, 0.5f })
                {
                    SynthEngine engine;
                    engine.prepare (rate);
                    auto p = sinePatch();
                    p.performance.bendUp = static_cast<float> (range);
                    p.performance.bendDown = static_cast<float> (range);
                    engine.setParams (p);
                    engine.noteOn (57, 1.0f); // 220 Hz (+48 st でも 3520 Hz)
                    engine.setPitchBend (bend);

                    const int n = static_cast<int> (rate);
                    const auto out = render (engine, n);
                    const double expected = 220.0 * std::exp2 (static_cast<double> (bend) * range / 12.0);
                    const double measured = polybrisk::test::estimateFrequency (out.getReadPointer (0, n / 2), n / 2, rate);
                    expectWithinAbsoluteError (centsBetween (measured, expected), 0.0, 1.0);
                }
            }
        }

        beginTest ("Pitch Bend: up and down ranges are independent");
        {
            const auto measure = [&] (float bend)
            {
                SynthEngine engine;
                engine.prepare (sr);
                auto p = sinePatch();
                p.performance.bendUp = 12.0f;
                p.performance.bendDown = 3.0f;
                engine.setParams (p);
                engine.noteOn (note, 1.0f);
                engine.setPitchBend (bend);
                const auto out = render (engine, 48000);
                return polybrisk::test::estimateFrequency (out.getReadPointer (0, 24000), 24000, sr);
            };

            expectWithinAbsoluteError (centsBetween (measure (1.0f), base), 1200.0, 1.0);
            expectWithinAbsoluteError (centsBetween (measure (-1.0f), base), -300.0, 1.0);
        }

        beginTest ("Pitch Bend keeps the OSC A / B interval and is smoothed (no pitch jump)");
        {
            SynthEngine engine;
            engine.prepare (sr);
            auto p = sinePatch();
            p.oscA.enabled = false;
            p.oscB.enabled = true;
            p.oscB.waveform = Waveform::sine;
            p.oscB.semitone = 7;
            p.oscB.fineCents = 20.0f;
            p.oscB.level = 1.0f;
            p.performance.bendUp = 12.0f;
            engine.setParams (p);
            engine.noteOn (57, 1.0f);
            render (engine, 9600);
            engine.setPitchBend (1.0f);
            const auto out = render (engine, 48000);

            const double expected = 220.0 * std::exp2 ((12.0 + 7.2) / 12.0);
            expectWithinAbsoluteError (centsBetween (polybrisk::test::estimateFrequency (out.getReadPointer (0, 24000), 24000, sr), expected), 0.0, 1.0);

            // ベンド直後: 周波数は段階的に上がり、数 ms かけて目標へ近づく
            const auto track = instantaneousFrequency (out.getReadPointer (0), 4800, sr);
            double previous = 0.0;
            bool monotonic = true;
            for (const auto& [time, frequency] : track)
            {
                monotonic = monotonic && frequency > previous - 1.0;
                previous = frequency;
            }
            expect (monotonic);
            expect (track.front().second < expected * 0.8);         // 1 周期目はまだ途中
            expectWithinAbsoluteError (centsBetween (track.back().second, expected), 0.0, 2.0);
        }

        beginTest ("Vibrato: CC1 = 0 gives none; CC1 = 127 reaches the set depth at the set rate");
        for (const double rate : { 44100.0, 48000.0, 96000.0 })
        {
            const auto analyse = [&] (float modWheel, float depthCents, float lfoRate)
            {
                SynthEngine engine;
                engine.prepare (rate);
                auto p = sinePatch();
                p.performance.vibratoDepthCents = depthCents;
                p.performance.vibratoRateHz = lfoRate;
                engine.setParams (p);
                engine.setModWheel (modWheel);
                engine.noteOn (93, 1.0f); // 1760 Hz: 周期が短く、LFO を細かく追える

                const int n = static_cast<int> (rate * 2.2);
                const auto out = render (engine, n);
                const int skip = static_cast<int> (rate * 0.2);
                const auto track = instantaneousFrequency (out.getReadPointer (0, skip), n - skip, rate);

                double low = 1.0e9, high = 0.0, mean = 0.0;
                for (const auto& [time, frequency] : track)
                {
                    low = std::min (low, frequency);
                    high = std::max (high, frequency);
                    mean += frequency;
                }
                mean /= static_cast<double> (track.size());

                // 中心を上向きに横切る回数 = LFO の周期数
                int cycles = 0;
                for (size_t i = 1; i < track.size(); ++i)
                    if (track[i - 1].second < mean && track[i].second >= mean)
                        ++cycles;

                const double nominal = SynthEngine::midiNoteToHz (93);
                struct Result { double up, down; int cycles; };
                return Result { centsBetween (high, nominal), centsBetween (low, nominal), cycles };
            };

            const auto none = analyse (0.0f, 200.0f, 6.0f);
            expectWithinAbsoluteError (none.up, 0.0, 0.5);
            expectWithinAbsoluteError (none.down, 0.0, 0.5);

            const auto full = analyse (1.0f, 100.0f, 5.0f);
            expectWithinAbsoluteError (full.up, 100.0, 3.0);
            expectWithinAbsoluteError (full.down, -100.0, 3.0);
            expectWithinAbsoluteError (static_cast<double> (full.cycles), 10.0, 1.0); // 2 秒間 × 5 Hz

            const auto half = analyse (0.5f, 100.0f, 10.0f);
            expectWithinAbsoluteError (half.up, 50.0, 2.5);
            expectWithinAbsoluteError (static_cast<double> (half.cycles), 20.0, 1.0);

            const auto maximum = analyse (1.0f, 200.0f, 12.0f);
            expectWithinAbsoluteError (maximum.up, 200.0, 8.0);
        }

        beginTest ("Vibrato Delay: no vibrato before the delay, fades in afterwards, per voice");
        {
            SynthEngine engine;
            engine.prepare (sr);
            auto p = sinePatch();
            p.performance.vibratoDepthCents = 100.0f;
            p.performance.vibratoRateHz = 8.0f;
            p.performance.vibratoDelay = 0.5f;
            engine.setParams (p);
            engine.setModWheel (1.0f);
            engine.noteOn (93, 1.0f);

            const auto out = render (engine, 96000);
            const double nominal = SynthEngine::midiNoteToHz (93);

            double before = 0.0, during = 0.0, after = 0.0;
            for (const auto& [time, frequency] : instantaneousFrequency (out.getReadPointer (0), 96000, sr))
            {
                const double cents = std::abs (centsBetween (frequency, nominal));
                if (time > 0.02 && time < 0.49) before = std::max (before, cents);
                if (time > 0.5 && time < 0.62)  during = std::max (during, cents);
                if (time > 1.0)                 after = std::max (after, cents);
            }

            expect (before < 0.5);
            expect (during > 2.0 && during < 60.0);     // 立ち上がりの途中
            expectWithinAbsoluteError (after, 100.0, 3.0);
        }

        beginTest ("Bend and vibrato add in cents");
        {
            SynthEngine engine;
            engine.prepare (sr);
            auto p = sinePatch();
            p.performance.bendUp = 12.0f;
            p.performance.vibratoDepthCents = 50.0f;
            engine.setParams (p);
            engine.setModWheel (1.0f);
            engine.setPitchBend (1.0f);
            engine.noteOn (81, 1.0f);

            const auto out = render (engine, 96000);
            const double centre = SynthEngine::midiNoteToHz (93);
            double low = 1.0e9, high = 0.0;
            for (const auto& [time, frequency] : instantaneousFrequency (out.getReadPointer (0, 9600), 86400, sr))
            {
                low = std::min (low, frequency);
                high = std::max (high, frequency);
            }

            expectWithinAbsoluteError (centsBetween (high, centre), 50.0, 2.5);
            expectWithinAbsoluteError (centsBetween (low, centre), -50.0, 2.5);
        }

        beginTest ("Processor: MIDI pitch wheel and CC1, Symmetric / Independent bend mode");
        {
            const auto measure = [&] (int wheel, float up, float down, int mode)
            {
                PolyBriskAudioProcessor processor;
                setParam (processor, ids::oscA.wave, 5.0f); // Sine
                setParam (processor, ids::filterCharacter, 1.0f);
                setParam (processor, ids::ampSustain, 100.0f);
                setParam (processor, ids::bendUp, up);
                setParam (processor, ids::bendDown, down);
                setParam (processor, ids::bendMode, static_cast<float> (mode));

                const auto out = polybrisk::test::renderProcessor (
                    processor, sr, { 256 }, 48000,
                    { { 0, juce::MidiMessage::pitchWheel (1, wheel) }, { 0, juce::MidiMessage::noteOn (1, note, 1.0f) } });
                return centsBetween (polybrisk::test::estimateFrequency (out.getReadPointer (0, 24000), 24000, sr), base);
            };

            expectWithinAbsoluteError (measure (8192, 12.0f, 12.0f, 0), 0.0, 1.0);
            expectWithinAbsoluteError (measure (16383, 2.0f, 2.0f, 0), 200.0, 1.0);     // 既定値 ±2
            expectWithinAbsoluteError (measure (0, 2.0f, 2.0f, 0), -200.0, 1.0);
            expectWithinAbsoluteError (measure (16383, 7.0f, 24.0f, 1), 700.0, 1.0);    // Independent
            expectWithinAbsoluteError (measure (0, 7.0f, 24.0f, 1), -2400.0, 1.0);
            expectWithinAbsoluteError (measure (0, 7.0f, 24.0f, 0), -700.0, 1.0);       // Symmetric: 下も Bend Up の値

            // CC1 → Vibrato
            PolyBriskAudioProcessor processor;
            setParam (processor, ids::oscA.wave, 5.0f);
            setParam (processor, ids::filterCharacter, 1.0f);
            setParam (processor, ids::ampSustain, 100.0f);
            setParam (processor, ids::vibratoDepth, 100.0f);

            const auto out = polybrisk::test::renderProcessor (
                processor, sr, { 256 }, 96000,
                { { 0, juce::MidiMessage::noteOn (1, 93, 1.0f) }, { 48000, juce::MidiMessage::controllerEvent (1, 1, 127) } });

            double firstHalf = 0.0, secondHalf = 0.0;
            for (const auto& [time, frequency] : instantaneousFrequency (out.getReadPointer (0), 96000, sr))
            {
                const double cents = std::abs (centsBetween (frequency, SynthEngine::midiNoteToHz (93)));
                if (time > 0.05 && time < 0.99) firstHalf = std::max (firstHalf, cents);
                if (time > 1.2)                 secondHalf = std::max (secondHalf, cents);
            }

            expect (firstHalf < 0.5);
            expectWithinAbsoluteError (secondHalf, 100.0, 3.0);
        }
    }
};

PerformanceTests performanceTests;
} // namespace
