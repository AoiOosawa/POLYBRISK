#include "TestHelpers.h"
#include "synth/Halfband.h"
#include "synth/SynthEngine.h"

#include <vector>

namespace
{
class HalfbandTests final : public juce::UnitTest
{
public:
    HalfbandTests() : juce::UnitTest ("Oversampling / halfband decimator", "dsp") {}

    void runTest() override
    {
        beginTest ("Passband is flat up to 20 kHz (at 44.1 kHz output)");
        {
            double worst = 0.0;
            for (const double hz : { 200.0, 1000.0, 5000.0, 10000.0, 15000.0, 18000.0, 20000.0 })
            {
                const double db = gainDb (hz / 88200.0);
                worst = std::max (worst, std::abs (db));
            }

            logMessage ("Worst passband deviation: " + juce::String (worst, 5) + " dB");
            expect (worst < 0.01);
        }

        beginTest ("Stopband rejects everything that would fold into the audible band");
        {
            double worst = -200.0;

            // 出力 44.1 kHz のとき、24.1 kHz 以上の成分が 20 kHz 以下へ折り返す
            for (double hz = 24200.0; hz <= 44000.0; hz += 247.0)
                worst = std::max (worst, gainDb (hz / 88200.0));

            logMessage ("Worst stopband gain: " + juce::String (worst, 1) + " dB");
            expect (worst < -95.0);
        }

        beginTest ("Reset clears the state; silence in gives silence out");
        {
            polybrisk::HalfbandDecimator d;
            for (int i = 0; i < 100; ++i)
                d.process (1.0f, -1.0f);

            d.reset();
            for (int i = 0; i < 100; ++i)
                expectEquals (d.process (0.0f, 0.0f), 0.0f);
        }

        beginTest ("Engine oversamples at 44.1/48 kHz and runs 1x at 88.2/96 kHz");
        {
            for (const double sampleRate : { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
            {
                polybrisk::SynthEngine engine;
                engine.prepare (sampleRate);
                expectEquals (engine.getOversamplingFactor(), sampleRate < 70000.0 ? 2 : 1);
            }
        }

        beginTest ("Aliasing through the engine: audible-band inharmonic content");
        {
            const int order = 15;
            const int size = 1 << order;

            for (const double sampleRate : { 44100.0, 48000.0, 96000.0 })
            {
                for (const auto wave : { polybrisk::Waveform::saw, polybrisk::Waveform::pulse, polybrisk::Waveform::square, polybrisk::Waveform::triangle })
                {
                    double worst = -200.0;

                    for (const int note : { 69, 87, 96, 108 }) // A4, D#6, C7, C8
                    {
                        polybrisk::SynthEngine engine;
                        engine.prepare (sampleRate);

                        polybrisk::EngineParams params;
                        params.oscA.waveform = wave;
                        params.oscA.pulseWidth = 0.25f;
                        params.amp = { 0.001f, 0.05f, 1.0f, 0.05f };
                        engine.setParams (params);
                        engine.noteOn (note, 1.0f);

                        std::vector<float> l (static_cast<size_t> (size + 9600)), r (l.size());
                        engine.render (l.data(), r.data(), static_cast<int> (l.size()));

                        const auto spectrum = polybrisk::test::magnitudeSpectrum (l.data() + 9600, order);
                        const double f0 = polybrisk::SynthEngine::midiNoteToHz (note);
                        worst = std::max (worst, polybrisk::test::worstInharmonicDb (spectrum, sampleRate, f0, 16000.0));
                    }

                    static const char* const names[] = { "Saw", "SuperSAW", "Pulse 25%", "Square", "Triangle", "Sine", "Noise" };
                    logMessage (juce::String (names[static_cast<int> (wave)]) + " @ " + juce::String (sampleRate)
                                + " Hz: worst inharmonic below 16 kHz (A4..C8) " + juce::String (worst, 1) + " dB re fundamental");
                    expect (worst < -50.0, "aliasing " + juce::String (worst, 1) + " dB");
                }
            }
        }
    }

private:
    /** 高レート側の正規化周波数 (0..0.5) の正弦波を通したときのゲイン (dB)。 */
    static double gainDb (double normalisedFrequency)
    {
        polybrisk::HalfbandDecimator d;
        const int numOut = 1 << 17;
        const int skip = 4096; // 過渡応答を捨てる
        const double w = polybrisk::osc::twoPi * normalisedFrequency;

        // 出力に現れる周波数 (折り返し後) との直交相関で振幅を求める
        double folded = 2.0 * normalisedFrequency;
        folded -= std::floor (folded);
        if (folded > 0.5)
            folded = 1.0 - folded;
        const double wOut = polybrisk::osc::twoPi * folded;

        double re = 0.0, im = 0.0;

        for (int i = 0; i < numOut; ++i)
        {
            const double y = d.process (static_cast<float> (std::sin (w * (2 * i))),
                                        static_cast<float> (std::sin (w * (2 * i + 1))));

            if (i >= skip)
            {
                re += y * std::cos (wOut * i);
                im += y * std::sin (wOut * i);
            }
        }

        const double amplitude = 2.0 * std::sqrt (re * re + im * im) / (numOut - skip);
        return juce::Decibels::gainToDecibels (amplitude, -200.0);
    }
};

HalfbandTests halfbandTests;
} // namespace
