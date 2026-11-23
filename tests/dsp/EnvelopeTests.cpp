#include "TestHelpers.h"
#include "synth/Envelopes.h"

namespace
{
using polybrisk::Adsr;
using polybrisk::AdsrCoefficients;
using polybrisk::AdsrParams;

class EnvelopeTests final : public juce::UnitTest
{
public:
    EnvelopeTests() : juce::UnitTest ("ADSR envelope", "dsp") {}

    void runTest() override
    {
        for (const double sampleRate : { 44100.0, 48000.0, 96000.0 })
        {
            const auto rate = " @ " + juce::String (sampleRate);

            beginTest ("Attack / decay / sustain / release timing" + rate);
            {
                for (const AdsrParams params : { AdsrParams { 0.010f, 0.100f, 0.5f, 0.200f },
                                                 AdsrParams { 0.0001f, 0.001f, 0.0f, 0.001f },
                                                 AdsrParams { 0.250f, 0.500f, 0.8f, 1.000f } })
                {
                    const auto c = AdsrCoefficients::make (params, sampleRate);
                    Adsr env;
                    expect (! env.isActive());
                    expectEquals (env.next (c), 0.0f);

                    // Attack: 指定時間で 1.0 に到達
                    env.noteOn();
                    int attackSamples = 0;
                    float previous = 0.0f;
                    while (env.getStage() == Adsr::Stage::attack && attackSamples < 10000000)
                    {
                        const float v = env.next (c);
                        expect (v >= previous);
                        previous = v;
                        ++attackSamples;
                    }
                    expectWithinAbsoluteError (static_cast<double> (attackSamples), std::max (1.0, params.attack * sampleRate),
                                               2.0 + params.attack * sampleRate * 0.01);
                    expectEquals (env.getLevel(), 1.0f);

                    // Decay: 指定時間で sustain との差が -60 dB
                    const int decaySamples = static_cast<int> (params.decay * sampleRate);
                    for (int i = 0; i < decaySamples; ++i)
                        env.next (c);
                    const float span = 1.0f - params.sustain;
                    expectWithinAbsoluteError (env.getLevel() - params.sustain, span * 0.001f, span * 0.0005f + 1.0e-6f);

                    // Sustain: 保持される
                    for (int i = 0; i < static_cast<int> (sampleRate); ++i)
                        env.next (c);
                    expectWithinAbsoluteError (env.getLevel(), params.sustain, 1.0e-6f);
                    // Sustain 0 は減衰しきった時点で停止する (押さえたままの Stab がボイスを占有し続けない)
                    expect (env.isActive() == (params.sustain > 0.0f));
                    expect (! env.isReleasing());

                    // Release: 指定時間で -60 dB、その後停止して完全に 0
                    const float start = env.getLevel();
                    env.noteOff();
                    expect (env.isReleasing() || start == 0.0f);

                    if (start > 0.01f)
                    {
                        const int releaseSamples = static_cast<int> (params.release * sampleRate);
                        for (int i = 0; i < releaseSamples; ++i)
                            env.next (c);

                        const double db = juce::Decibels::gainToDecibels (env.getLevel() / start, -200.0f);
                        expectWithinAbsoluteError (db, -60.0, 1.5, "release reached " + juce::String (db, 2) + " dB");
                    }

                    for (int i = 0; i < static_cast<int> (params.release * sampleRate * 2.0) + 16 && env.isActive(); ++i)
                        env.next (c);

                    expect (! env.isActive());
                    expectEquals (env.getLevel(), 0.0f);
                    expectEquals (env.next (c), 0.0f);
                }
            }
        }

        beginTest ("A 10 s decay still converges to the sustain level (no precision stall)");
        {
            const auto c = AdsrCoefficients::make ({ 0.001f, 10.0f, 0.8f, 0.1f }, 96000.0);
            Adsr env;
            env.noteOn();
            for (int i = 0; i < 96000 * 25; ++i)
                env.next (c);
            expectWithinAbsoluteError (env.getLevel(), 0.8f, 1.0e-6f);
        }

        beginTest ("Release during attack starts from the current level; retrigger restarts the attack");
        {
            const auto c = AdsrCoefficients::make ({ 0.100f, 0.100f, 0.5f, 0.100f }, 48000.0);
            Adsr env;
            env.noteOn();
            for (int i = 0; i < 2400; ++i)
                env.next (c);

            const float atRelease = env.getLevel();
            expectWithinAbsoluteError (atRelease, 0.5f, 0.01f);

            env.noteOff();
            const float next = env.next (c);
            expect (next < atRelease && next > atRelease * 0.99f); // 段差なし

            env.noteOn();
            expect (env.getStage() == Adsr::Stage::attack);
            expect (env.next (c) > next);
        }

        beginTest ("Extreme and invalid settings stay finite");
        {
            for (const AdsrParams params : { AdsrParams { 0.0f, 0.0f, 2.0f, 0.0f },
                                             AdsrParams { -1.0f, -1.0f, -1.0f, -1.0f },
                                             AdsrParams { 10.0f, 10.0f, 1.0f, 20.0f } })
            {
                const auto c = AdsrCoefficients::make (params, 48000.0);
                Adsr env;
                env.noteOn();

                bool ok = true;
                for (int i = 0; i < 4800; ++i)
                {
                    const float v = env.next (c);
                    ok = ok && std::isfinite (v) && v >= 0.0f && v <= 1.0f;
                }

                env.noteOff();
                for (int i = 0; i < 4800; ++i)
                {
                    const float v = env.next (c);
                    ok = ok && std::isfinite (v) && v >= 0.0f && v <= 1.0f;
                }

                expect (ok);
            }

            expect (AdsrCoefficients::make ({}, 0.0).attackStep > 0.0);
        }
    }
};

EnvelopeTests envelopeTests;
} // namespace
