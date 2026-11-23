#include "TestHelpers.h"
#include "synth/Oscillator.h"

#include <functional>
#include <vector>

namespace
{
namespace osc = polybrisk::osc;

class OscillatorTests final : public juce::UnitTest
{
public:
    OscillatorTests() : juce::UnitTest ("Oscillator waveforms", "dsp") {}

    using Generator = std::function<double (double phase, double inc)>;

    void runTest() override
    {
        const std::vector<std::pair<juce::String, Generator>> waves {
            { "Saw", [] (double p, double inc) { return osc::saw (p, inc); } },
            { "Square", [] (double p, double inc) { return osc::pulse (p, inc, 0.5); } },
            { "Pulse 25%", [] (double p, double inc) { return osc::pulse (p, inc, 0.25); } },
            { "Triangle", [] (double p, double inc) { return osc::triangle (p, inc); } },
            { "Sine", [] (double p, double) { return osc::sine (p); } },
        };

        for (const auto& [name, generator] : waves)
        {
            beginTest (name + ": pitch, DC and peak across the range");

            for (const double sampleRate : { 44100.0, 48000.0, 96000.0 })
            {
                for (const double frequency : { 27.5, 110.0, 440.0, 1760.0, 7040.0 })
                {
                    const int numSamples = static_cast<int> (sampleRate * 2.0);
                    const auto data = generate (generator, frequency / sampleRate, numSamples);

                    const double measured = polybrisk::test::estimateFrequency (data.data(), numSamples, sampleRate);
                    expectWithinAbsoluteError (measured, frequency, frequency * 0.0005,
                                               name + " " + juce::String (frequency) + " Hz @ " + juce::String (sampleRate));

                    expect (std::abs (polybrisk::test::dcOffset (data.data(), numSamples)) < 0.01, name + " DC");

                    float peak = 0.0f;
                    for (const float s : data)
                        peak = std::max (peak, std::abs (s));

                    // 高音域ではバンド制限により波形の角が丸まり、ピークが下がる
                    expect (peak > (frequency > 5000.0 ? 0.6f : 0.9f) && peak < 1.6f, name + " peak " + juce::String (peak));
                }
            }
        }

        beginTest ("Waveforms start near zero at phase 0 (except pulse)");
        {
            const double inc = 440.0 / 48000.0;
            expectWithinAbsoluteError (osc::saw (0.0, inc), 0.0, 1.0e-9);
            expectWithinAbsoluteError (osc::triangle (0.0, inc), 0.0, 1.0e-9);
            expectWithinAbsoluteError (osc::sine (0.0), 0.0, 1.0e-12);
            expect (osc::saw (0.01, inc) > 0.0);
            expect (osc::triangle (0.01, inc) > 0.0);
        }

        beginTest ("Pulse width sets the duty cycle");
        {
            const double inc = 100.0 / 48000.0;

            for (const double width : { 0.05, 0.1, 0.25, 0.5, 0.75, 0.95 })
            {
                const int numSamples = 96000;
                const auto data = generate ([width] (double p, double i) { return osc::pulse (p, i, width); }, inc, numSamples);

                // DC 除去前の符号で High 区間の割合を測る
                const double dc = 2.0 * width - 1.0;
                int high = 0;
                for (const float s : data)
                    if (s + dc > 0.0)
                        ++high;

                expectWithinAbsoluteError (static_cast<double> (high) / numSamples, width, 0.005);
                expect (std::abs (polybrisk::test::dcOffset (data.data(), numSamples)) < 0.01);
            }
        }

        beginTest ("Band-limiting: PolyBLEP / PolyBLAMP reduce audible-band aliasing versus naive shapes");
        {
            // ここでは発振器単体 (等倍レート) の素性を記録する。製品としての折り返し性能は
            // 2x オーバーサンプリング込みで HalfbandTests が検査する。
            const double sampleRate = 48000.0;
            const int order = 15;
            const int size = 1 << order;
            const double audible = 16000.0;

            struct Case { juce::String name; Generator limited; Generator naive; double minimumGainDb; };
            const std::vector<Case> cases {
                { "Saw", [] (double p, double inc) { return osc::saw (p, inc); },
                         [] (double p, double) { return 2.0 * osc::wrap01 (p + 0.5) - 1.0; }, 12.0 },
                { "Square", [] (double p, double inc) { return osc::pulse (p, inc, 0.5); },
                            [] (double p, double) { return p < 0.5 ? 1.0 : -1.0; }, 12.0 },
                { "Triangle", [] (double p, double inc) { return osc::triangle (p, inc); },
                              [] (double p, double) { return 4.0 * std::abs (osc::wrap01 (p + 0.75) - 0.5) - 1.0; }, 6.0 },
            };

            for (const double frequency : { 1244.5, 4186.01 }) // D#6, C8
            {
                for (const auto& c : cases)
                {
                    const auto limited = generate (c.limited, frequency / sampleRate, size);
                    const auto naive = generate (c.naive, frequency / sampleRate, size);
                    const double limitedDb = polybrisk::test::worstInharmonicDb (polybrisk::test::magnitudeSpectrum (limited.data(), order), sampleRate, frequency, audible);
                    const double naiveDb = polybrisk::test::worstInharmonicDb (polybrisk::test::magnitudeSpectrum (naive.data(), order), sampleRate, frequency, audible);

                    logMessage (c.name + " @ " + juce::String (frequency, 1) + " Hz (1x): band-limited " + juce::String (limitedDb, 1)
                                + " dB, naive " + juce::String (naiveDb, 1) + " dB");
                    expect (limitedDb < naiveDb - c.minimumGainDb, c.name + " band-limiting is not effective");
                }
            }

            const auto sine = generate ([] (double p, double) { return osc::sine (p); }, 4186.01 / sampleRate, size);
            expect (polybrisk::test::worstInharmonicDb (polybrisk::test::magnitudeSpectrum (sine.data(), order), sampleRate, 4186.01) < -90.0);
        }
    }

private:
    static std::vector<float> generate (const Generator& generator, double inc, int numSamples)
    {
        std::vector<float> out (static_cast<size_t> (numSamples));
        double phase = 0.0;

        for (auto& s : out)
        {
            s = static_cast<float> (generator (phase, inc));
            phase += inc;
            if (phase >= 1.0)
                phase -= 1.0;
        }

        return out;
    }
};

OscillatorTests oscillatorTests;
} // namespace
