#include "TestHelpers.h"
#include "synth/Filters.h"
#include "synth/SynthEngine.h"

#include <cmath>
#include <random>
#include <vector>

namespace
{
using polybrisk::EngineParams;
using polybrisk::FilterCharacter;
using polybrisk::FilterMode;
using polybrisk::FilterSettings;
using polybrisk::StereoFilter;
using polybrisk::SynthEngine;
using polybrisk::Waveform;

constexpr FilterCharacter allCharacters[] = { FilterCharacter::analog, FilterCharacter::clean, FilterCharacter::punch };
constexpr FilterMode allModes[] = { FilterMode::lp12, FilterMode::lp24, FilterMode::hp12, FilterMode::hp24, FilterMode::bp12 };

double toDb (double ratio) { return 20.0 * std::log10 (std::max (ratio, 1.0e-12)); }

/** 定常状態の正弦波に対するゲイン (dB)。 */
double sineGainDb (const FilterSettings& settings, double cutoffHz, double frequency, double sampleRate, float amplitude = 0.1f)
{
    StereoFilter f;
    const float g = polybrisk::filter::cutoffToG (cutoffHz, sampleRate);
    const int settle = static_cast<int> (sampleRate * 0.5);
    const int measure = static_cast<int> (sampleRate * 0.5);

    std::vector<float> l (static_cast<size_t> (settle + measure)), r (l.size());
    for (size_t i = 0; i < l.size(); ++i)
        l[i] = r[i] = amplitude * static_cast<float> (std::sin (2.0 * juce::MathConstants<double>::pi * frequency * static_cast<double> (i) / sampleRate));

    for (int pos = 0; pos < settle + measure; pos += 32)
        f.process (l.data() + pos, r.data() + pos, std::min (32, settle + measure - pos), settings, g, 0.0f);

    const double out = polybrisk::test::rms (l.data() + settle, measure);
    return toDb (out / (amplitude / std::sqrt (2.0)));
}

EngineParams sawPatch()
{
    EngineParams p;
    p.oscA.waveform = Waveform::saw;
    p.amp = { 0.001f, 0.05f, 1.0f, 0.02f };
    return p;
}

juce::AudioBuffer<float> renderNote (double sampleRate, const EngineParams& params, int note, float velocity, int numSamples)
{
    SynthEngine engine;
    engine.prepare (sampleRate);
    engine.setParams (params);
    engine.noteOn (note, velocity);

    juce::AudioBuffer<float> out (2, numSamples);
    for (int pos = 0; pos < numSamples; pos += 256)
        engine.render (out.getWritePointer (0, pos), out.getWritePointer (1, pos), std::min (256, numSamples - pos));

    return out;
}

/** 高域成分の目安: 1 階差分の RMS / RMS。明るい音ほど大きい。 */
double brightness (const juce::AudioBuffer<float>& buffer, int start, int length)
{
    std::vector<float> diff (static_cast<size_t> (length - 1));
    const float* x = buffer.getReadPointer (0, start);
    for (int i = 0; i + 1 < length; ++i)
        diff[static_cast<size_t> (i)] = x[i + 1] - x[i];

    return polybrisk::test::rms (diff.data(), length - 1) / std::max (1.0e-12, polybrisk::test::rms (x, length));
}

class FilterTests final : public juce::UnitTest
{
public:
    FilterTests() : juce::UnitTest ("Filter", "dsp") {}

    void runTest() override
    {
        const double sr = 96000.0;
        const double fc = 1000.0;

        beginTest ("Clean: each mode has the expected slope and -3 dB point");
        {
            const auto gain = [&] (FilterMode mode, double frequency)
            {
                return sineGainDb (FilterSettings::make (FilterCharacter::clean, mode, 0.0f, 0.0f), fc, frequency, sr);
            };

            expectWithinAbsoluteError (gain (FilterMode::lp12, 250.0), 0.0, 0.2);
            expectWithinAbsoluteError (gain (FilterMode::lp12, 1000.0), -3.01, 0.2);
            expectWithinAbsoluteError (gain (FilterMode::lp12, 4000.0), -24.1, 1.0);

            expectWithinAbsoluteError (gain (FilterMode::lp24, 250.0), 0.0, 0.2);
            expectWithinAbsoluteError (gain (FilterMode::lp24, 1000.0), -3.01, 0.2);
            expectWithinAbsoluteError (gain (FilterMode::lp24, 4000.0), -48.2, 2.0);

            expectWithinAbsoluteError (gain (FilterMode::hp12, 4000.0), 0.0, 0.2);
            expectWithinAbsoluteError (gain (FilterMode::hp12, 1000.0), -3.01, 0.2);
            expectWithinAbsoluteError (gain (FilterMode::hp12, 250.0), -24.1, 1.0);

            expectWithinAbsoluteError (gain (FilterMode::hp24, 4000.0), 0.0, 0.2);
            expectWithinAbsoluteError (gain (FilterMode::hp24, 1000.0), -3.01, 0.2);
            expectWithinAbsoluteError (gain (FilterMode::hp24, 250.0), -48.2, 2.0);

            expectWithinAbsoluteError (gain (FilterMode::bp12, 1000.0), -3.01, 0.2);
            expect (gain (FilterMode::bp12, 250.0) < -9.0);
            expect (gain (FilterMode::bp12, 4000.0) < -9.0);
        }

        beginTest ("Cutoff is accurate at every sample rate (prewarped)");
        for (const double rate : { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
            for (const double cutoff : { 100.0, 1000.0, 8000.0 })
                expectWithinAbsoluteError (
                    sineGainDb (FilterSettings::make (FilterCharacter::clean, FilterMode::lp24, 0.0f, 0.0f), cutoff, cutoff, rate),
                    -3.01, 0.2);

        beginTest ("Cutoff is clamped to a safe range below Nyquist");
        {
            const float gLow = polybrisk::filter::cutoffToG (1.0, 48000.0);
            const float gHigh = polybrisk::filter::cutoffToG (1.0e6, 44100.0);
            expectWithinAbsoluteError (gLow, polybrisk::filter::cutoffToG (20.0, 48000.0), 1.0e-9f);
            expectWithinAbsoluteError (gHigh, polybrisk::filter::cutoffToG (0.45 * 44100.0, 44100.0), 1.0e-6f);
            expect (std::isfinite (gHigh) && gHigh > 0.0f);
        }

        beginTest ("Clean is exactly linear at Drive 0; Analog and Punch are not");
        {
            const auto render = [] (FilterCharacter character, float amplitude)
            {
                StereoFilter f;
                const auto settings = FilterSettings::make (character, FilterMode::lp24, 0.5f, 0.0f);
                const float g = polybrisk::filter::cutoffToG (800.0, 96000.0);
                std::vector<float> l (4096), r (4096);
                for (size_t i = 0; i < l.size(); ++i)
                    l[i] = r[i] = amplitude * static_cast<float> (std::sin (0.05 * static_cast<double> (i)));
                f.process (l.data(), r.data(), 4096, settings, g, 0.0f);
                return l;
            };

            const auto nonlinearity = [&] (FilterCharacter character)
            {
                const auto small = render (character, 0.5f);
                const auto large = render (character, 2.0f);
                float worst = 0.0f;
                for (size_t i = 0; i < small.size(); ++i)
                    worst = std::max (worst, std::abs (large[i] - 4.0f * small[i]));
                return worst;
            };

            expect (nonlinearity (FilterCharacter::clean) < 1.0e-5f);
            expect (nonlinearity (FilterCharacter::analog) > 0.01f);
            expect (nonlinearity (FilterCharacter::punch) > 0.01f);
        }

        beginTest ("Resonance: peak grows with the knob; Punch and Analog boost less than Clean");
        {
            const auto peak = [&] (FilterCharacter character, FilterMode mode, float resonance)
            {
                return sineGainDb (FilterSettings::make (character, mode, resonance, 0.0f), fc, fc, sr, 0.5f);
            };

            for (const auto mode : { FilterMode::lp12, FilterMode::lp24 })
            {
                for (const auto character : allCharacters)
                {
                    expect (peak (character, mode, 0.5f) > peak (character, mode, 0.0f) + 2.0);
                    expect (peak (character, mode, 1.0f) > peak (character, mode, 0.5f));
                    expect (peak (character, mode, 1.0f) < 24.0);
                }

                const double clean = peak (FilterCharacter::clean, mode, 1.0f);
                const double analog = peak (FilterCharacter::analog, mode, 1.0f);
                const double punch = peak (FilterCharacter::punch, mode, 1.0f);
                logMessage ("  resonance 100% peak gain: clean " + juce::String (clean, 1) + " dB, analog "
                            + juce::String (analog, 1) + " dB, punch " + juce::String (punch, 1) + " dB");
                expect (analog < clean - 3.0);
                expect (punch < clean - 6.0);
            }
        }

        beginTest ("Filter Drive adds harmonics and keeps the level under control");
        for (const auto character : allCharacters)
        {
            const auto measure = [&] (float drive)
            {
                StereoFilter f;
                const auto settings = FilterSettings::make (character, FilterMode::lp12, 0.0f, drive);
                const float g = polybrisk::filter::cutoffToG (18000.0, sr);
                const int n = 1 << 15;
                std::vector<float> l (static_cast<size_t> (n)), r (l.size());
                for (size_t i = 0; i < l.size(); ++i)
                    l[i] = r[i] = 0.7f * static_cast<float> (std::sin (2.0 * juce::MathConstants<double>::pi * 750.0 * static_cast<double> (i) / sr));
                f.process (l.data(), r.data(), n, settings, g, 0.0f);

                const auto spectrum = polybrisk::test::magnitudeSpectrum (l.data(), 15);
                const double fundamental = polybrisk::test::magnitudeNear (spectrum, sr, 750.0);
                const double third = polybrisk::test::magnitudeNear (spectrum, sr, 2250.0);
                return std::pair { toDb (third / fundamental), polybrisk::test::rms (l.data(), n) };
            };

            const auto [thirdOff, rmsOff] = measure (0.0f);
            const auto [thirdFull, rmsFull] = measure (1.0f);
            expect (thirdFull > thirdOff + 20.0);
            expect (thirdFull > -20.0);
            expect (toDb (rmsFull / rmsOff) < 6.0);   // 音量は多少上がるが暴れない
            expect (toDb (rmsFull / rmsOff) > -6.0);
        }

        beginTest ("Stability: full resonance and drive with violent cutoff modulation stays bounded");
        for (const double rate : { 44100.0, 96000.0 })
        {
            for (const auto character : allCharacters)
            {
                for (const auto mode : allModes)
                {
                    StereoFilter f;
                    const auto settings = FilterSettings::make (character, mode, 1.0f, 1.0f);
                    std::mt19937 rng (1234);
                    std::uniform_real_distribution<float> noise (-2.0f, 2.0f);
                    std::uniform_real_distribution<double> octave (std::log2 (20.0), std::log2 (20000.0));

                    float g = polybrisk::filter::cutoffToG (1000.0, rate);
                    float worst = 0.0f;
                    bool finite = true;

                    for (int block = 0; block < 3000; ++block)
                    {
                        float l[32], r[32];
                        for (int i = 0; i < 32; ++i)
                        {
                            l[i] = noise (rng);
                            r[i] = noise (rng);
                        }

                        const float target = polybrisk::filter::cutoffToG (std::exp2 (octave (rng)), rate);
                        f.process (l, r, 32, settings, g, (target - g) / 32.0f);
                        g = target;

                        for (int i = 0; i < 32; ++i)
                        {
                            finite = finite && std::isfinite (l[i]) && std::isfinite (r[i]);
                            worst = std::max ({ worst, std::abs (l[i]), std::abs (r[i]) });
                        }
                    }

                    expect (finite);
                    expect (worst < 40.0f, "peak " + juce::String (worst));
                }
            }
        }

        beginTest ("A NaN in the input does not poison the filter");
        for (const auto character : allCharacters)
        {
            StereoFilter f;
            const auto settings = FilterSettings::make (character, FilterMode::lp24, 0.8f, 0.5f);
            const float g = polybrisk::filter::cutoffToG (1000.0, 48000.0);
            float l[32] = {}, r[32] = {};
            l[3] = std::numeric_limits<float>::quiet_NaN();
            r[5] = std::numeric_limits<float>::infinity();
            f.process (l, r, 32, settings, g, 0.0f);

            for (int i = 0; i < 32; ++i)
                l[i] = r[i] = 0.25f;
            f.process (l, r, 32, settings, g, 0.0f);

            bool finite = true;
            for (int i = 0; i < 32; ++i)
                finite = finite && std::isfinite (l[i]) && std::isfinite (r[i]);
            expect (finite);
        }

        beginTest ("Engine: the default (fully open) filter leaves a 1 kHz tone untouched");
        {
            EngineParams p;
            p.oscA.waveform = Waveform::sine;
            p.amp = { 0.001f, 0.05f, 1.0f, 0.02f };
            const auto out = renderNote (48000.0, p, 83, 1.0f, 24000); // B5 ≒ 988 Hz
            const float peak = polybrisk::test::peakLevel (out, 12000, 12000);
            expectWithinAbsoluteError (toDb (peak / (polybrisk::Voice::voiceGain * 0.70710678f)), 0.0, 0.05);
        }

        beginTest ("Engine: Filter EG opens (positive) or closes (negative) the cutoff");
        {
            auto p = sawPatch();
            p.filter.cutoffHz = 300.0f;
            p.filterEnv = { 0.05f, 0.5f, 1.0f, 0.1f };

            const auto without = renderNote (48000.0, p, 45, 1.0f, 19200);
            p.filter.envAmount = 0.6f;
            const auto with = renderNote (48000.0, p, 45, 1.0f, 19200);

            // Attack の前後で明るさが上がり、Env Amount 0 では変わらない
            expect (brightness (with, 9600, 4800) > 3.0 * brightness (with, 0, 960));
            expect (brightness (with, 9600, 4800) > 3.0 * brightness (without, 9600, 4800));
            expectWithinAbsoluteError (brightness (without, 9600, 4800) / brightness (without, 4800, 4800), 1.0, 0.05);

            p.filter.cutoffHz = 6000.0f;
            p.filter.envAmount = 0.0f;
            const auto open = renderNote (48000.0, p, 45, 1.0f, 19200);
            p.filter.envAmount = -0.6f;
            const auto closing = renderNote (48000.0, p, 45, 1.0f, 19200);
            expect (brightness (closing, 9600, 4800) < 0.4 * brightness (open, 9600, 4800));
        }

        beginTest ("Engine: Filter EG follows note-off (release closes the filter)");
        {
            auto p = sawPatch();
            p.amp = { 0.001f, 0.05f, 1.0f, 2.0f };          // Amp は長く残す
            p.filter.cutoffHz = 200.0f;
            p.filter.envAmount = 0.7f;
            p.filterEnv = { 0.001f, 0.05f, 1.0f, 0.03f };   // Filter EG はすぐ閉じる

            SynthEngine engine;
            engine.prepare (48000.0);
            engine.setParams (p);
            engine.noteOn (45, 1.0f);

            juce::AudioBuffer<float> out (2, 19200);
            engine.render (out.getWritePointer (0), out.getWritePointer (1), 9600);
            engine.noteOff (45);
            engine.render (out.getWritePointer (0, 9600), out.getWritePointer (1, 9600), 9600);

            expect (brightness (out, 14400, 4800) < 0.3 * brightness (out, 4800, 4800));
        }

        beginTest ("Engine: Key Tracking moves the cutoff with the note, 1:1 at 100%");
        {
            EngineParams p;
            p.oscA.waveform = Waveform::sine;
            p.amp = { 0.001f, 0.05f, 1.0f, 0.02f };
            p.filter.cutoffHz = static_cast<float> (SynthEngine::midiNoteToHz (60));

            const auto levelDb = [&] (int note)
            {
                const auto out = renderNote (48000.0, p, note, 1.0f, 24000);
                return toDb (polybrisk::test::peakLevel (out, 12000, 12000) / (polybrisk::Voice::voiceGain * 0.70710678f));
            };

            // Key Track 0: カットオフ固定。高いノートほど大きく減衰する
            expectWithinAbsoluteError (levelDb (60), -3.01, 0.3);
            expect (levelDb (84) < -40.0);
            expect (levelDb (36) > -0.3);

            // Key Track 100%: カットオフが音程に追従し、どのノートでも -3 dB
            p.filter.keyTrack = 1.0f;
            for (const int note : { 36, 60, 84, 96 })
                expectWithinAbsoluteError (levelDb (note), -3.01, 0.3);

            // 50%: 2 オクターブ上で 1 オクターブだけ追従 → 1 オクターブ分 (約 -24 dB) の減衰
            p.filter.keyTrack = 0.5f;
            expectWithinAbsoluteError (levelDb (84), -24.3, 1.5);
        }

        beginTest ("Engine: Velocity -> Cutoff darkens soft notes; Velocity -> Amp is adjustable");
        {
            EngineParams p;
            p.oscA.waveform = Waveform::sine;
            p.amp = { 0.001f, 0.05f, 1.0f, 0.02f };
            p.velocityToAmp = 0.0f;

            const auto level = [&] (float velocity)
            {
                const auto out = renderNote (48000.0, p, 69, velocity, 24000);
                return static_cast<double> (polybrisk::test::peakLevel (out, 12000, 12000));
            };

            const double reference = polybrisk::Voice::voiceGain * 0.70710678;

            // Velocity → Amp 0%: ベロシティによらず同じ音量
            expectWithinAbsoluteError (level (0.2f), reference, 0.001);
            expectWithinAbsoluteError (level (1.0f), reference, 0.001);

            // 50%: gain = 1 - 0.5 + 0.5 * velocity
            p.velocityToAmp = 0.5f;
            expectWithinAbsoluteError (level (0.2f), reference * 0.6, 0.001);

            // Velocity → Cutoff 100%: ベロシティ 1 では変化なし、0.2 で 4 オクターブ下がる
            p.velocityToAmp = 0.0f;
            p.filter.cutoffHz = 440.0f;
            p.filter.velocity = 1.0f;
            expectWithinAbsoluteError (toDb (level (1.0f) / reference), -3.01, 0.3);
            expect (toDb (level (0.2f) / reference) < -60.0);

            p.filter.velocity = 0.0f;
            expectWithinAbsoluteError (toDb (level (0.2f) / reference), -3.01, 0.3);
        }

        beginTest ("Engine: the three characters are audibly different, not the same filter renamed");
        {
            const auto render = [] (FilterCharacter character)
            {
                auto p = sawPatch();
                p.oscA.unisonVoices = 3;
                p.filter.character = character;
                p.filter.cutoffHz = 500.0f;
                p.filter.resonance = 0.4f;
                p.filter.drive = 0.3f;
                p.filter.envAmount = 0.5f;
                p.filterEnv = { 0.002f, 0.15f, 0.2f, 0.1f };
                return renderNote (48000.0, p, 48, 1.0f, 24000);
            };

            const auto analog = render (FilterCharacter::analog);
            const auto clean = render (FilterCharacter::clean);
            const auto punch = render (FilterCharacter::punch);

            const auto differenceDb = [] (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
            {
                std::vector<float> diff (static_cast<size_t> (a.getNumSamples()));
                for (int i = 0; i < a.getNumSamples(); ++i)
                    diff[static_cast<size_t> (i)] = a.getSample (0, i) - b.getSample (0, i);
                return toDb (polybrisk::test::rms (diff.data(), a.getNumSamples())
                             / polybrisk::test::rms (a.getReadPointer (0), a.getNumSamples()));
            };

            logMessage ("  difference: analog/clean " + juce::String (differenceDb (analog, clean), 1) + " dB, clean/punch "
                        + juce::String (differenceDb (clean, punch), 1) + " dB, analog/punch "
                        + juce::String (differenceDb (analog, punch), 1) + " dB");
            expect (differenceDb (analog, clean) > -30.0);
            expect (differenceDb (clean, punch) > -30.0);
            expect (differenceDb (analog, punch) > -30.0);

            // Analog はカットオフの追従がわずかに遅い → アタック直後は Clean より暗い
            expect (brightness (analog, 0, 96) < brightness (clean, 0, 96));
        }

        beginTest ("Engine: large cutoff / resonance jumps while playing do not click");
        for (const auto character : allCharacters)
        {
            EngineParams p;
            p.oscA.waveform = Waveform::sine;
            p.amp = { 0.001f, 0.05f, 1.0f, 0.02f };
            p.filter.character = character;

            SynthEngine engine;
            engine.prepare (48000.0);
            engine.setParams (p);
            engine.noteOn (57, 1.0f); // 220 Hz

            juce::AudioBuffer<float> out (2, 48000);
            for (int pos = 0, block = 0; pos < 48000; pos += 64, ++block)
            {
                const bool high = (block / 12) % 2 == 0;
                p.filter.cutoffHz = high ? 16000.0f : 400.0f;
                p.filter.resonance = high ? 0.1f : 0.6f;
                engine.setParams (p);
                engine.render (out.getWritePointer (0, pos), out.getWritePointer (1, pos), 64);
            }

            // 220 Hz の正弦波の最大傾き (レゾナンスによる多少の持ち上がりを許容)
            const float sineSlope = polybrisk::Voice::voiceGain * 0.70710678f
                                  * static_cast<float> (2.0 * juce::MathConstants<double>::pi * 220.0 / 48000.0);
            expect (polybrisk::test::allFinite (out));
            expect (polybrisk::test::maxAbsDelta (out.getReadPointer (0, 2400), 45600) < sineSlope * 2.5f);
        }

        beginTest ("Engine: filtered tone is consistent across sample rates");
        {
            auto p = sawPatch();
            p.filter.cutoffHz = 1200.0f;
            p.filter.resonance = 0.3f;

            std::vector<double> levels;
            for (const double rate : { 44100.0, 48000.0, 96000.0 })
            {
                const int n = static_cast<int> (rate);
                const auto out = renderNote (rate, p, 45, 1.0f, n); // 110 Hz
                levels.push_back (toDb (polybrisk::test::rms (out.getReadPointer (0, n / 2), n / 2)));
            }

            expectWithinAbsoluteError (levels[1], levels[0], 0.3);
            expectWithinAbsoluteError (levels[2], levels[0], 0.3);
        }

        beginTest ("Engine: every character x mode with extreme settings is finite and bounded at all rates");
        for (const double rate : { 44100.0, 48000.0, 96000.0 })
        {
            for (const auto character : allCharacters)
            {
                for (const auto mode : allModes)
                {
                    auto p = sawPatch();
                    p.oscA.unisonVoices = 7;
                    p.oscB.enabled = true;
                    p.oscB.waveform = Waveform::square;
                    p.filter.character = character;
                    p.filter.mode = mode;
                    p.filter.cutoffHz = 900.0f;
                    p.filter.resonance = 1.0f;
                    p.filter.drive = 1.0f;
                    p.filter.envAmount = 1.0f;
                    p.filter.keyTrack = 1.0f;
                    p.filter.velocity = 1.0f;
                    p.filterEnv = { 0.0001f, 0.02f, 0.0f, 0.01f };

                    SynthEngine engine;
                    engine.prepare (rate);
                    engine.setParams (p);
                    for (int n = 0; n < 16; ++n)
                        engine.noteOn (24 + n * 6, 1.0f);

                    juce::AudioBuffer<float> out (2, 9600);
                    for (int pos = 0; pos < 9600; pos += 96)
                        engine.render (out.getWritePointer (0, pos), out.getWritePointer (1, pos), 96);

                    expect (polybrisk::test::allFinite (out));
                    expect (polybrisk::test::peakLevel (out) <= 1.0f);
                }
            }
        }
    }
};

FilterTests filterTests;
} // namespace
