#include "TestHelpers.h"
#include "synth/SynthEngine.h"

#include <cmath>
#include <vector>

namespace
{
using polybrisk::EngineParams;
using polybrisk::SynthEngine;
using polybrisk::TransientMode;
using polybrisk::Waveform;

double toDb (double ratio) { return 20.0 * std::log10 (std::max (ratio, 1.0e-12)); }

juce::AudioBuffer<float> renderNote (double sampleRate, const EngineParams& params, int note, int numSamples)
{
    SynthEngine engine;
    engine.prepare (sampleRate);
    engine.setParams (params);
    engine.noteOn (note, 1.0f);

    juce::AudioBuffer<float> out (2, numSamples);
    for (int pos = 0; pos < numSamples; pos += 128)
        engine.render (out.getWritePointer (0, pos), out.getWritePointer (1, pos), std::min (128, numSamples - pos));

    return out;
}

/** OSC を止めて Noise Burst だけを鳴らす設定。 */
EngineParams burstOnly (float amount, float duration, float colour)
{
    EngineParams p;
    p.oscA.enabled = false;
    p.amp = { 0.0001f, 0.05f, 1.0f, 0.02f };
    p.transient.enabled = true;
    p.transient.mode = TransientMode::noiseBurst;
    p.transient.amount = amount;
    p.transient.duration = duration;
    p.transient.noiseColor = colour;
    return p;
}

EngineParams sineWithSnap (float semitones, float amount, float duration, TransientMode mode = TransientMode::pitchSnap)
{
    EngineParams p;
    p.oscA.waveform = Waveform::sine;
    p.amp = { 0.0001f, 0.05f, 1.0f, 0.02f };
    p.transient.enabled = true;
    p.transient.mode = mode;
    p.transient.amount = amount;
    p.transient.duration = duration;
    p.transient.pitchSnap = semitones;
    return p;
}

double brightness (const juce::AudioBuffer<float>& buffer, int start, int length)
{
    std::vector<float> diff (static_cast<size_t> (length - 1));
    const float* x = buffer.getReadPointer (0, start);
    for (int i = 0; i + 1 < length; ++i)
        diff[static_cast<size_t> (i)] = x[i + 1] - x[i];

    return polybrisk::test::rms (diff.data(), length - 1) / std::max (1.0e-12, polybrisk::test::rms (x, length));
}

class TransientTests final : public juce::UnitTest
{
public:
    TransientTests() : juce::UnitTest ("Transient", "dsp") {}

    void runTest() override
    {
        const double sr = 48000.0;

        beginTest ("Disabled transient changes nothing, whatever its other settings are");
        {
            EngineParams base;
            base.oscA.waveform = Waveform::saw;
            auto other = base;
            other.transient.mode = TransientMode::both;
            other.transient.amount = 1.0f;
            other.transient.pitchSnap = -24.0f;
            other.transient.duration = 0.15f;

            const auto a = renderNote (sr, base, 60, 9600);
            const auto b = renderNote (sr, other, 60, 9600);

            bool identical = true;
            for (int i = 0; i < 9600; ++i)
                identical = identical && juce::exactlyEqual (a.getSample (0, i), b.getSample (0, i));
            expect (identical);
        }

        beginTest ("Noise Burst: independent of the oscillators, fades in without a pop and decays in the set time");
        for (const double rate : { 44100.0, 48000.0, 96000.0 })
        {
            const double duration = 0.05;
            const auto out = renderNote (rate, burstOnly (1.0f, static_cast<float> (duration), 0.5f), 60, static_cast<int> (rate * 0.3));
            const auto at = [rate] (double seconds) { return static_cast<int> (rate * seconds); };

            expect (polybrisk::test::allFinite (out));

            const double early = polybrisk::test::rms (out.getReadPointer (0, at (0.001)), at (0.004));
            const double half = polybrisk::test::rms (out.getReadPointer (0, at (0.5 * duration - 0.002)), at (0.004));
            const double end = polybrisk::test::rms (out.getReadPointer (0, at (duration - 0.002)), at (0.004));

            expect (early > 0.01);
            expect (std::abs (out.getSample (0, 0)) < 0.01f);                  // 立ち上がりはフェードイン
            expectWithinAbsoluteError (toDb (half / early), -25.0, 6.0);       // 半分の時間で約 -30 dB (窓の位置の分だけ浅い)
            expectWithinAbsoluteError (toDb (end / early), -55.0, 6.0);        // 指定時間で約 -60 dB
            expect (polybrisk::test::peakLevel (out, at (2.0 * duration), at (0.1)) < 1.0e-9f);   // その後は無音 (ボイスは発音中のまま)

            // モノラル (センター) で加わる
            bool centred = true;
            for (int i = 0; i < out.getNumSamples(); ++i)
                centred = centred && juce::exactlyEqual (out.getSample (0, i), out.getSample (1, i));
            expect (centred);
        }

        beginTest ("Noise Burst: Amount scales the level linearly; 0 is silent");
        {
            const auto full = renderNote (sr, burstOnly (1.0f, 0.05f, 0.5f), 60, 4800);
            const auto half = renderNote (sr, burstOnly (0.5f, 0.05f, 0.5f), 60, 4800);
            const auto none = renderNote (sr, burstOnly (0.0f, 0.05f, 0.5f), 60, 4800);

            const double ratio = polybrisk::test::rms (half.getReadPointer (0), 4800) / polybrisk::test::rms (full.getReadPointer (0), 4800);
            expectWithinAbsoluteError (ratio, 0.5, 0.001);
            expectEquals (polybrisk::test::peakLevel (none), 0.0f);
        }

        beginTest ("Noise Burst: Color goes from dark to bright");
        {
            const auto dark = renderNote (sr, burstOnly (1.0f, 0.1f, 0.0f), 60, 2400);
            const auto mid = renderNote (sr, burstOnly (1.0f, 0.1f, 0.5f), 60, 2400);
            const auto bright = renderNote (sr, burstOnly (1.0f, 0.1f, 1.0f), 60, 2400);

            const double bDark = brightness (dark, 48, 2000);
            const double bMid = brightness (mid, 48, 2000);
            const double bBright = brightness (bright, 48, 2000);
            logMessage ("  brightness dark/mid/bright: " + juce::String (bDark, 3) + " / " + juce::String (bMid, 3) + " / " + juce::String (bBright, 3));
            expect (bDark < 0.5 * bMid);
            expect (bBright > 1.1 * bMid);
        }

        beginTest ("Noise Burst passes through the filter (it is part of the voice mix)");
        {
            auto p = burstOnly (1.0f, 0.1f, 0.5f);
            const auto open = renderNote (sr, p, 60, 2400);
            p.filter.cutoffHz = 300.0f;
            const auto closed = renderNote (sr, p, 60, 2400);
            expect (brightness (closed, 48, 2000) < 0.3 * brightness (open, 48, 2000));
        }

        beginTest ("Pitch Snap: starts offset by the set interval and converges to the exact pitch");
        {
            const int note = 93; // A6 = 1760 Hz
            const double nominal = SynthEngine::midiNoteToHz (note);
            const auto early = [&] (const EngineParams& p)
            {
                const auto out = renderNote (sr, p, note, 48000);
                return polybrisk::test::estimateFrequency (out.getReadPointer (0), 480, sr);
            };
            const auto late = [&] (const EngineParams& p)
            {
                const auto out = renderNote (sr, p, note, 48000);
                return polybrisk::test::estimateFrequency (out.getReadPointer (0, 24000), 24000, sr);
            };

            const auto up = sineWithSnap (12.0f, 1.0f, 0.1f);
            const auto down = sineWithSnap (-12.0f, 1.0f, 0.1f);
            const auto halfAmount = sineWithSnap (12.0f, 0.5f, 0.1f);

            // 最初の 10 ms: +12 st から始まり、指数的に戻る途中 (平均でおよそ +8〜9 st)
            expect (early (up) > nominal * 1.4 && early (up) < nominal * 2.0);
            expect (early (down) < nominal / 1.4 && early (down) > nominal / 2.0);
            expect (early (halfAmount) > nominal * 1.15 && early (halfAmount) < early (up));

            // 収束後は 1 cent 未満の精度で元のピッチ
            expectWithinAbsoluteError (late (up), nominal, nominal * 0.0003);
            expectWithinAbsoluteError (late (down), nominal, nominal * 0.0003);

            // Noise Burst モードではピッチは動かない / Amount 0・Snap 0 でも動かない
            {
                const auto control = polybrisk::TransientControl::make (sineWithSnap (12.0f, 1.0f, 0.1f, TransientMode::noiseBurst).transient, sr);
                expect (! control.pitchActive && control.noiseActive);
                expectEquals (control.pitchOffsetAt (0.0), 0.0);

                const auto snapOnly = polybrisk::TransientControl::make (up.transient, sr);
                expect (snapOnly.pitchActive && ! snapOnly.noiseActive);
                expectWithinAbsoluteError (snapOnly.pitchOffsetAt (0.0), 12.0, 1.0e-9);
                expectWithinAbsoluteError (snapOnly.pitchOffsetAt (0.1 * sr), 0.012, 1.0e-6); // 指定時間で 1/1000
                expectEquals (snapOnly.pitchOffsetAt (0.2 * sr), 0.0);
            }
            expectWithinAbsoluteError (early (sineWithSnap (12.0f, 0.0f, 0.1f)), nominal, nominal * 0.02);
            expectWithinAbsoluteError (early (sineWithSnap (0.0f, 1.0f, 0.1f)), nominal, nominal * 0.02);
        }

        beginTest ("Pitch Snap is common to both oscillators and keeps their tuning interval");
        {
            auto p = sineWithSnap (24.0f, 1.0f, 0.02f);
            p.oscA.enabled = false;
            p.oscB.enabled = true;
            p.oscB.waveform = Waveform::sine;
            p.oscB.semitone = 7;
            p.oscB.level = 1.0f;

            const auto out = renderNote (sr, p, 69, 48000);
            const double expected = SynthEngine::midiNoteToHz (76);
            const double measured = polybrisk::test::estimateFrequency (out.getReadPointer (0, 24000), 24000, sr);
            expectWithinAbsoluteError (measured, expected, expected * 0.0003);
        }

        beginTest ("Pitch Snap is continuous (no click) and the same at every sample rate");
        {
            std::vector<double> frequencies;

            for (const double rate : { 44100.0, 48000.0, 96000.0 })
            {
                const auto out = renderNote (rate, sineWithSnap (24.0f, 1.0f, 0.05f), 81, static_cast<int> (rate * 0.2));
                expect (polybrisk::test::allFinite (out));

                // 880 Hz の +24 st = 3520 Hz の正弦波より急な変化は起きない
                const float slope = polybrisk::Voice::voiceGain * 0.70710678f
                                  * static_cast<float> (2.0 * juce::MathConstants<double>::pi * 3520.0 / rate);
                expect (polybrisk::test::maxAbsDelta (out.getReadPointer (0), out.getNumSamples()) < slope * 1.1f);

                frequencies.push_back (polybrisk::test::estimateFrequency (out.getReadPointer (0), static_cast<int> (rate * 0.02), rate));
            }

            expectWithinAbsoluteError (frequencies[1], frequencies[0], frequencies[0] * 0.02);
            expectWithinAbsoluteError (frequencies[2], frequencies[0], frequencies[0] * 0.02);
        }

        beginTest ("Extreme transient settings with full polyphony stay finite and bounded");
        for (const auto mode : { TransientMode::noiseBurst, TransientMode::pitchSnap, TransientMode::both })
        {
            for (const float duration : { 0.001f, 0.15f })
            {
                for (const float snap : { -24.0f, 24.0f })
                {
                    EngineParams p;
                    p.oscA.waveform = Waveform::saw;
                    p.oscA.unisonVoices = 9;
                    p.amp = { 0.0001f, 0.05f, 0.5f, 0.01f };
                    p.transient = { true, mode, 1.0f, duration, 1.0f, snap };

                    SynthEngine engine;
                    engine.prepare (44100.0);
                    engine.setParams (p);

                    juce::AudioBuffer<float> out (2, 8192);
                    for (int pos = 0, block = 0; pos < 8192; pos += 64, ++block)
                    {
                        if (block % 4 == 0)
                            for (int n = 0; n < 6; ++n)
                                engine.noteOn (20 + ((block * 7 + n * 13) % 100), 1.0f); // 高音域とスティールを含む

                        engine.render (out.getWritePointer (0, pos), out.getWritePointer (1, pos), 64);
                    }

                    expect (polybrisk::test::allFinite (out));
                    expect (polybrisk::test::peakLevel (out) <= 1.0f);
                }
            }
        }
    }
};

TransientTests transientTests;
} // namespace
