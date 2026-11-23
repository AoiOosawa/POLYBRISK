#include "PluginProcessor.h"
#include "TestHelpers.h"
#include "fx/Phaser.h"
#include "rhythm/GatePattern.h"
#include "state/Parameters.h"
#include "synth/Lfo.h"
#include "synth/SynthEngine.h"

#include <cmath>
#include <random>
#include <vector>

/** D-041 で追加した機能: LFO / Mono・Legato・Glide / Sub・Noise / Aftertouch / Phaser / Gate の 1/64 とランダム。 */
namespace
{
using namespace polybrisk;
using polybrisk::test::TimedMidi;
namespace ids = polybrisk::params::id;

constexpr double sr = 48000.0;

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

/** [start, start + length) の区間の基本周波数。 */
double frequencyAt (const juce::AudioBuffer<float>& buffer, double startSeconds, double lengthSeconds)
{
    return polybrisk::test::estimateFrequency (buffer.getReadPointer (0, static_cast<int> (startSeconds * sr)),
                                               static_cast<int> (lengthSeconds * sr), sr);
}

double rmsAt (const juce::AudioBuffer<float>& buffer, double startSeconds, double lengthSeconds)
{
    return polybrisk::test::rms (buffer.getReadPointer (0, static_cast<int> (startSeconds * sr)), static_cast<int> (lengthSeconds * sr));
}

double cents (double f, double reference) { return 1200.0 * std::log2 (f / reference); }

class ModulationTests final : public juce::UnitTest
{
public:
    ModulationTests() : juce::UnitTest ("Modulation (LFO / mono / glide / sub / aftertouch / phaser)", "dsp") {}

    void runTest() override
    {
        const double a4 = SynthEngine::midiNoteToHz (69);

        beginTest ("LFO shapes: range, start values and a stateless sample & hold");
        {
            for (const auto wave : { LfoWave::sine, LfoWave::triangle, LfoWave::saw, LfoWave::square, LfoWave::sampleHold })
                for (double phase = -3.0; phase < 5.0; phase += 0.0137)
                {
                    const double v = lfo::value (wave, phase, 1234u);
                    expect (std::isfinite (v) && v >= -1.0 && v <= 1.0);
                }

            expectWithinAbsoluteError (lfo::value (LfoWave::sine, 0.0, 0), 0.0, 1.0e-12);
            expectWithinAbsoluteError (lfo::value (LfoWave::sine, 0.25, 0), 1.0, 1.0e-12);
            expectWithinAbsoluteError (lfo::value (LfoWave::triangle, 0.25, 0), 1.0, 1.0e-12);
            expectWithinAbsoluteError (lfo::value (LfoWave::triangle, 0.75, 0), -1.0, 1.0e-12);
            expectWithinAbsoluteError (lfo::value (LfoWave::saw, 0.0, 0), 1.0, 1.0e-12);
            expectWithinAbsoluteError (lfo::value (LfoWave::square, 0.1, 0), 1.0, 1.0e-12);
            expectWithinAbsoluteError (lfo::value (LfoWave::square, 0.6, 0), -1.0, 1.0e-12);

            // S&H: 同じ周期の中では一定、周期が変われば別の値、同じ入力なら同じ値
            expectEquals (lfo::value (LfoWave::sampleHold, 3.1, 7u), lfo::value (LfoWave::sampleHold, 3.9, 7u));
            expect (std::abs (lfo::value (LfoWave::sampleHold, 3.5, 7u) - lfo::value (LfoWave::sampleHold, 4.5, 7u)) > 1.0e-9);
            expect (std::abs (lfo::value (LfoWave::sampleHold, 3.5, 7u) - lfo::value (LfoWave::sampleHold, 3.5, 8u)) > 1.0e-9);
        }

        beginTest ("LFO with every depth at 0 leaves the output bit-identical, whatever the other LFO settings");
        {
            auto p = sinePatch();
            p.oscA.waveform = Waveform::pulse;
            p.oscA.unisonVoices = 5;
            p.filter.cutoffHz = 2000.0f;

            SynthEngine plain;
            plain.prepare (sr);
            plain.setParams (p);
            plain.noteOn (57, 0.8f);
            const auto a = render (plain, 24000);

            p.lfo.wave = LfoWave::square;
            p.lfo.rateHz = 9.0f;
            p.lfo.retrigger = true;
            p.lfo.fadeSeconds = 0.2f;
            p.voiceMode = VoiceMode::poly;
            p.sub.octave = -2;
            p.noise.color = 0.9f;

            SynthEngine withLfo;
            withLfo.prepare (sr);
            withLfo.setParams (p);
            withLfo.noteOn (57, 0.8f);
            const auto b = render (withLfo, 24000);

            bool identical = true;
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 24000; ++i)
                    identical = identical && juce::exactlyEqual (a.getSample (ch, i), b.getSample (ch, i));
            expect (identical);
        }

        beginTest ("LFO to pitch: a key-synced square alternates between +depth and -depth");
        {
            auto p = sinePatch();
            p.lfo.wave = LfoWave::square;
            p.lfo.rateHz = 1.0f;
            p.lfo.retrigger = true;
            p.lfo.toPitchCents = 100.0f;

            SynthEngine engine;
            engine.prepare (sr);
            engine.setParams (p);
            engine.noteOn (69, 0.8f);
            const auto out = render (engine, 48000);

            expectWithinAbsoluteError (cents (frequencyAt (out, 0.1, 0.3), a4), 100.0, 2.0);
            expectWithinAbsoluteError (cents (frequencyAt (out, 0.6, 0.3), a4), -100.0, 2.0);
        }

        beginTest ("LFO fade in: the modulation is absent at note on and full after the fade");
        {
            auto p = sinePatch();
            p.lfo.wave = LfoWave::square;
            p.lfo.rateHz = 0.5f;        // 最初の 1 秒は +1
            p.lfo.retrigger = true;
            p.lfo.toPitchCents = 200.0f;
            p.lfo.fadeSeconds = 0.4f;

            SynthEngine engine;
            engine.prepare (sr);
            engine.setParams (p);
            engine.noteOn (69, 0.8f);
            const auto out = render (engine, 43200);

            expect (cents (frequencyAt (out, 0.005, 0.03), a4) < 20.0);
            expectWithinAbsoluteError (cents (frequencyAt (out, 0.6, 0.25), a4), 200.0, 3.0);
        }

        beginTest ("LFO to amp: 100 % square tremolo silences the low half; the transitions are click-free");
        {
            auto p = sinePatch();
            p.lfo.wave = LfoWave::square;
            p.lfo.rateHz = 2.0f;
            p.lfo.retrigger = true;
            p.lfo.toAmp = 1.0f;

            SynthEngine engine;
            engine.prepare (sr);
            engine.setParams (p);
            engine.noteOn (69, 0.8f);
            const auto out = render (engine, 48000);

            const double loud = rmsAt (out, 0.05, 0.15);
            const double quiet = rmsAt (out, 0.30, 0.15);
            expect (loud > 0.05);
            expect (quiet < loud * 0.01);

            // 440 Hz・振幅 0.144 の正弦波そのものの最大の傾きは約 0.0083。ゲインの切り替えで大きな段差が出ないこと
            expect (polybrisk::test::maxAbsDelta (out.getReadPointer (0), 48000) < 0.02f);
        }

        beginTest ("LFO to cutoff: opens and closes a low-pass filter");
        {
            auto p = sinePatch();
            p.oscA.waveform = Waveform::saw;
            p.filter.mode = FilterMode::lp24;
            p.filter.cutoffHz = 400.0f;
            p.lfo.wave = LfoWave::square;
            p.lfo.rateHz = 1.0f;
            p.lfo.retrigger = true;
            p.lfo.toCutoff = 0.75f;     // ±3 オクターブ

            SynthEngine engine;
            engine.prepare (sr);
            engine.setParams (p);
            engine.noteOn (45, 0.8f);
            const auto out = render (engine, 48000);

            // 開いている半周期 (3.2 kHz) の方が、閉じている半周期 (50 Hz) よりずっと大きい
            expect (rmsAt (out, 0.1, 0.3) > rmsAt (out, 0.6, 0.3) * 3.0);
        }

        beginTest ("LFO to pulse width changes a pulse wave and leaves a saw untouched");
        {
            const auto run = [] (Waveform wave, float depth)
            {
                auto p = sinePatch();
                p.oscA.waveform = wave;
                p.lfo.wave = LfoWave::triangle;
                p.lfo.rateHz = 3.0f;
                p.lfo.toPulseWidth = depth;

                SynthEngine engine;
                engine.prepare (sr);
                engine.setParams (p);
                engine.noteOn (57, 0.8f);
                return render (engine, 24000);
            };

            const auto difference = [] (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
            {
                float d = 0.0f;
                for (int i = 0; i < a.getNumSamples(); ++i)
                    d = std::max (d, std::abs (a.getSample (0, i) - b.getSample (0, i)));
                return d;
            };

            expect (difference (run (Waveform::pulse, 0.0f), run (Waveform::pulse, 0.8f)) > 0.05f);
            expect (difference (run (Waveform::saw, 0.0f), run (Waveform::saw, 0.8f)) < 1.0e-6f);
            expect (polybrisk::test::allFinite (run (Waveform::pulse, 1.0f)));
        }

        beginTest ("LFO tempo sync: locked to the host position, independent of block size");
        {
            // 120 BPM、1/4 = 0.5 秒周期。Square のトレモロで、前半 0.25 秒が鳴り、後半が無音
            const auto run = [] (int block)
            {
                auto p = sinePatch();
                p.lfo.wave = LfoWave::square;
                p.lfo.sync = true;
                p.lfo.divisionBeats = 1.0;
                p.lfo.toAmp = 1.0f;

                SynthEngine engine;
                engine.prepare (sr);
                engine.setParams (p);
                engine.noteOn (69, 0.8f);

                juce::AudioBuffer<float> out (2, 48000);
                for (int pos = 0; pos < 48000; pos += block)
                {
                    const int n = std::min (block, 48000 - pos);
                    engine.setTransport ({ 120.0, true, true, pos * 120.0 / (60.0 * sr) });
                    engine.render (out.getWritePointer (0, pos), out.getWritePointer (1, pos), n);
                }
                return out;
            };

            const auto out = run (256);
            for (int cycle = 0; cycle < 2; ++cycle)
            {
                expect (rmsAt (out, 0.5 * cycle + 0.05, 0.15) > 0.05);
                expect (rmsAt (out, 0.5 * cycle + 0.30, 0.15) < 0.005);
            }

            const auto other = run (37);
            float maxDiff = 0.0f;
            for (int i = 0; i < 48000; ++i)
                maxDiff = std::max (maxDiff, std::abs (out.getSample (0, i) - other.getSample (0, i)));
            expect (maxDiff < 1.0e-4f, "max diff " + juce::String (maxDiff));
        }

        beginTest ("Mono: one voice, last-note priority, returns to the held note");
        {
            auto p = sinePatch();
            p.voiceMode = VoiceMode::mono;

            SynthEngine engine;
            engine.prepare (sr);
            engine.setParams (p);

            engine.noteOn (69, 0.8f);
            render (engine, 4800);
            engine.noteOn (76, 0.8f);
            const auto second = render (engine, 9600);
            expectEquals (engine.getActiveVoiceCount(), 1);
            expectWithinAbsoluteError (cents (frequencyAt (second, 0.05, 0.1), SynthEngine::midiNoteToHz (76)), 0.0, 1.0);

            engine.noteOff (76);
            const auto back = render (engine, 9600);
            expectEquals (engine.getActiveVoiceCount(), 1);
            expectWithinAbsoluteError (cents (frequencyAt (back, 0.05, 0.1), a4), 0.0, 1.0);
            expect (rmsAt (back, 0.1, 0.05) > 0.05);

            engine.noteOff (69);
            render (engine, 24000);
            expectEquals (engine.getActiveVoiceCount(), 0);

            // Poly では 2 声になる
            p.voiceMode = VoiceMode::poly;
            engine.setParams (p);
            engine.noteOn (69, 0.8f);
            engine.noteOn (76, 0.8f);
            render (engine, 4800);
            expectEquals (engine.getActiveVoiceCount(), 2);
        }

        beginTest ("Mono retriggers the envelope, Legato does not; neither clicks");
        {
            const auto run = [] (VoiceMode mode)
            {
                auto p = sinePatch();
                p.voiceMode = mode;
                p.amp = { 0.2f, 0.05f, 1.0f, 0.02f };   // 遅い Attack
                p.filterEnv = { 0.001f, 0.1f, 0.0f, 0.1f };
                p.filter.cutoffHz = 300.0f;
                p.filter.envAmount = 0.6f;
                p.oscA.waveform = Waveform::saw;

                SynthEngine engine;
                engine.prepare (sr);
                engine.setParams (p);
                engine.noteOn (57, 0.8f);
                auto first = render (engine, 24000);     // Filter EG は減衰しきる
                engine.noteOn (57 + 12, 0.8f);
                auto second = render (engine, 4800);
                return std::pair { std::move (first), std::move (second) };
            };

            const auto mono = run (VoiceMode::mono);
            const auto legato = run (VoiceMode::legato);

            // Mono は Filter EG が再始動するので、2 音目の直後が明るい (高域が多い)。Legato は暗いまま
            const auto brightness = [] (const juce::AudioBuffer<float>& b)
            {
                double sum = 0.0;
                for (int i = 1; i < 2400; ++i)
                    sum += std::abs (b.getSample (0, i) - b.getSample (0, i - 1));
                return sum;
            };

            expect (brightness (mono.second) > brightness (legato.second) * 1.5);

            // 切り替えの瞬間に段差がない (直前の傾きと同程度)
            for (const auto* pair : { &mono, &legato })
            {
                const float before = polybrisk::test::maxAbsDelta (pair->first.getReadPointer (0, 23000), 1000);
                const float boundary = std::abs (pair->second.getSample (0, 0) - pair->first.getSample (0, 23999));
                expect (boundary < before * 3.0f + 1.0e-3f);
            }
        }

        beginTest ("Sustain pedal in Mono: the held voice moves to the new note instead of stacking voices");
        {
            auto p = sinePatch();
            p.voiceMode = VoiceMode::legato;

            SynthEngine engine;
            engine.prepare (sr);
            engine.setParams (p);
            engine.setSustainPedal (true);
            engine.noteOn (60, 0.8f);
            render (engine, 2400);
            engine.noteOff (60);
            render (engine, 2400);
            engine.noteOn (67, 0.8f);
            const auto out = render (engine, 9600);

            expectEquals (engine.getActiveVoiceCount(), 1);
            expectWithinAbsoluteError (cents (frequencyAt (out, 0.05, 0.1), SynthEngine::midiNoteToHz (67)), 0.0, 1.0);

            engine.noteOff (67);
            engine.setSustainPedal (false);
            render (engine, 24000);
            expectEquals (engine.getActiveVoiceCount(), 0);
        }

        beginTest ("Glide: the pitch moves linearly (in semitones) over the glide time, then lands exactly");
        {
            auto p = sinePatch();
            p.voiceMode = VoiceMode::mono;
            p.glideSeconds = 0.4f;

            SynthEngine engine;
            engine.prepare (sr);
            engine.setParams (p);
            engine.noteOn (69, 0.8f);
            render (engine, 9600);
            engine.noteOn (81, 0.8f);                    // 1 オクターブ上へ
            const auto out = render (engine, 38400);

            // 中間点 (0.2 秒) では 6 半音 = 600 cent 付近
            expectWithinAbsoluteError (cents (frequencyAt (out, 0.19, 0.02), a4), 600.0, 40.0);
            expect (cents (frequencyAt (out, 0.05, 0.02), a4) < 300.0);
            expectWithinAbsoluteError (cents (frequencyAt (out, 0.5, 0.2), a4), 1200.0, 1.0);
        }

        beginTest ("Glide in Poly starts each new voice from the previous note; Legato glides only when notes overlap");
        {
            auto p = sinePatch();
            p.glideSeconds = 0.3f;

            SynthEngine engine;
            engine.prepare (sr);
            engine.setParams (p);
            engine.noteOn (57, 0.8f);
            render (engine, 4800);
            engine.noteOff (57);
            render (engine, 9600);                       // 前の音は消える
            engine.noteOn (69, 0.8f);
            const auto poly = render (engine, 24000);
            expect (cents (frequencyAt (poly, 0.02, 0.02), a4) < -700.0);     // 1 オクターブ下から始まる
            expectWithinAbsoluteError (cents (frequencyAt (poly, 0.35, 0.1), a4), 0.0, 1.0);

            p.voiceMode = VoiceMode::legato;
            SynthEngine legato;
            legato.prepare (sr);
            legato.setParams (p);
            legato.noteOn (57, 0.8f);
            render (legato, 4800);
            legato.noteOff (57);
            render (legato, 9600);
            legato.noteOn (69, 0.8f);                    // 離してから弾いた: グライドしない
            const auto detached = render (legato, 4800);
            expectWithinAbsoluteError (cents (frequencyAt (detached, 0.02, 0.05), a4), 0.0, 2.0);
        }

        beginTest ("Sub oscillator: one or two octaves below the note, centred, independent of OSC tuning");
        {
            for (const int octave : { -1, -2 })
                for (const auto wave : { SubWave::sine, SubWave::square })
                {
                    auto p = sinePatch();
                    p.oscA.level = 0.0f;
                    p.oscA.semitone = 7;            // Sub には効かない
                    p.sub.level = 1.0f;
                    p.sub.octave = octave;
                    p.sub.wave = wave;

                    SynthEngine engine;
                    engine.prepare (sr);
                    engine.setParams (p);
                    engine.noteOn (69, 0.8f);
                    const auto out = render (engine, 24000);

                    expectWithinAbsoluteError (cents (frequencyAt (out, 0.1, 0.3), a4), 1200.0 * octave, 1.0);
                    expect (rmsAt (out, 0.1, 0.3) > 0.05);

                    float side = 0.0f;
                    for (int i = 0; i < 24000; ++i)
                        side = std::max (side, std::abs (out.getSample (0, i) - out.getSample (1, i)));
                    expect (side < 1.0e-6f);
                }
        }

        beginTest ("Noise layer: sustained, unpitched, follows the amp envelope and the colour control");
        {
            const auto run = [] (float colour)
            {
                auto p = sinePatch();
                p.oscA.level = 0.0f;
                p.noise.level = 1.0f;
                p.noise.color = colour;

                SynthEngine engine;
                engine.prepare (sr);
                engine.setParams (p);
                engine.noteOn (69, 0.8f);
                auto held = render (engine, 24000);
                engine.noteOff (69);
                render (engine, 24000);
                return std::pair { std::move (held), engine.getActiveVoiceCount() };
            };

            const auto white = run (0.5f);
            expect (rmsAt (white.first, 0.1, 0.3) > 0.02);
            expect (polybrisk::test::allFinite (white.first));
            expectEquals (white.second, 0);

            // Dark はなめらか (サンプル間の差が小さい)、Bright は荒い
            const auto roughness = [] (const juce::AudioBuffer<float>& b)
            {
                double diff = 0.0, level = 0.0;
                for (int i = 4801; i < 24000; ++i)
                {
                    diff += std::abs (b.getSample (0, i) - b.getSample (0, i - 1));
                    level += std::abs (b.getSample (0, i));
                }
                return diff / level;
            };

            expect (roughness (run (0.05f).first) < roughness (white.first) * 0.5);
            expect (roughness (run (0.95f).first) > roughness (white.first));
        }

        beginTest ("Sub / Noise at level 0 add nothing; changing the level does not click");
        {
            auto p = sinePatch();
            SynthEngine engine;
            engine.prepare (sr);
            engine.setParams (p);
            engine.noteOn (69, 0.8f);
            render (engine, 9600);

            p.sub.level = 1.0f;
            engine.setParams (p);
            const auto out = render (engine, 9600);
            expect (polybrisk::test::maxAbsDelta (out.getReadPointer (0), 9600) < 0.03f);
        }

        beginTest ("Aftertouch: adds vibrato and moves the cutoff; no effect at amount 0");
        {
            auto p = sinePatch();
            p.performance.aftertouchVibratoCents = 100.0f;

            SynthEngine engine;
            engine.prepare (sr);
            engine.setParams (p);
            engine.noteOn (69, 0.8f);
            const auto still = render (engine, 24000);
            expectWithinAbsoluteError (cents (frequencyAt (still, 0.1, 0.3), a4), 0.0, 0.5);

            engine.setAftertouch (1.0f);
            const auto pressed = render (engine, 48000);

            // 周期ごとの周波数の振れ幅が ±100 cent 付近になる
            double low = 1.0e9, high = 0.0;
            for (double t = 0.2; t < 0.95; t += 0.004)
            {
                const double f = frequencyAt (pressed, t, 0.012);
                low = std::min (low, f);
                high = std::max (high, f);
            }
            expect (cents (high, a4) > 70.0 && cents (high, a4) < 115.0, "high " + juce::String (cents (high, a4)));
            expect (cents (low, a4) < -70.0 && cents (low, a4) > -115.0, "low " + juce::String (cents (low, a4)));

            // Cutoff
            auto f = sinePatch();
            f.oscA.waveform = Waveform::saw;
            f.filter.mode = FilterMode::lp24;
            f.filter.cutoffHz = 50.0f;     // 基音 (110 Hz) より下: 閉じているあいだはほとんど通らない
            f.performance.aftertouchCutoff = 1.0f;

            SynthEngine filtered;
            filtered.prepare (sr);
            filtered.setParams (f);
            filtered.noteOn (45, 0.8f);
            const auto closed = render (filtered, 24000);
            filtered.setAftertouch (1.0f);
            const auto open = render (filtered, 24000);
            expect (rmsAt (open, 0.2, 0.2) > rmsAt (closed, 0.2, 0.2) * 1.5);

            // 量が 0 なら、押し込んでも変わらない
            f.performance.aftertouchCutoff = 0.0f;
            SynthEngine a, b;
            a.prepare (sr);
            b.prepare (sr);
            a.setParams (f);
            b.setParams (f);
            a.noteOn (45, 0.8f);
            b.noteOn (45, 0.8f);
            b.setAftertouch (1.0f);
            const auto x = render (a, 12000);
            const auto y = render (b, 12000);
            bool identical = true;
            for (int i = 0; i < 12000; ++i)
                identical = identical && juce::exactlyEqual (x.getSample (0, i), y.getSample (0, i));
            expect (identical);
        }

        beginTest ("Channel pressure and poly aftertouch reach the engine through the processor");
        {
            for (const bool poly : { false, true })
            {
                PolyBriskAudioProcessor processor;
                const auto set = [&processor] (const char* parameterId, float value)
                {
                    auto* parameter = processor.getValueTreeState().getParameter (parameterId);
                    parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
                };

                set (ids::oscA.wave, 0.0f);
                set (ids::filterCutoff, 50.0f);
                set (ids::aftertouchCutoff, 100.0f);

                const auto pressure = poly ? juce::MidiMessage::aftertouchChange (1, 45, 127) : juce::MidiMessage::channelPressureChange (1, 127);
                const std::vector<TimedMidi> events { { 0, juce::MidiMessage::noteOn (1, 45, 0.9f) }, { 24000, pressure } };
                const auto out = polybrisk::test::renderProcessor (processor, sr, { 256 }, 48000, events);
                expect (rmsAt (out, 0.75, 0.2) > rmsAt (out, 0.25, 0.2) * 1.5);
            }
        }

        beginTest ("Phaser / Flanger: bounded and finite at extreme settings; both types change the sound");
        {
            std::mt19937 rng (99);
            std::uniform_real_distribution<float> dist (-0.9f, 0.9f);

            for (const auto type : { PhaserType::phaser, PhaserType::flanger })
                for (const double rate : { 44100.0, 96000.0 })
                {
                    Phaser phaser;
                    phaser.prepare (rate);

                    PhaserParams p;
                    p.enabled = true;
                    p.type = type;
                    p.rateHz = 8.0f;
                    p.depth = 1.0f;
                    p.feedback = 0.9f;
                    p.mix = 1.0f;

                    float peak = 0.0f, difference = 0.0f;
                    bool finite = true;

                    for (int block = 0; block < 400; ++block)
                    {
                        float l[128], r[128], dryL[128];
                        for (int i = 0; i < 128; ++i)
                        {
                            l[i] = dist (rng);
                            r[i] = dist (rng);
                            dryL[i] = l[i];
                        }

                        phaser.process (l, r, 128, p);

                        for (int i = 0; i < 128; ++i)
                        {
                            finite = finite && std::isfinite (l[i]) && std::isfinite (r[i]);
                            peak = std::max ({ peak, std::abs (l[i]), std::abs (r[i]) });
                            difference = std::max (difference, std::abs (l[i] - dryL[i]));
                        }
                    }

                    expect (finite);
                    expect (peak < 12.0f, "peak " + juce::String (peak));    // Feedback 0.9 の理論上限は入力の 10 倍
                    expect (difference > 0.1f);
                }
        }

        beginTest ("Phaser / Flanger: switching the type while running stays finite and bounded");
        {
            // pluginval のパラメーター乱数テストで見つかった不具合の再現 (切り替えで内部の値の単位を取り違えていた)
            std::mt19937 rng (7);
            std::uniform_real_distribution<float> dist (-0.9f, 0.9f);
            std::uniform_real_distribution<float> unit (0.0f, 1.0f);

            for (const double rate : { 11025.0, 44100.0, 96000.0, 192000.0 })
            {
                Phaser phaser;
                phaser.prepare (rate);
                bool finite = true;
                float peak = 0.0f;

                for (int block = 0; block < 3000; ++block)
                {
                    PhaserParams p;
                    p.enabled = true;
                    p.type = unit (rng) < 0.5f ? PhaserType::phaser : PhaserType::flanger;
                    p.rateHz = 0.02f + 7.98f * unit (rng);
                    p.depth = unit (rng);
                    p.feedback = 0.9f * unit (rng);
                    p.mix = unit (rng);

                    float l[32], r[32];
                    for (int i = 0; i < 32; ++i)
                    {
                        l[i] = dist (rng);
                        r[i] = dist (rng);
                    }

                    phaser.process (l, r, 32, p);

                    for (int i = 0; i < 32; ++i)
                    {
                        finite = finite && std::isfinite (l[i]) && std::isfinite (r[i]);
                        peak = std::max ({ peak, std::abs (l[i]), std::abs (r[i]) });
                    }
                }

                expect (finite, "rate " + juce::String (rate));
                expect (peak < 12.0f, "peak " + juce::String (peak));
            }
        }

        beginTest ("Engine: random settings of every new feature never produce NaN / Inf");
        {
            std::mt19937 rng (2024);
            std::uniform_real_distribution<float> unit (0.0f, 1.0f);

            for (const double rate : { 11025.0, 44100.0, 96000.0 })
            {
                SynthEngine engine;
                engine.prepare (rate);
                bool finite = true;

                for (int block = 0; block < 2500; ++block)
                {
                    EngineParams p;
                    p.oscA.waveform = static_cast<Waveform> (static_cast<int> (unit (rng) * 6.99f));
                    p.oscA.pulseWidth = 0.05f + 0.9f * unit (rng);
                    p.oscA.unisonVoices = 1 + static_cast<int> (unit (rng) * 8.99f);
                    p.filter.cutoffHz = 20.0f + 19980.0f * unit (rng);
                    p.filter.resonance = unit (rng);
                    p.filter.character = static_cast<FilterCharacter> (static_cast<int> (unit (rng) * 2.99f));
                    p.voiceMode = static_cast<VoiceMode> (static_cast<int> (unit (rng) * 2.99f));
                    p.glideSeconds = 2.0f * unit (rng);
                    p.sub = { unit (rng), unit (rng) < 0.5f ? -1 : -2, unit (rng) < 0.5f ? SubWave::sine : SubWave::square };
                    p.noise = { unit (rng), unit (rng) };
                    p.lfo.wave = static_cast<LfoWave> (static_cast<int> (unit (rng) * 4.99f));
                    p.lfo.sync = unit (rng) < 0.5f;
                    p.lfo.rateHz = 0.05f + 19.95f * unit (rng);
                    p.lfo.divisionBeats = polybrisk::params::lfoDivisionBeats (static_cast<int> (unit (rng) * 11.99f));
                    p.lfo.fadeSeconds = 5.0f * unit (rng);
                    p.lfo.retrigger = unit (rng) < 0.5f;
                    p.lfo.toCutoff = 2.0f * unit (rng) - 1.0f;
                    p.lfo.toPulseWidth = unit (rng);
                    p.lfo.toPitchCents = 1200.0f * unit (rng);
                    p.lfo.toAmp = unit (rng);
                    p.performance.aftertouchVibratoCents = 200.0f * unit (rng);
                    p.performance.aftertouchCutoff = 2.0f * unit (rng) - 1.0f;
                    p.fx.phaser = { unit (rng) < 0.7f, unit (rng) < 0.5f ? PhaserType::phaser : PhaserType::flanger,
                                    0.02f + 7.98f * unit (rng), unit (rng), 0.9f * unit (rng), unit (rng) };
                    engine.setParams (p);
                    engine.setAftertouch (unit (rng));
                    engine.setTransport ({ 60.0 + 180.0 * unit (rng), unit (rng) < 0.7f, true, 400.0 * unit (rng) - 8.0 });

                    const int note = 24 + static_cast<int> (unit (rng) * 84.0f);
                    if (unit (rng) < 0.5f)
                        engine.noteOn (note, 0.1f + 0.9f * unit (rng));
                    else
                        engine.noteOff (note);
                    if (block % 97 == 0)
                        engine.allNotesOff();

                    float l[64], r[64];
                    engine.render (l, r, 64);

                    for (int i = 0; i < 64; ++i)
                        finite = finite && std::isfinite (l[i]) && std::isfinite (r[i]) && std::abs (l[i]) <= 1.0f && std::abs (r[i]) <= 1.0f;
                }

                expect (finite, "rate " + juce::String (rate));
            }
        }

        beginTest ("Phaser in the chain: off is an exact bypass, on makes moving notches, switching does not click");
        {
            const auto run = [] (bool enabled, bool switchOn)
            {
                auto p = sinePatch();
                p.oscA.waveform = Waveform::saw;
                p.fx.phaser.enabled = enabled;
                p.fx.phaser.mix = 1.0f;

                SynthEngine engine;
                engine.prepare (sr);
                engine.setParams (p);
                engine.noteOn (45, 0.8f);
                auto out = render (engine, 24000);

                if (switchOn)
                {
                    p.fx.phaser.enabled = true;
                    engine.setParams (p);
                    out = render (engine, 24000);
                }

                return out;
            };

            const auto dry = run (false, false);
            const auto wet = run (true, false);

            float diff = 0.0f;
            for (int i = 0; i < 24000; ++i)
                diff = std::max (diff, std::abs (dry.getSample (0, i) - wet.getSample (0, i)));
            expect (diff > 0.01f);
            expect (polybrisk::test::allFinite (wet));

            // ON にした瞬間: のこぎり波そのものの段差 (約 0.29) を超える跳びが出ない
            const auto switched = run (false, true);
            expect (polybrisk::test::maxAbsDelta (switched.getReadPointer (0), 24000) < polybrisk::test::maxAbsDelta (dry.getReadPointer (0), 24000) * 1.3f);
        }

        beginTest ("Gate: 1/64 rate, and the random pattern generator");
        {
            expectWithinAbsoluteError (polybrisk::params::gateRateBeats (5), 0.0625, 1.0e-12);
            expectWithinAbsoluteError (polybrisk::params::gateRateBeats (1), 0.25, 1.0e-12);     // 既存の並びは変わらない
            expectWithinAbsoluteError (polybrisk::params::gateRateBeats (4), 1.0 / 6.0, 1.0e-12);

            int distinct = 0;
            GatePattern previous {};

            for (std::uint32_t seed = 1; seed <= 200; ++seed)
            {
                const auto mono = makeRandomGatePattern (seed, false);
                const auto again = makeRandomGatePattern (seed, false);
                expect (mono == again);
                expect (! mono.isStereo());
                expectEquals (mono.values[0], 1.0f);

                int open = 0;
                for (size_t i = 0; i < 16; ++i)
                {
                    expect (mono.values[i] >= 0.0f && mono.values[i] <= 1.0f);
                    expectEquals (mono.values[i + 16], mono.values[i]);
                    open += mono.values[i] > 0.0f ? 1 : 0;
                }
                expect (open >= 5 && open <= 16);

                distinct += mono == previous ? 0 : 1;
                previous = mono;

                const auto stereo = makeRandomGatePattern (seed, true);
                expect (stereo.values == mono.values);       // 左は同じ seed で同じ
                for (const float v : stereo.right)
                    expect (v >= 0.0f && v <= 1.0f);
            }

            expect (distinct > 190);
            expect (makeRandomGatePattern (0, false) == makeRandomGatePattern (0, false));
        }
    }
};

ModulationTests modulationTests;
} // namespace
