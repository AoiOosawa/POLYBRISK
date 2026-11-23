#include "TestHelpers.h"
#include "synth/Unison.h"

#include <vector>

namespace
{
using polybrisk::UnisonLayout;
using polybrisk::UnisonOscillator;
using polybrisk::Waveform;

class UnisonTests final : public juce::UnitTest
{
public:
    UnisonTests() : juce::UnitTest ("Unison", "dsp") {}

    void runTest() override
    {
        beginTest ("Detune amount curve");
        {
            expectWithinAbsoluteError (polybrisk::detuneAmountToCents (0.0f), 0.0, 1.0e-9);
            expectWithinAbsoluteError (polybrisk::detuneAmountToCents (1.0f), 100.0, 1.0e-6);

            double previous = -1.0;
            for (int i = 0; i <= 100; ++i)
            {
                const double c = polybrisk::detuneAmountToCents (static_cast<float> (i) / 100.0f);
                expect (c > previous);
                previous = c;
            }
        }

        beginTest ("Layout: Unison Mix keeps the total power and trades centre against sides; 50 % is the plain layout (D-045)");
        {
            for (int n = 1; n <= polybrisk::maxUnisonVoices; ++n)
            {
                UnisonLayout plain, half;
                polybrisk::computeUnisonLayout (plain, n, 0.4f, 0.7f);
                polybrisk::computeUnisonLayout (half, n, 0.4f, 0.7f, 0.5f, false);

                for (int i = 0; i < n; ++i)
                {
                    const auto u = static_cast<size_t> (i);
                    expectWithinAbsoluteError (half.gainL[u], plain.gainL[u], 1.0e-6f);
                    expectWithinAbsoluteError (plain.gainL[u] * plain.gainL[u] + plain.gainR[u] * plain.gainR[u], 1.0f / static_cast<float> (n), 1.0e-5f);
                }

                const int centreCount = (n & 1) != 0 ? 1 : 2;

                for (const float mix : { 0.0f, 0.2f, 0.8f, 1.0f })
                {
                    UnisonLayout layout;
                    polybrisk::computeUnisonLayout (layout, n, 0.4f, 0.7f, mix, true);
                    const auto label = "n=" + juce::String (n) + " mix=" + juce::String (mix);
                    double total = 0.0, centre = 0.0;

                    for (int i = 0; i < n; ++i)
                    {
                        const auto u = static_cast<size_t> (i);
                        const double power = layout.gainL[u] * layout.gainL[u] + layout.gainR[u] * layout.gainR[u];
                        total += power;
                        if (i < centreCount)
                            centre += power;
                    }

                    expectWithinAbsoluteError (total, 1.0, 1.0e-5, label);

                    if (n > centreCount)
                    {
                        if (mix < 0.1f) expectWithinAbsoluteError (centre, 1.0, 1.0e-5, label);    // 中央だけ
                        if (mix > 0.9f) expectWithinAbsoluteError (centre, 0.0, 1.0e-5, label);    // 外側だけ
                        if (mix > 0.1f && mix < 0.5f) expect (centre > static_cast<double> (centreCount) / n, label);
                        if (mix > 0.5f && mix < 0.9f) expect (centre < static_cast<double> (centreCount) / n, label);
                    }
                }
            }
        }

        beginTest ("Layout: SuperSAW detune is asymmetric, close to the symmetric one, and keeps the centre voice (D-045)");
        {
            for (int n = 2; n <= polybrisk::maxUnisonVoices; ++n)
            {
                UnisonLayout symmetric, asymmetric;
                polybrisk::computeUnisonLayout (symmetric, n, 0.5f, 1.0f);
                polybrisk::computeUnisonLayout (asymmetric, n, 0.5f, 1.0f, 0.5f, true);
                const double maxCents = polybrisk::detuneAmountToCents (0.5f);

                for (int i = 0; i < n; ++i)
                {
                    const auto u = static_cast<size_t> (i);
                    expectWithinAbsoluteError (asymmetric.cents[u], symmetric.cents[u], 0.08 * maxCents);
                    expectEquals (asymmetric.pan[u], symmetric.pan[u]);
                    expectEquals (asymmetric.gainL[u], symmetric.gainL[u]);

                    // 上下で同じ量の声がない (中央を除く)
                    for (int j = 0; j < n; ++j)
                        if (j != i && std::abs (asymmetric.cents[u]) > 1.0e-9)
                            expect (std::abs (asymmetric.cents[static_cast<size_t> (j)] + asymmetric.cents[u]) > 1.0e-6);
                }

                if ((n & 1) != 0)
                    expectEquals (asymmetric.cents[0], 0.0);
            }
        }

        beginTest ("Layout: symmetric detune, symmetric pan, constant power");
        {
            for (int n = 1; n <= polybrisk::maxUnisonVoices; ++n)
            {
                for (const float detune : { 0.0f, 0.35f, 1.0f })
                {
                    for (const float spread : { 0.0f, 0.5f, 1.0f })
                    {
                        UnisonLayout layout;
                        polybrisk::computeUnisonLayout (layout, n, detune, spread);
                        const auto label = "n=" + juce::String (n) + " detune=" + juce::String (detune) + " spread=" + juce::String (spread);

                        expectEquals (layout.numVoices, n);

                        double centsSum = 0.0, maxCents = 0.0, panSum = 0.0;
                        double powerL = 0.0, powerR = 0.0;
                        int centred = 0;

                        for (int i = 0; i < n; ++i)
                        {
                            const auto u = static_cast<size_t> (i);
                            centsSum += layout.cents[u];
                            maxCents = std::max (maxCents, std::abs (layout.cents[u]));
                            panSum += layout.pan[u];
                            powerL += layout.gainL[u] * layout.gainL[u];
                            powerR += layout.gainR[u] * layout.gainR[u];
                            expect (std::abs (layout.pan[u]) <= spread + 1.0e-6f, label);
                            expectWithinAbsoluteError (layout.ratio[u], std::exp2 (layout.cents[u] / 1200.0), 1.0e-12);

                            if (std::abs (layout.cents[u]) < 1.0e-12)
                                ++centred;

                            // 各声に、符号を反転した対になる声が存在する
                            bool mirrored = false;
                            for (int j = 0; j < n; ++j)
                            {
                                const auto w = static_cast<size_t> (j);
                                if (std::abs (layout.cents[w] + layout.cents[u]) < 1.0e-9
                                    && std::abs (layout.pan[w] + layout.pan[u]) < 1.0e-6f)
                                    mirrored = true;
                            }
                            expect (mirrored, label);
                        }

                        expectWithinAbsoluteError (centsSum, 0.0, 1.0e-9, label);
                        expectWithinAbsoluteError (panSum, 0.0, 1.0e-6, label);
                        expectWithinAbsoluteError (powerL + powerR, 1.0, 1.0e-5, label);
                        expectWithinAbsoluteError (powerL, powerR, 1.0e-5, label);

                        if (n > 1)
                            expectWithinAbsoluteError (maxCents, polybrisk::detuneAmountToCents (detune), 1.0e-9, label);

                        if ((n & 1) != 0 && detune > 0.0f)
                            expectEquals (centred, 1, label); // 奇数本は中央の 1 声がピッチの芯になる

                        if (spread == 0.0f)
                            for (int i = 0; i < n; ++i)
                                expectWithinAbsoluteError (layout.gainL[static_cast<size_t> (i)], layout.gainR[static_cast<size_t> (i)], 1.0e-7f, label);
                    }
                }
            }

            // 範囲外の本数はクランプされる
            UnisonLayout layout;
            polybrisk::computeUnisonLayout (layout, 0, 0.5f, 0.5f);
            expectEquals (layout.numVoices, 1);
            polybrisk::computeUnisonLayout (layout, 99, 0.5f, 0.5f);
            expectEquals (layout.numVoices, polybrisk::maxUnisonVoices);
        }

        beginTest ("Level stays stable as the unison count changes");
        {
            const double sampleRate = 48000.0;
            const int numSamples = 48000 * 4;
            double reference = 0.0;

            for (int n = 1; n <= polybrisk::maxUnisonVoices; ++n)
            {
                const auto out = render (Waveform::saw, n, 0.35f, 0.5f, 220.0 / sampleRate, numSamples, false, 7);
                const double l = polybrisk::test::rms (out.getReadPointer (0), numSamples);
                const double r = polybrisk::test::rms (out.getReadPointer (1), numSamples);
                const double total = std::sqrt (l * l + r * r);

                if (n == 1)
                    reference = total;

                const double db = juce::Decibels::gainToDecibels (total / reference);
                logMessage ("Unison " + juce::String (n) + ": " + juce::String (db, 2) + " dB re 1 voice");
                expect (std::abs (db) < 1.5, "unison " + juce::String (n) + " level " + juce::String (db, 2) + " dB");
            }
        }

        beginTest ("Stereo spread: 0 is mono, 1 is wide, mono sum keeps its body");
        {
            const double inc = 220.0 / 48000.0;
            const int numSamples = 48000 * 2;

            const auto narrow = render (Waveform::saw, 7, 0.35f, 0.0f, inc, numSamples, false, 3);
            bool identical = true;
            for (int i = 0; i < numSamples; ++i)
                identical = identical && juce::exactlyEqual (narrow.getSample (0, i), narrow.getSample (1, i));
            expect (identical);
            expect (polybrisk::test::rms (narrow.getReadPointer (0), numSamples) > 0.1); // センターの音は消えない

            const auto wide = render (Waveform::saw, 7, 0.35f, 1.0f, inc, numSamples, false, 3);
            std::vector<float> mid (static_cast<size_t> (numSamples)), side (static_cast<size_t> (numSamples));
            for (int i = 0; i < numSamples; ++i)
            {
                mid[static_cast<size_t> (i)] = 0.5f * (wide.getSample (0, i) + wide.getSample (1, i));
                side[static_cast<size_t> (i)] = 0.5f * (wide.getSample (0, i) - wide.getSample (1, i));
            }

            const double midRms = polybrisk::test::rms (mid.data(), numSamples);
            const double sideRms = polybrisk::test::rms (side.data(), numSamples);
            const double leftRms = polybrisk::test::rms (wide.getReadPointer (0), numSamples);
            logMessage ("Spread 100%: mid " + juce::String (midRms, 3) + ", side " + juce::String (sideRms, 3) + ", left " + juce::String (leftRms, 3));

            expect (sideRms > 0.05);          // 実際に広がっている
            expect (midRms > leftRms * 0.6);  // モノ合成でも芯が残る
        }

        beginTest ("Retrigger is reproducible; Free differs per seed");
        {
            const double inc = 330.0 / 48000.0;
            const auto a = render (Waveform::saw, 7, 0.35f, 0.5f, inc, 2048, true, 1);
            const auto b = render (Waveform::saw, 7, 0.35f, 0.5f, inc, 2048, true, 99);

            bool same = true;
            for (int i = 0; i < 2048; ++i)
                same = same && juce::exactlyEqual (a.getSample (0, i), b.getSample (0, i));
            expect (same);

            const auto c = render (Waveform::saw, 7, 0.35f, 0.5f, inc, 2048, false, 1);
            const auto d = render (Waveform::saw, 7, 0.35f, 0.5f, inc, 2048, false, 99);

            float diff = 0.0f;
            for (int i = 0; i < 2048; ++i)
                diff = std::max (diff, std::abs (c.getSample (0, i) - d.getSample (0, i)));
            expect (diff > 0.05f);

            // 単声の Retrigger は位相 0 から始まる
            const auto single = render (Waveform::saw, 1, 0.0f, 0.0f, inc, 16, true, 5);
            expectWithinAbsoluteError (single.getSample (0, 0), 0.0f, 1.0e-6f);
        }

        beginTest ("Noise: independent per seed, pitch-independent, white-ish, centred");
        {
            const int numSamples = 48000;
            const auto a = render (Waveform::noise, 7, 0.35f, 1.0f, 0.01, numSamples, true, 1);
            const auto b = render (Waveform::noise, 1, 0.0f, 0.0f, 0.2, numSamples, true, 1);
            const auto c = render (Waveform::noise, 1, 0.0f, 0.0f, 0.01, numSamples, true, 2);

            bool samePitchIndependent = true, differsPerSeed = false, centred = true;
            for (int i = 0; i < numSamples; ++i)
            {
                samePitchIndependent = samePitchIndependent && juce::exactlyEqual (a.getSample (0, i), b.getSample (0, i));
                differsPerSeed = differsPerSeed || ! juce::exactlyEqual (a.getSample (0, i), c.getSample (0, i));
                centred = centred && juce::exactlyEqual (a.getSample (0, i), a.getSample (1, i));
            }

            expect (samePitchIndependent); // 音程・ユニゾン・Spread・Retrigger の影響を受けない
            expect (differsPerSeed);
            expect (centred);

            const auto* data = a.getReadPointer (0);
            expectWithinAbsoluteError (polybrisk::test::rms (data, numSamples), 0.7071 / std::sqrt (3.0), 0.01);
            expect (std::abs (polybrisk::test::dcOffset (data, numSamples)) < 0.01);

            // 隣接サンプルの相関が小さい (白色に近い)
            double correlation = 0.0, energy = 0.0;
            for (int i = 1; i < numSamples; ++i)
            {
                correlation += static_cast<double> (data[i]) * data[i - 1];
                energy += static_cast<double> (data[i]) * data[i];
            }
            expect (std::abs (correlation / energy) < 0.02);

            // 2 つのシードの系列は無相関
            double cross = 0.0;
            const auto* other = c.getReadPointer (0);
            for (int i = 0; i < numSamples; ++i)
                cross += static_cast<double> (data[i]) * other[i];
            expect (std::abs (cross / energy) < 0.02);
        }

        beginTest ("Very high pitch with detune stays finite and bounded");
        {
            const auto out = render (Waveform::pulse, 9, 1.0f, 1.0f, 0.44, 4096, true, 1);
            expect (polybrisk::test::allFinite (out));
            expect (polybrisk::test::peakLevel (out) < 4.0f);
        }
    }

private:
    static juce::AudioBuffer<float> render (Waveform wave, int voices, float detune, float spread, double inc,
                                            int numSamples, bool retrigger, std::uint32_t seed)
    {
        UnisonLayout layout;
        polybrisk::computeUnisonLayout (layout, voices, detune, spread);

        UnisonOscillator oscillator;
        oscillator.seed (seed);
        if (retrigger)
            oscillator.retrigger();

        juce::AudioBuffer<float> out (2, numSamples);
        out.clear();

        // 実際の使われ方と同じく 32 サンプル以下に分割して呼ぶ
        for (int pos = 0; pos < numSamples; pos += 32)
            oscillator.render (out.getWritePointer (0, pos), out.getWritePointer (1, pos),
                               std::min (32, numSamples - pos), wave, inc, layout, 0.5);

        return out;
    }
};

UnisonTests unisonTests;
} // namespace
