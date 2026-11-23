#include "TestHelpers.h"
#include "synth/SynthEngine.h"

#include <vector>

namespace
{
using polybrisk::EngineParams;
using polybrisk::SynthEngine;
using polybrisk::Waveform;

/** エンジン単体をレンダリングする小さなハーネス。 */
struct Harness
{
    explicit Harness (double sr = 48000.0, EngineParams p = sineParams()) : sampleRate (sr), params (p)
    {
        engine.prepare (sampleRate);
        engine.setParams (params);
    }

    static EngineParams sineParams()
    {
        EngineParams p;
        p.oscA.waveform = Waveform::sine;
        p.amp = { 0.002f, 0.05f, 1.0f, 0.05f };
        return p;
    }

    void apply() { engine.setParams (params); }

    juce::AudioBuffer<float> render (int numSamples, int chunkSize = 512)
    {
        juce::AudioBuffer<float> out (2, numSamples);

        for (int pos = 0; pos < numSamples; pos += chunkSize)
            engine.render (out.getWritePointer (0, pos), out.getWritePointer (1, pos), std::min (chunkSize, numSamples - pos));

        return out;
    }

    int ms (double milliseconds) const { return static_cast<int> (sampleRate * milliseconds / 1000.0); }

    double sampleRate;
    EngineParams params;
    SynthEngine engine;
};

class EngineTests final : public juce::UnitTest
{
public:
    EngineTests() : juce::UnitTest ("SynthEngine", "dsp") {}

    void runTest() override
    {
        beginTest ("MIDI note to frequency");
        expectWithinAbsoluteError (SynthEngine::midiNoteToHz (69), 440.0, 1.0e-9);
        expectWithinAbsoluteError (SynthEngine::midiNoteToHz (57), 220.0, 1.0e-9);
        expectWithinAbsoluteError (SynthEngine::midiNoteToHz (60), 261.6255653, 1.0e-6);

        beginTest ("Single note: correct pitch for every pitched waveform and sample rate");
        for (const auto wave : { Waveform::saw, Waveform::superSaw, Waveform::pulse, Waveform::square, Waveform::triangle, Waveform::sine })
        {
            for (const double sampleRate : { 44100.0, 48000.0, 96000.0 })
            {
                for (const int note : { 33, 60, 69, 96 })
                {
                    Harness h (sampleRate);
                    h.params.oscA.waveform = wave;
                    h.apply();
                    h.engine.noteOn (note, 1.0f);

                    const int numSamples = static_cast<int> (sampleRate);
                    const auto out = h.render (numSamples);
                    const double expected = SynthEngine::midiNoteToHz (note);
                    const double measured = polybrisk::test::estimateFrequency (out.getReadPointer (0), numSamples, sampleRate);
                    expectWithinAbsoluteError (measured, expected, expected * 0.0005); // 1 cent 未満
                }
            }
        }

        beginTest ("Octave / Semitone / Fine shift the pitch exactly; OSC B is independent");
        {
            struct Tuning { int octave; int semitone; float fine; };

            for (const Tuning t : { Tuning { 1, 0, 0.0f }, Tuning { -2, 0, 0.0f }, Tuning { 0, 7, 0.0f },
                                    Tuning { 0, -12, 0.0f }, Tuning { 0, 0, 50.0f }, Tuning { 1, -5, -37.0f } })
            {
                Harness h;
                h.params.oscA.enabled = false;
                h.params.oscB.enabled = true;
                h.params.oscB.waveform = Waveform::sine;
                h.params.oscB.octave = t.octave;
                h.params.oscB.semitone = t.semitone;
                h.params.oscB.fineCents = t.fine;
                h.params.oscA.octave = 2; // 無効な OSC A の設定は影響しない
                h.apply();
                h.engine.noteOn (69, 1.0f);

                const auto out = h.render (48000);
                const double expected = SynthEngine::midiNoteToHz (69.0 + 12.0 * t.octave + t.semitone + t.fine / 100.0);
                const double measured = polybrisk::test::estimateFrequency (out.getReadPointer (0), 48000, 48000.0);
                expectWithinAbsoluteError (measured, expected, expected * 0.0005);
            }
        }

        beginTest ("16-note chord: every note sounds, then everything releases to exact silence");
        {
            Harness h;
            std::vector<int> notes;
            for (int i = 0; i < polybrisk::maxPolyphony; ++i)
                notes.push_back (40 + i * 3);

            // リミッターの線形領域に確実に収まるベロシティで弾く (スペクトルの純度を検査するため)
            for (const int n : notes)
                h.engine.noteOn (n, 0.3f);

            expectEquals (h.engine.getActiveVoiceCount(), 16);

            const int order = 15;
            const auto out = h.render ((1 << order) + 4800);
            expect (polybrisk::test::allFinite (out));
            expect (polybrisk::test::peakLevel (out) < SynthEngine::limiterThreshold);

            const auto spectrum = polybrisk::test::magnitudeSpectrum (out.getReadPointer (0) + 4800, order);
            float strongest = 0.0f;
            for (const int n : notes)
                strongest = std::max (strongest, polybrisk::test::magnitudeNear (spectrum, 48000.0, SynthEngine::midiNoteToHz (n)));

            for (const int n : notes)
            {
                const float m = polybrisk::test::magnitudeNear (spectrum, 48000.0, SynthEngine::midiNoteToHz (n));
                expect (m > strongest * 0.5f, "note " + juce::String (n) + " missing from the chord");
            }

            // 鳴らしていない音程 (隣の音から窓のメインローブ幅以上離れた C6) には成分がない
            expect (polybrisk::test::magnitudeNear (spectrum, 48000.0, SynthEngine::midiNoteToHz (84)) < strongest * 0.001f);

            for (const int n : notes)
                h.engine.noteOff (n);

            h.render (h.ms (200));
            expectEquals (h.engine.getActiveVoiceCount(), 0);
            expect (h.engine.isSilent());
            expectEquals (polybrisk::test::peakLevel (h.render (4800)), 0.0f);
        }

        beginTest ("Duplicate note-ons are tracked as independent voices");
        {
            Harness h;
            h.params.amp.release = 0.5f;
            h.apply();

            h.engine.noteOn (60, 1.0f);
            h.render (480);
            h.engine.noteOn (60, 1.0f);
            h.render (480);
            expectEquals (h.engine.getActiveVoiceCount(), 2);

            h.engine.noteOff (60);
            h.render (480);
            expectEquals (countKeyDown (h.engine), 1);
            expectEquals (countReleasing (h.engine), 1);

            // 先に押した方 (最古) から離される
            for (int i = 0; i < polybrisk::maxPolyphony; ++i)
                if (h.engine.getVoice (i).isReleasing())
                    expectEquals (static_cast<int> (h.engine.getVoice (i).getAge()), 1);

            h.engine.noteOff (60);
            h.render (480);
            expectEquals (countKeyDown (h.engine), 0);
            expectEquals (countReleasing (h.engine), 2);

            h.engine.noteOff (60); // 余分な Note Off は無害
            h.engine.noteOff (61);
            h.render (h.ms (1200));
            expectEquals (h.engine.getActiveVoiceCount(), 0);
        }

        beginTest ("Sustain pedal holds released notes until the pedal is lifted");
        {
            Harness h;
            h.engine.noteOn (60, 1.0f);
            h.engine.noteOn (64, 1.0f);
            h.engine.setSustainPedal (true);
            h.engine.noteOff (60);
            h.render (h.ms (500));

            expectEquals (h.engine.getActiveVoiceCount(), 2);
            expectEquals (countReleasing (h.engine), 0);
            expect (polybrisk::test::peakLevel (h.render (4800)) > 0.1f);

            // ペダル中に弾いて離したノートも保持される
            h.engine.noteOn (67, 1.0f);
            h.engine.noteOff (67);
            h.render (h.ms (300));
            expectEquals (h.engine.getActiveVoiceCount(), 3);

            // ペダルを離すと、鍵盤を押さえたままの 64 だけが残る
            h.engine.setSustainPedal (false);
            h.render (h.ms (300));
            expectEquals (h.engine.getActiveVoiceCount(), 1);
            for (int i = 0; i < polybrisk::maxPolyphony; ++i)
                if (h.engine.getVoice (i).isActive())
                    expectEquals (h.engine.getVoice (i).getNote(), 64);

            h.engine.noteOff (64);
            h.render (h.ms (300));
            expectEquals (h.engine.getActiveVoiceCount(), 0);

            // ペダルを踏んでいないときは通常どおり Release
            h.engine.noteOn (72, 1.0f);
            h.engine.noteOff (72);
            h.render (h.ms (300));
            expectEquals (h.engine.getActiveVoiceCount(), 0);
        }

        beginTest ("Voice steal: quietest releasing voice first, otherwise the oldest");
        {
            Harness h;
            h.params.amp.release = 2.0f;
            h.apply();

            for (int i = 0; i < 16; ++i)
            {
                h.engine.noteOn (40 + i, 1.0f);
                h.render (64);
            }

            // 2 つを時間差で離す: 先に離した 45 の方が小さくなる
            h.engine.noteOff (45);
            h.render (h.ms (300));
            h.engine.noteOff (50);
            h.render (h.ms (50));
            expectEquals (countReleasing (h.engine), 2);

            h.engine.noteOn (100, 1.0f);
            h.render (h.ms (20));
            expectEquals (h.engine.getActiveVoiceCount(), 16);
            expect (! hasNote (h.engine, 45), "the quietest releasing voice should have been stolen");
            expect (hasNote (h.engine, 50));
            expect (hasNote (h.engine, 100));

            h.engine.noteOn (101, 1.0f);
            h.render (h.ms (20));
            expect (! hasNote (h.engine, 50));
            expect (hasNote (h.engine, 101));

            // Release 中のボイスがなくなったら最古 (40) を奪う
            expectEquals (countReleasing (h.engine), 0);
            h.engine.noteOn (102, 1.0f);
            h.render (h.ms (20));
            expect (! hasNote (h.engine, 40));
            expect (hasNote (h.engine, 41));
            expect (hasNote (h.engine, 102));
            expectEquals (h.engine.getActiveVoiceCount(), 16);

            // 同じサンプル位置で 16 音を一度に弾き直しても破綻しない
            for (int i = 0; i < 16; ++i)
                h.engine.noteOn (70 + i, 1.0f);
            const auto out = h.render (h.ms (50));
            expect (polybrisk::test::allFinite (out));
            expectEquals (h.engine.getActiveVoiceCount(), 16);
            for (int i = 0; i < 16; ++i)
                expect (hasNote (h.engine, 70 + i));
        }

        beginTest ("Voice steal is declicked");
        {
            Harness h;
            h.params.amp = { 0.002f, 0.05f, 1.0f, 0.5f };
            h.apply();

            for (int i = 0; i < 16; ++i)
                h.engine.noteOn (48 + i, 1.0f);

            h.render (h.ms (100));
            const auto steady = h.render (h.ms (100));
            const float steadyDelta = polybrisk::test::maxAbsDelta (steady.getReadPointer (0), steady.getNumSamples());

            float stealDelta = 0.0f;
            for (int i = 0; i < 8; ++i)
            {
                h.engine.noteOn (50 + i * 2, 1.0f);
                const auto out = h.render (h.ms (15), 37);
                stealDelta = std::max (stealDelta, polybrisk::test::maxAbsDelta (out.getReadPointer (0), out.getNumSamples()));
                expectEquals (h.engine.getActiveVoiceCount(), 16);
            }

            logMessage ("Max sample delta: steady " + juce::String (steadyDelta, 4) + ", while stealing " + juce::String (stealDelta, 4));
            expect (stealDelta < steadyDelta * 1.5f + 0.01f);
        }

        beginTest ("A note released while waiting for a stolen voice does not hang");
        {
            Harness h;
            for (int i = 0; i < 16; ++i)
                h.engine.noteOn (40 + i, 1.0f);
            h.render (h.ms (50));

            h.engine.noteOn (90, 1.0f);
            h.engine.noteOff (90); // スティールのフェード中に離される
            for (int i = 0; i < 16; ++i)
                h.engine.noteOff (40 + i);

            h.render (h.ms (400));
            expectEquals (h.engine.getActiveVoiceCount(), 0);
        }

        beginTest ("OSC A / B mixing, levels and enable switches");
        {
            const auto level = [] (bool a, bool b, float levelA, float levelB)
            {
                Harness h;
                h.params.oscA.enabled = a;
                h.params.oscA.level = levelA;
                h.params.oscB.enabled = b;
                h.params.oscB.level = levelB;
                h.params.oscB.waveform = Waveform::sine;
                h.params.oscB.semitone = 7;
                h.apply();
                h.engine.noteOn (60, 1.0f);
                h.render (4800);
                const auto out = h.render (48000);
                return polybrisk::test::rms (out.getReadPointer (0), 48000);
            };

            const double onlyA = level (true, false, 1.0f, 1.0f);
            const double onlyB = level (false, true, 1.0f, 1.0f);
            const double both = level (true, true, 1.0f, 1.0f);
            const double halfA = level (true, false, 0.5f, 1.0f);

            expect (onlyA > 0.05);
            expectWithinAbsoluteError (onlyB, onlyA, onlyA * 0.01);
            expectWithinAbsoluteError (both, onlyA * std::sqrt (2.0), onlyA * 0.02); // 無相関な 2 音のパワー加算
            expectWithinAbsoluteError (halfA, onlyA * 0.5, onlyA * 0.01);
            expectWithinAbsoluteError (level (false, false, 1.0f, 1.0f), 0.0, 1.0e-9);
            expectWithinAbsoluteError (level (true, true, 0.0f, 0.0f), 0.0, 1.0e-9);
        }

        beginTest ("Unison through the engine: mono at 1 voice, wide with spread, level stable");
        {
            const auto renderUnison = [] (int voices, float spread)
            {
                Harness h;
                h.params.oscA.waveform = Waveform::superSaw;
                h.params.oscA.unisonVoices = voices;
                h.params.oscA.spread = spread;
                h.params.oscA.retrigger = false;
                h.apply();
                h.engine.noteOn (57, 1.0f);
                h.render (4800);
                return h.render (96000);
            };

            const auto single = renderUnison (1, 1.0f);
            const auto seven = renderUnison (7, 1.0f);

            float singleDiff = 0.0f, sevenDiff = 0.0f;
            for (int i = 0; i < 96000; ++i)
            {
                singleDiff = std::max (singleDiff, std::abs (single.getSample (0, i) - single.getSample (1, i)));
                sevenDiff = std::max (sevenDiff, std::abs (seven.getSample (0, i) - seven.getSample (1, i)));
            }

            expectEquals (singleDiff, 0.0f);
            expect (sevenDiff > 0.05f);

            const double singleRms = polybrisk::test::rms (single.getReadPointer (0), 96000);
            const double sevenRms = polybrisk::test::rms (seven.getReadPointer (0), 96000);
            expect (std::abs (juce::Decibels::gainToDecibels (sevenRms / singleRms)) < 1.5);
        }

        beginTest ("Phase mode: Retrigger repeats the attack exactly, Free does not");
        {
            const auto firstAndSecond = [] (bool retrigger)
            {
                Harness h;
                h.params.oscA.waveform = Waveform::saw;
                h.params.oscA.unisonVoices = 5;
                h.params.oscA.retrigger = retrigger;
                h.params.amp = { 0.001f, 0.05f, 1.0f, 0.005f };
                h.apply();

                std::vector<juce::AudioBuffer<float>> takes;
                for (int i = 0; i < 2; ++i)
                {
                    h.engine.noteOn (60, 1.0f);
                    takes.push_back (h.render (2048, 32));
                    h.engine.noteOff (60);
                    h.render (4800 + i * 17, 32); // 完全に消えるまで待つ (ずらして位相を進める)
                }

                float diff = 0.0f;
                for (int i = 0; i < 2048; ++i)
                    diff = std::max (diff, std::abs (takes[0].getSample (0, i) - takes[1].getSample (0, i)));
                return diff;
            };

            expectEquals (firstAndSecond (true), 0.0f);
            expect (firstAndSecond (false) > 0.01f);
        }

        beginTest ("Velocity scales the level linearly");
        {
            const auto level = [] (float velocity)
            {
                Harness h;
                h.engine.noteOn (60, velocity);
                h.render (4800);
                return polybrisk::test::peakLevel (h.render (9600));
            };

            expectWithinAbsoluteError (level (0.5f), level (1.0f) * 0.5f, 0.002f);
            expectWithinAbsoluteError (level (1.0f), polybrisk::Voice::voiceGain * 0.70710678f, 0.002f);
        }

        beginTest ("Soft limiter: transparent below the threshold, never exceeds full scale");
        {
            for (float x = -SynthEngine::limiterThreshold; x <= SynthEngine::limiterThreshold; x += 0.01f)
                expectEquals (SynthEngine::softLimit (x), x);

            float previous = -2.0f;
            for (float x = -50.0f; x <= 50.0f; x += 0.01f)
            {
                const float y = SynthEngine::softLimit (x);
                expect (y >= previous && std::abs (y) <= 1.0f);
                previous = y;
            }

            expect (std::abs (SynthEngine::softLimit (1.0e9f)) <= 1.0f);

            // 最悪条件: 16 音 × 2 OSC × 9 ユニゾン、+12 dB
            Harness h;
            for (auto* o : { &h.params.oscA, &h.params.oscB })
            {
                o->enabled = true;
                o->waveform = Waveform::saw;
                o->unisonVoices = 9;
                o->level = 1.0f;
                o->spread = 1.0f;
                o->detune = 1.0f;
            }
            h.params.outputGain = 4.0f;
            h.apply();

            for (int i = 0; i < 16; ++i)
                h.engine.noteOn (36 + i * 4, 1.0f);

            const auto out = h.render (48000);
            expect (polybrisk::test::allFinite (out));
            expect (polybrisk::test::peakLevel (out) <= 1.0f);
            expect (polybrisk::test::peakLevel (out) > 0.9f);
        }

        beginTest ("A typical chord stays below the limiter threshold");
        {
            Harness h;
            h.params.oscA.waveform = Waveform::superSaw;
            h.params.oscA.unisonVoices = 7;
            h.params.oscA.retrigger = false;
            h.apply();

            for (const int n : { 48, 55, 60, 64, 67, 72 })
                h.engine.noteOn (n, 0.8f);

            const auto out = h.render (96000);
            const float peak = polybrisk::test::peakLevel (out);
            logMessage ("6-note SuperSAW chord (velocity 0.8): peak " + juce::String (juce::Decibels::gainToDecibels (peak), 1) + " dBFS");
            expect (peak < SynthEngine::limiterThreshold);
        }

        beginTest ("All Notes Off releases even sustained voices; All Sound Off mutes quickly");
        {
            Harness h;
            h.params.amp.release = 5.0f;
            h.apply();
            h.engine.setSustainPedal (true);
            h.engine.noteOn (60, 1.0f);
            h.engine.noteOn (64, 1.0f);
            h.engine.noteOff (64);
            h.render (4800);

            h.engine.allNotesOff();
            h.render (64);
            expectEquals (countReleasing (h.engine), 2);
            expectEquals (h.engine.getActiveVoiceCount(), 2); // 長い Release の途中

            h.engine.allSoundOff();
            const auto fade = h.render (h.ms (5));
            expect (polybrisk::test::maxAbsDelta (fade.getReadPointer (0), fade.getNumSamples()) < 0.05f);
            expectEquals (h.engine.getActiveVoiceCount(), 0);
            h.render (512); // デシメーターの余韻を出し切る
            expectEquals (polybrisk::test::peakLevel (h.render (4800)), 0.0f);
        }

        beginTest ("Output does not depend on how the block is split");
        {
            const auto renderWith = [] (const std::vector<int>& chunkPattern)
            {
                Harness h;
                for (auto* o : { &h.params.oscA, &h.params.oscB })
                {
                    o->enabled = true;
                    o->waveform = Waveform::superSaw;
                    o->unisonVoices = 7;
                }
                h.params.oscB.octave = 1;
                h.apply();

                const int total = 24000;
                juce::AudioBuffer<float> out (2, total);
                int pos = 0;
                size_t index = 0;

                while (pos < total)
                {
                    if (pos == 0)
                        for (const int n : { 48, 52, 55 })
                            h.engine.noteOn (n, 1.0f);

                    const int n = std::min (chunkPattern[index++ % chunkPattern.size()], total - pos);
                    h.engine.render (out.getWritePointer (0, pos), out.getWritePointer (1, pos), n);
                    pos += n;
                }

                return out;
            };

            const auto reference = renderWith ({ 24000 });

            for (const auto& pattern : std::vector<std::vector<int>> { { 1 }, { 32 }, { 31, 33 }, { 7, 480, 1, 64, 129 } })
            {
                const auto out = renderWith (pattern);
                float diff = 0.0f;
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < 24000; ++i)
                        diff = std::max (diff, std::abs (out.getSample (ch, i) - reference.getSample (ch, i)));

                expectEquals (diff, 0.0f);
            }
        }

        beginTest ("Parameter changes while playing are smooth and finite");
        {
            Harness h;
            h.engine.noteOn (57, 1.0f);
            h.render (9600);
            const auto before = h.render (4800);
            const float steadyDelta = polybrisk::test::maxAbsDelta (before.getReadPointer (0), 4800);

            // 出力ゲインと OSC レベルを急変させる
            h.params.outputGain = 0.1f;
            h.params.oscA.level = 0.2f;
            h.apply();
            const auto gainChange = h.render (4800);
            expect (polybrisk::test::maxAbsDelta (gainChange.getReadPointer (0), 4800) < steadyDelta * 1.2f);

            // OSC を OFF にしてもクリックしない
            h.params.oscA.enabled = false;
            h.apply();
            const auto disabled = h.render (9600);
            expect (polybrisk::test::maxAbsDelta (disabled.getReadPointer (0), 9600) < steadyDelta * 1.2f);
            expect (polybrisk::test::peakLevel (disabled, 4800, 4800) < 1.0e-4f);

            // あらゆる離散・連続パラメーターを動かし続けても有限
            juce::Random random (42);
            bool finite = true;
            for (int i = 0; i < 400; ++i)
            {
                for (auto* o : { &h.params.oscA, &h.params.oscB })
                {
                    o->enabled = random.nextBool();
                    o->waveform = static_cast<Waveform> (random.nextInt (7));
                    o->octave = random.nextInt (5) - 2;
                    o->semitone = random.nextInt (25) - 12;
                    o->fineCents = random.nextFloat() * 200.0f - 100.0f;
                    o->level = random.nextFloat();
                    o->unisonVoices = random.nextInt (9) + 1;
                    o->detune = random.nextFloat();
                    o->spread = random.nextFloat();
                    o->retrigger = random.nextBool();
                    o->pulseWidth = random.nextFloat();
                }

                h.params.amp = { random.nextFloat() * 0.05f, random.nextFloat() * 0.2f, random.nextFloat(), random.nextFloat() * 0.2f };
                h.params.outputGain = random.nextFloat() * 4.0f;
                h.apply();

                if (random.nextInt (3) == 0)
                    h.engine.noteOn (random.nextInt (128), random.nextFloat());
                if (random.nextInt (3) == 0)
                    h.engine.noteOff (random.nextInt (128));
                if (random.nextInt (20) == 0)
                    h.engine.setSustainPedal (random.nextBool());

                const auto out = h.render (random.nextInt (300) + 1, 64);
                finite = finite && polybrisk::test::allFinite (out) && polybrisk::test::peakLevel (out) <= 1.0f;
            }
            expect (finite);
        }

        beginTest ("Out-of-range notes and reset are safe");
        {
            Harness h;
            h.engine.noteOn (-5, 1.0f);
            h.engine.noteOn (500, 2.0f);
            h.engine.noteOff (-5);
            expect (polybrisk::test::allFinite (h.render (4800)));

            h.engine.reset();
            expectEquals (h.engine.getActiveVoiceCount(), 0);
            expectEquals (polybrisk::test::peakLevel (h.render (480)), 0.0f);
        }
    }

private:
    static int countKeyDown (const SynthEngine& engine)
    {
        int n = 0;
        for (int i = 0; i < polybrisk::maxPolyphony; ++i)
            if (engine.getVoice (i).isActive() && engine.getVoice (i).isKeyDown())
                ++n;
        return n;
    }

    static int countReleasing (const SynthEngine& engine)
    {
        int n = 0;
        for (int i = 0; i < polybrisk::maxPolyphony; ++i)
            if (engine.getVoice (i).isReleasing())
                ++n;
        return n;
    }

    static bool hasNote (const SynthEngine& engine, int note)
    {
        for (int i = 0; i < polybrisk::maxPolyphony; ++i)
            if (engine.getVoice (i).isActive() && engine.getVoice (i).getNote() == note)
                return true;
        return false;
    }
};

EngineTests engineTests;
} // namespace
