#include "PluginProcessor.h"
#include "TestHelpers.h"
#include "rhythm/TranceGate.h"
#include "state/Parameters.h"
#include "state/StateSerializer.h"
#include "synth/SynthEngine.h"

#include <cmath>
#include <vector>

namespace
{
using namespace polybrisk;
namespace ids = polybrisk::params::id;

GatePattern patternOf (std::initializer_list<float> values)
{
    GatePattern p;
    p.values.fill (1.0f);
    size_t i = 0;
    for (const float v : values)
        p.values[i++] = v;
    p.right = p.values;
    return p;
}

/** TranceGate 単体を、テンポ一定のクロックで駆動する。左のゲインを返し、rightOut があれば右も書く。 */
std::vector<float> runGate (const GateParams& params, double sampleRate, double bpm, int numSamples, int block = 128, double startBeats = 0.0,
                            std::vector<float>* rightOut = nullptr)
{
    TranceGate gate;
    gate.prepare (sampleRate);
    RhythmClock clock;
    clock.prepare (sampleRate);
    clock.setTransport ({ bpm, true, true, startBeats });

    std::vector<float> gains (static_cast<size_t> (numSamples), 1.0f);
    std::vector<float> rightGains (static_cast<size_t> (numSamples), 1.0f);

    for (int pos = 0; pos < numSamples; pos += block)
    {
        const int n = std::min (block, numSamples - pos);
        double beats[2048];
        clock.peek (beats, n);
        if (! gate.process (gains.data() + pos, rightGains.data() + pos, beats, n, params))
        {
            std::fill (gains.begin() + pos, gains.begin() + pos + n, 1.0f);
            std::fill (rightGains.begin() + pos, rightGains.begin() + pos + n, 1.0f);
        }
        clock.advance (n);
    }

    if (rightOut != nullptr)
        *rightOut = rightGains;

    return gains;
}

/** エンジンを「再生中のホスト」として駆動する (ブロックごとに PPQ を渡す)。 */
juce::AudioBuffer<float> renderWithHost (SynthEngine& engine, double sampleRate, double bpm, int numSamples, int block, double startBeats = 0.0)
{
    juce::AudioBuffer<float> out (2, numSamples);

    for (int pos = 0; pos < numSamples; pos += block)
    {
        const int n = std::min (block, numSamples - pos);
        engine.setTransport ({ bpm, true, true, startBeats + pos * bpm / (60.0 * sampleRate) });
        engine.render (out.getWritePointer (0, pos), out.getWritePointer (1, pos), n);
    }

    return out;
}

EngineParams heldSine()
{
    EngineParams p;
    p.oscA.waveform = Waveform::sine;
    p.amp = { 0.001f, 0.05f, 1.0f, 0.02f };
    return p;
}

void setParam (PolyBriskAudioProcessor& processor, const char* parameterId, float realValue)
{
    auto* param = processor.getValueTreeState().getParameter (parameterId);
    param->setValueNotifyingHost (param->convertTo0to1 (realValue));
}

class TranceGateTests final : public juce::UnitTest
{
public:
    TranceGateTests() : juce::UnitTest ("Trance Gate", "rhythm") {}

    void runTest() override
    {
        const double sr = 48000.0;

        beginTest ("targetGain = 1 - depth * (1 - step)");
        expectEquals (TranceGate::targetGain (1.0f, 1.0f), 1.0f);
        expectEquals (TranceGate::targetGain (0.0f, 1.0f), 0.0f);
        expectEquals (TranceGate::targetGain (0.0f, 0.0f), 1.0f);
        expectEquals (TranceGate::targetGain (0.5f, 1.0f), 0.5f);
        expectEquals (TranceGate::targetGain (0.0f, 0.25f), 0.75f);

        beginTest ("All steps on: gain stays at exactly 1. All steps off: gain reaches zero. Depth 0: no gating");
        {
            GateParams p;
            p.enabled = true;
            p.pattern.values.fill (1.0f);
            for (const float g : runGate (p, sr, 120.0, 48000))
                expectEquals (g, 1.0f);

            p.pattern.values.fill (0.0f);
            const auto closed = runGate (p, sr, 120.0, 48000);
            for (size_t i = 0; i < closed.size(); ++i)
                expect (closed[i] < 1.0e-4f);

            p.depth = 0.0f;
            for (const float g : runGate (p, sr, 120.0, 48000))
                expectEquals (g, 1.0f);
        }

        beginTest ("Switching the gate on for the first time fades in, even on a closed step (D-068)");
        {
            // OFF のあいだはエンジンが process を呼ばず、代わりに idle() を呼ぶ。そのあと閉じたステップで ON にしても、
            // 1 倍から 10 ms かけて下がる (以前は「リセット直後」の扱いのままで、1 サンプルで 0 に落ちてクリックした)
            TranceGate gate;
            gate.prepare (48000.0);
            gate.idle();

            GateParams p;
            p.enabled = true;
            p.pattern = patternOf ({ 0.0f, 0.0f, 0.0f, 0.0f });
            RhythmClock clock;
            clock.prepare (48000.0);
            clock.setTransport ({ 120.0, true, true, 0.0 });

            float left[128], right[128];
            double beats[128];
            clock.peek (beats, 128);
            expect (gate.process (left, right, beats, 128, p));
            expectGreaterThan (left[0], 0.95f);

            float largestStep = std::abs (1.0f - left[0]);
            for (int i = 1; i < 128; ++i)
                largestStep = std::max (largestStep, std::abs (left[i] - left[i - 1]));
            expectLessThan (largestStep, 0.02f);

            // prepare の直後から ON の場合は、これまでどおりパターンの状態から始まる (頭が閉じていれば最初から無音)
            TranceGate fromStart;
            fromStart.prepare (48000.0);
            expect (fromStart.process (left, right, beats, 128, p));
            expectLessThan (left[0], 0.05f);
        }

        beginTest ("8 / 16 / 32 steps wrap around correctly; 8 steps of 1/16 at 120 BPM take one second");
        for (const int steps : { 8, 16, 32 })
        {
            GateParams p;
            p.enabled = true;
            p.steps = steps;
            p.smoothSeconds = 0.0005f;

            // ステップごとに違う値を入れておき、ステップの中央でその値になっていることを確かめる
            for (int i = 0; i < maxGateSteps; ++i)
                p.pattern.values[static_cast<size_t> (i)] = 0.1f + 0.025f * static_cast<float> (i);

            const int stepSamples = 6000;   // 125 ms
            const auto gains = runGate (p, sr, 120.0, stepSamples * (steps * 2 + 3));

            for (int s = 0; s < steps * 2 + 2; ++s)
                expectWithinAbsoluteError (gains[static_cast<size_t> (s * stepSamples + stepSamples / 2)],
                                           p.pattern.values[static_cast<size_t> (s % steps)], 1.0e-4f,
                                           "steps " + juce::String (steps) + " step " + juce::String (s));

            if (steps == 8)
                for (int i = 6000; i < 48000; i += 997)   // 8 ステップ = 48000 サンプル = 1 秒で一周する
                    expectWithinAbsoluteError (gains[static_cast<size_t> (i)], gains[static_cast<size_t> (i + 48000)], 1.0e-6f);
        }

        beginTest ("Step boundaries are sample accurate at every rate, sample rate and block size");
        for (const double rate : { 44100.0, 48000.0, 96000.0 })
        {
            for (const int rateIndex : { 0, 1, 2, 3, 4 })
            {
                GateParams p;
                p.enabled = true;
                p.rateBeats = polybrisk::params::gateRateBeats (rateIndex);
                p.smoothSeconds = 0.0005f;
                p.pattern = patternOf ({ 1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f });

                const double stepSamples = p.rateBeats * 60.0 / 120.0 * rate;
                std::vector<std::vector<float>> runs;

                for (const int block : { 32, 128, 512, 2048 })
                    runs.push_back (runGate (p, rate, 120.0, static_cast<int> (stepSamples * 9.5), block));

                // ステップ 0 → 1 の境界: 境界の直前までは 1、直後から下がり始める (誤差 1 サンプル程度)
                for (const auto& gains : runs)
                {
                    for (int step = 1; step <= 7; step += 2)
                    {
                        const int boundary = static_cast<int> (std::ceil (step * stepSamples));
                        expect (gains[static_cast<size_t> (boundary - 2)] > 0.999f);
                        expect (gains[static_cast<size_t> (boundary + 1)] < 0.999f);
                    }
                }

                // ブロック長を変えても同じ結果
                for (size_t r = 1; r < runs.size(); ++r)
                {
                    float worst = 0.0f;
                    for (size_t i = 0; i < runs[0].size(); ++i)
                        worst = std::max (worst, std::abs (runs[r][i] - runs[0][i]));
                    expect (worst < 1.0e-5f);
                }
            }
        }

        beginTest ("Swing: even steps start late, a pair still lasts two steps; Smooth sets the transition time");
        {
            GateParams p;
            p.enabled = true;
            p.swing = 0.5f;
            p.smoothSeconds = 0.0005f;
            p.pattern = patternOf ({ 1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f });
            const auto gains = runGate (p, sr, 120.0, 48000);

            // 1 つ目 (開) は 1.5 ステップ = 9000 サンプル、2 つ目 (閉) は 0.5 ステップ = 3000 サンプル
            expect (gains[8990] > 0.999f);
            expect (gains[9100] < 0.01f);
            expect (gains[11990] < 0.01f);
            expect (gains[12100] > 0.99f);      // 2 ステップ合計 12000 サンプルで次の組が始まる
            expect (gains[20990] > 0.999f);

            // Smooth: 指定時間でおおむね (95%) 遷移し終える。長くすれば遷移もゆっくりになる
            p.swing = 0.0f;
            for (const float smoothMs : { 1.0f, 10.0f, 30.0f })
            {
                p.smoothSeconds = smoothMs * 0.001f;
                const auto g = runGate (p, sr, 120.0, 12000);
                const auto after = [&] (double ms) { return g[static_cast<size_t> (6000 + ms * 48.0)]; };
                expectWithinAbsoluteError (after (smoothMs), 0.05f, 0.02f);
                expect (after (smoothMs * 0.2) > 0.4f);

                // 急峻な切り替えにならない (1 サンプルあたりの変化が小さい)
                float step = 0.0f;
                for (size_t i = 1; i < g.size(); ++i)
                    step = std::max (step, std::abs (g[i] - g[i - 1]));
                expect (step < 3.2f / (smoothMs * 48.0f));
            }
        }

        beginTest ("Changing rate, swing, steps, depth and pattern while running never jumps or produces NaN");
        {
            TranceGate gate;
            gate.prepare (sr);
            RhythmClock clock;
            clock.prepare (sr);
            GateParams p;
            p.enabled = true;
            float previous = 1.0f;
            float worstStep = 0.0f;
            bool finite = true;

            for (int block = 0; block < 3000; ++block)
            {
                p.rateBeats = polybrisk::params::gateRateBeats (block / 7 % 6);
                p.swing = static_cast<float> (block % 11) * 0.05f;
                p.steps = polybrisk::params::gateStepCount (block / 13 % 3);
                p.depth = block % 5 == 0 ? 0.3f : 1.0f;
                p.enabled = block / 50 % 3 != 2;
                p.pattern.values[static_cast<size_t> (block % 32)] = static_cast<float> (block % 3) * 0.5f;

                clock.setTransport ({ 90.0 + (block % 4) * 30.0, true, true, block % 400 == 0 ? 3.0 : clock.getPosition() });
                double beats[64];
                float gains[64], rightGains[64];
                p.stereo = block / 17 % 2 == 1;
                p.width = static_cast<float> (block % 6) * 0.2f;
                clock.peek (beats, 64);
                if (! gate.process (gains, rightGains, beats, 64, p))
                    std::fill (gains, gains + 64, 1.0f);
                clock.advance (64);

                if (block == 0)
                    previous = gains[0];    // 最初から ON のときは、パターンの状態から始まる

                for (const float g : gains)
                {
                    finite = finite && std::isfinite (g) && g >= 0.0f && g <= 1.0f;
                    worstStep = std::max (worstStep, std::abs (g - previous));
                    previous = g;
                }
            }

            expect (finite);
            expect (worstStep < 0.03f, "largest step " + juce::String (worstStep));   // Smooth 3 ms のときの上限は約 0.021
        }

        beginTest ("Engine: step value 0.5 halves the level, with the same gain on left and right");
        {
            const auto render = [&] (bool gateOn)
            {
                SynthEngine engine;
                engine.prepare (sr);
                auto p = heldSine();
                p.oscA.unisonVoices = 3;
                p.oscA.waveform = Waveform::saw;
                p.gate.enabled = gateOn;
                p.gate.pattern.values.fill (0.5f);
                engine.setParams (p);
                engine.noteOn (57, 1.0f);
                return renderWithHost (engine, sr, 120.0, 48000, 128);
            };

            const auto dry = render (false);
            const auto gated = render (true);

            for (int ch = 0; ch < 2; ++ch)
                for (int i = 12000; i < 48000; i += 53)
                    expectWithinAbsoluteError (gated.getSample (ch, i), 0.5f * dry.getSample (ch, i), 2.0e-5f);

            // ステレオの信号でも左右が同じゲイン (定位が動かない)
            expect (std::abs (dry.getSample (0, 20000) - dry.getSample (1, 20000)) > 1.0e-4f);
        }

        beginTest ("Engine: the gate sits before Delay / Reverb, so their tails survive closed steps");
        {
            const auto render = [&] (bool reverb)
            {
                SynthEngine engine;
                engine.prepare (sr);
                auto p = heldSine();
                p.gate.enabled = true;
                p.gate.pattern = patternOf ({ 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f });
                p.fx.reverb.enabled = reverb;
                p.fx.reverb.mix = 0.5f;
                p.fx.reverb.decaySeconds = 2.0f;
                engine.setParams (p);
                engine.noteOn (69, 1.0f);
                return renderWithHost (engine, sr, 120.0, 48000, 128);
            };

            // 閉じたステップの中 (300〜400 ms): リバーブなしでは無音、ありでは残響が聞こえる
            expect (polybrisk::test::peakLevel (render (false), 14400, 4800) < 1.0e-4f);
            expect (polybrisk::test::peakLevel (render (true), 14400, 4800) > 1.0e-3f);
        }

        beginTest ("Engine: FX Bypass does not bypass the gate; turning the gate off returns smoothly to unity");
        {
            SynthEngine engine;
            engine.prepare (sr);
            auto p = heldSine();
            p.gate.enabled = true;
            p.gate.pattern.values.fill (0.0f);
            p.fx.bypassAll = true;
            engine.setParams (p);
            engine.noteOn (69, 1.0f);
            const auto closed = renderWithHost (engine, sr, 120.0, 24000, 128);
            expect (polybrisk::test::peakLevel (closed, 12000, 12000) < 1.0e-4f);

            p.gate.enabled = false;
            engine.setParams (p);
            const auto open = renderWithHost (engine, sr, 120.0, 24000, 128, 1.0);
            const float full = Voice::voiceGain * 0.70710678f;
            expectWithinAbsoluteError (polybrisk::test::peakLevel (open, 12000, 12000), full, 0.002f);

            // 戻りは 10 ms 程度のフェードで、正弦波より急な段差を作らない
            const float slope = full * static_cast<float> (2.0 * juce::MathConstants<double>::pi * 440.0 / sr);
            expect (polybrisk::test::maxAbsDelta (open.getReadPointer (0), 24000) < slope * 1.5f);
        }

        beginTest ("Engine: Restart = Transport follows the timeline; Restart = Note On starts at the first note");
        {
            const auto firstOpen = [&] (bool restartOnNote, double startBeats, int noteAt)
            {
                SynthEngine engine;
                engine.prepare (sr);
                auto p = heldSine();
                p.gate.enabled = true;
                p.gate.restartOnNote = restartOnNote;
                p.gate.pattern = patternOf ({ 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f });
                engine.setParams (p);

                juce::AudioBuffer<float> out (2, 96000);
                for (int pos = 0; pos < 96000; pos += 64)
                {
                    if (pos == noteAt)
                        engine.noteOn (69, 1.0f);

                    engine.setTransport ({ 120.0, true, true, startBeats + pos * 120.0 / (60.0 * sr) });
                    engine.render (out.getWritePointer (0, pos), out.getWritePointer (1, pos), 64);
                }

                for (int i = 0; i < 96000; ++i)
                    if (std::abs (out.getSample (0, i)) > 0.02f)
                        return i;
                return -1;
            };

            // Transport: ステップ 3 は 0.75 拍 = 18000 サンプル目。ノートを押す位置には関係しない
            expectWithinAbsoluteError (static_cast<double> (firstOpen (false, 0.0, 0)), 18000.0, 150.0);
            expectWithinAbsoluteError (static_cast<double> (firstOpen (false, 0.0, 6400)), 18000.0, 150.0);
            // タイムラインが 0.5 拍進んだ位置から始まれば、その分だけ早く開く
            expectWithinAbsoluteError (static_cast<double> (firstOpen (false, 0.5, 0)), 6000.0, 150.0);
            // Note On: 最初のノートから 3 ステップ後
            expectWithinAbsoluteError (static_cast<double> (firstOpen (true, 0.0, 6400)), 6400.0 + 18000.0, 150.0);
            expectWithinAbsoluteError (static_cast<double> (firstOpen (true, 0.37, 6400)), 6400.0 + 18000.0, 150.0);
        }

        beginTest ("Engine: tempo changes and loop jumps keep the pattern locked to the host position");
        {
            SynthEngine engine;
            engine.prepare (sr);
            auto p = heldSine();
            p.gate.enabled = true;
            p.gate.smoothSeconds = 0.0005f;
            p.gate.pattern = patternOf ({ 1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f });
            engine.setParams (p);
            engine.noteOn (69, 1.0f);

            // テンポを 120 → 180 → 90 と変えながら、PPQ を積算して渡す。各ブロックの中央で、
            // 「その位置のステップが開いているか」と出力レベルが一致することを確かめる
            double ppq = 0.0;
            int mismatches = 0, checked = 0;
            bool finite = true;

            for (int block = 0; block < 1500; ++block)
            {
                const double bpm = block < 500 ? 120.0 : (block < 1000 ? 180.0 : 90.0);
                if (block == 700)
                    ppq = 2.0;     // ループで先頭付近へ戻る

                engine.setTransport ({ bpm, true, true, ppq });
                float l[64], r[64];
                engine.render (l, r, 64);

                const double middle = ppq + 32.0 * bpm / (60.0 * sr);
                const auto location = rhythm::locateStep (middle, 0.25, 0.0);
                const double inStep = (middle - location.start) / location.length;

                if (block > 20 && inStep > 0.2 && inStep < 0.8)
                {
                    float peak = 0.0f;
                    for (const float x : l)
                    {
                        finite = finite && std::isfinite (x);
                        peak = std::max (peak, std::abs (x));
                    }

                    const bool open = (location.index % 2 + 2) % 2 == 0;
                    ++checked;
                    if (open != (peak > 0.03f))
                        ++mismatches;
                }

                ppq += 64.0 * bpm / (60.0 * sr);
            }

            expect (finite);
            expect (checked > 500);
            expectEquals (mismatches, 0);
        }

        beginTest ("State: gate pattern, name and edited flag survive a roundtrip; old states load with the default pattern, gate off");
        {
            PolyBriskAudioProcessor source;
            auto& meta = source.getMetaState();
            expectEquals (meta.getGatePatternName(), juce::String ("Shifted Long"));
            expect (! meta.isGatePatternEdited());

            meta.setFactoryGatePattern (3);
            expectEquals (meta.getGatePatternName(), juce::String ("Syncopated"));
            expectEquals (meta.getGatePattern().values[1], 0.0f);
            meta.setGateStep (1, 0.35f);
            meta.setGateStep (31, 0.8f);
            expect (meta.isGatePatternEdited());
            setParam (source, ids::gateEnabled, 1.0f);
            setParam (source, ids::gateSteps, 2.0f);
            setParam (source, ids::gateRate, 3.0f);
            setParam (source, ids::gateSwing, 20.0f);
            setParam (source, ids::arpEnabled, 1.0f);
            setParam (source, ids::arpMode, 4.0f);
            setParam (source, ids::arpOctaves, 3.0f);
            setParam (source, ids::arpLatch, 1.0f);

            juce::MemoryBlock block;
            source.getStateInformation (block);

            PolyBriskAudioProcessor restored;
            restored.setStateInformation (block.getData(), static_cast<int> (block.getSize()));
            expect (restored.getMetaState().getGatePattern() == meta.getGatePattern());
            expectWithinAbsoluteError (restored.getMetaState().getGatePattern().values[1], 0.35f, 1.0e-4f);
            expectEquals (restored.getMetaState().getGatePatternName(), juce::String ("Syncopated"));
            expect (restored.getMetaState().isGatePatternEdited());

            for (const auto* parameterId : { ids::gateEnabled, ids::gateSteps, ids::gateRate, ids::gateSwing, ids::arpEnabled, ids::arpMode, ids::arpOctaves, ids::arpLatch })
                expectEquals (restored.getValueTreeState().getRawParameterValue (parameterId)->load(),
                              source.getValueTreeState().getRawParameterValue (parameterId)->load(), parameterId);

            // Gate / Arp の項目がない (Phase 4 以前の) 状態: 両方 OFF、パターンは既定
            auto tree = polybrisk::state::toTree (source.getValueTreeState(), meta);
            auto parameters = tree.getChildWithName (polybrisk::state::parametersType);
            for (int i = parameters.getNumChildren(); --i >= 0;)
            {
                const auto id = parameters.getChild (i).getProperty ("id").toString();
                if (id.startsWith ("gate.") || id.startsWith ("arp."))
                    parameters.removeChild (i, nullptr);
            }
            auto metaTree = tree.getChildWithName (polybrisk::state::metaType);
            metaTree.removeChild (metaTree.getChildWithName (polybrisk::MetaState::rhythmType), nullptr);

            expect (polybrisk::state::fromTree (tree, restored.getValueTreeState(), restored.getMetaState()));
            expectEquals (restored.getValueTreeState().getRawParameterValue (ids::gateEnabled)->load(), 0.0f);
            expectEquals (restored.getValueTreeState().getRawParameterValue (ids::arpEnabled)->load(), 0.0f);
            expectEquals (restored.getMetaState().getGatePatternName(), juce::String ("Shifted Long"));
            expect (! restored.getMetaState().isGatePatternEdited());

            // 壊れたパターン: 不足は開 (1.0) で補い、範囲外はクランプ、数値でないものは開
            juce::ValueTree broken { polybrisk::MetaState::rhythmType };
            broken.setProperty ("gatePattern", "0,2,-1,abc,0.25", nullptr);
            restored.getMetaState().rhythmFromTree (broken);
            const auto pattern = restored.getMetaState().getGatePattern();
            expectEquals (pattern.values[0], 0.0f);
            expectEquals (pattern.values[1], 1.0f);
            expectEquals (pattern.values[2], 0.0f);
            expectEquals (pattern.values[3], 1.0f);
            expectEquals (pattern.values[4], 0.25f);
            expectEquals (pattern.values[5], 1.0f);
            expectEquals (pattern.values[31], 1.0f);
        }

        beginTest ("Factory patterns match the addon spec");
        {
            // 先頭の 6 つは追加仕様 §3.3 のまま (名前と順序を変えない)
            expect (static_cast<int> (factoryGatePatterns.size()) >= numSpecGatePatterns);
            const char* specNames[] = { "Straight 8ths", "Pulse 16", "Offbeat", "Syncopated", "Build Up", "Full" };
            const char* specSteps[] = { "1010101010101010", "1110111011101110", "0101010101010101",
                                        "1011010010110100", "1000100010101111", "1111111111111111" };
            for (int i = 0; i < numSpecGatePatterns; ++i)
            {
                expectEquals (juce::String (factoryGatePatterns[static_cast<size_t> (i)].name), juce::String (specNames[i]));
                expectEquals (juce::String (factoryGatePatterns[static_cast<size_t> (i)].steps16), juce::String (specSteps[i]));
            }

            const auto straight = makeGatePattern (factoryGatePatterns[0]);
            for (int i = 0; i < 32; ++i)
                expectEquals (straight.values[static_cast<size_t> (i)], i % 2 == 0 ? 1.0f : 0.0f);

            const auto full = makeGatePattern (factoryGatePatterns[5]);
            for (const float v : full.values)
                expectEquals (v, 1.0f);
        }

        beginTest ("Every factory pattern is well formed and distinct");
        {
            juce::StringArray names, shapes;

            for (const auto& factory : factoryGatePatterns)
            {
                const juce::String steps (factory.steps16);
                expectEquals (steps.length(), 16, factory.name);
                expect (steps.containsOnly ("01xhml"), factory.name);

                if (factory.right16 != nullptr)
                {
                    const juce::String rightSteps (factory.right16);
                    expectEquals (rightSteps.length(), 16, factory.name);
                    expect (rightSteps.containsOnly ("01xhml"), factory.name);
                    expect (rightSteps != steps, factory.name);
                }
                expect (juce::String (factory.group).isNotEmpty(), factory.name);

                // 名前と形はどれも重複しない。音が出ないパターンは入れない
                expect (! names.contains (factory.name), factory.name);
                names.add (factory.name);

                const auto pattern = makeGatePattern (factory);
                juce::String shape;
                float sum = 0.0f;
                for (int i = 0; i < 16; ++i)
                {
                    const float v = pattern.values[static_cast<size_t> (i)];
                    expect (v >= 0.0f && v <= 1.0f);
                    expectEquals (pattern.values[static_cast<size_t> (i + 16)], v);
                    shape << juce::String (juce::roundToInt (v * 4.0f)) << juce::String (juce::roundToInt (pattern.right[static_cast<size_t> (i)] * 4.0f));
                    sum += v;
                }

                expect (pattern.isStereo() == (factory.right16 != nullptr), factory.name);
                expect (factory.rateChoice >= 0 && factory.rateChoice < 6, factory.name);   // gate.rate の選択肢は 6 つ
                if (juce::String (factory.group) == "Fast 1/32")
                    expect (polybrisk::params::gateRateBeats (factory.rateChoice) <= 0.125, factory.name);   // 1/32 か 1/64
                else
                    expectEquals (factory.rateChoice, 1, factory.name);   // それ以外は 1/16 (D-044)

                expect (sum > 0.0f, factory.name);
                expect (! shapes.contains (shape), factory.name);
                shapes.add (shape);
            }

            expectEquals (gateStepLevel ('1'), 1.0f);
            expectEquals (gateStepLevel ('x'), 1.0f);
            expectEquals (gateStepLevel ('h'), 0.75f);
            expectEquals (gateStepLevel ('m'), 0.5f);
            expectEquals (gateStepLevel ('l'), 0.25f);
            expectEquals (gateStepLevel ('0'), 0.0f);
        }

        beginTest ("Stereo: each channel follows its own lane; mono ignores the right lane");
        {
            // 120 BPM、1/16 = 6000 サンプル。左は偶数ステップ、右は奇数ステップ
            GateParams p;
            p.enabled = true;
            p.steps = 16;
            p.rateBeats = 0.25;
            p.depth = 1.0f;
            p.smoothSeconds = 0.0005f;
            for (size_t i = 0; i < 32; ++i)
            {
                p.pattern.values[i] = i % 2 == 0 ? 1.0f : 0.0f;
                p.pattern.right[i] = i % 2 == 0 ? 0.0f : 1.0f;
            }

            const int stepSamples = 6000;
            std::vector<float> right;

            p.stereo = true;
            p.width = 1.0f;
            auto left = runGate (p, 48000.0, 120.0, stepSamples * 8, 128, 0.0, &right);
            for (int step = 0; step < 8; ++step)
            {
                const auto middle = static_cast<size_t> (step * stepSamples + stepSamples / 2);
                expectWithinAbsoluteError (left[middle], step % 2 == 0 ? 1.0f : 0.0f, 1.0e-3f);
                expectWithinAbsoluteError (right[middle], step % 2 == 0 ? 0.0f : 1.0f, 1.0e-3f);
            }

            // Width 0: 左右とも「どちらかが開いていれば開く」になり、左右が一致する
            p.width = 0.0f;
            left = runGate (p, 48000.0, 120.0, stepSamples * 8, 128, 0.0, &right);
            for (size_t i = 0; i < left.size(); ++i)
                if (std::abs (left[i] - right[i]) > 1.0e-6f || std::abs (left[i] - 1.0f) > 1.0e-3f)
                {
                    expect (false, "width 0 must give identical, open channels for a ping-pong pattern");
                    break;
                }

            // Width 50 %: 閉じている側は半分まで開く
            p.width = 0.5f;
            left = runGate (p, 48000.0, 120.0, stepSamples * 8, 128, 0.0, &right);
            expectWithinAbsoluteError (left[static_cast<size_t> (stepSamples + stepSamples / 2)], 0.5f, 1.0e-3f);
            expectWithinAbsoluteError (right[static_cast<size_t> (stepSamples / 2)], 0.5f, 1.0e-3f);
            expectWithinAbsoluteError (left[static_cast<size_t> (stepSamples / 2)], 1.0f, 1.0e-3f);

            // Stereo OFF: 右のパターンは使わず、左右とも左のパターン
            p.stereo = false;
            p.width = 1.0f;
            left = runGate (p, 48000.0, 120.0, stepSamples * 8, 128, 0.0, &right);
            bool same = true;
            for (size_t i = 0; i < left.size(); ++i)
                same = same && std::abs (left[i] - right[i]) < 1.0e-9f;
            expect (same);
            expectWithinAbsoluteError (right[static_cast<size_t> (stepSamples + stepSamples / 2)], 0.0f, 1.0e-3f);
        }

        beginTest ("Defaults: gate.rate is 1/32 and the default pattern is Shifted Long, a 1/32 pattern (D-044)");
        {
            PolyBriskAudioProcessor processor;
            expectEquals (processor.getValueTreeState().getRawParameterValue (polybrisk::params::id::gateRate)->load(), 2.0f);

            const auto& factory = factoryGatePatterns[static_cast<size_t> (defaultFactoryGatePattern)];
            expectEquals (juce::String (factory.name), juce::String ("Shifted Long"));
            expectEquals (factory.rateChoice, 2);
            expect (processor.getMetaState().getGatePattern() == makeGatePattern (factory));
        }

        beginTest ("Stereo: the engine pans the gated sound; the right lane survives state and preset roundtrips");
        {
            PolyBriskAudioProcessor processor;
            auto& apvts = processor.getValueTreeState();
            const auto set = [&apvts] (const char* parameterId, float value)
            {
                auto* parameter = apvts.getParameter (parameterId);
                parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
            };

            int pingPong = -1;
            for (size_t i = 0; i < factoryGatePatterns.size(); ++i)
                if (juce::String (factoryGatePatterns[i].name) == "Ping Pong 16")
                    pingPong = static_cast<int> (i);
            expect (pingPong >= 0);

            processor.getMetaState().setFactoryGatePattern (pingPong);
            set (polybrisk::params::id::gateEnabled, 1.0f);
            set (polybrisk::params::id::gateStereo, 1.0f);
            set (polybrisk::params::id::gateRate, 1.0f);    // 1/16 (既定値は 1/32。D-044)
            set (polybrisk::params::id::gateSmooth, 0.5f);
            set (polybrisk::params::id::fxBypass, 1.0f);
            set (polybrisk::params::id::oscA.spread, 0.0f);
            set (polybrisk::params::id::oscA.unisonVoices, 1.0f);

            // 120 BPM、48 kHz: 1/16 = 6000 サンプル。ステップ 0 は左だけ、ステップ 1 は右だけ
            const std::vector<polybrisk::test::TimedMidi> events { { 0, juce::MidiMessage::noteOn (1, 57, 0.9f) } };
            const auto out = polybrisk::test::renderProcessor (processor, 48000.0, { 256 }, 12000, events, 120.0);
            const auto rms = [&out] (int channel, int start) { return out.getRMSLevel (channel, start, 3000); };

            expect (rms (0, 2000) > 0.01f);
            expect (rms (1, 2000) < rms (0, 2000) * 0.05f);
            expect (rms (1, 8000) > 0.01f);
            expect (rms (0, 8000) < rms (1, 8000) * 0.05f);

            // ホストの state
            juce::MemoryBlock block;
            processor.getStateInformation (block);
            PolyBriskAudioProcessor restored;
            restored.setStateInformation (block.getData(), static_cast<int> (block.getSize()));
            expect (restored.getMetaState().getGatePattern() == processor.getMetaState().getGatePattern());
            expect (restored.getMetaState().getGatePattern().isStereo());

            // 右チャンネルだけを編集しても保存される
            processor.getMetaState().setGateStep (3, 0.25f, 1);
            expectEquals (processor.getMetaState().getGatePattern().right[3], 0.25f);
            expectEquals (processor.getMetaState().getGatePattern().values[3], 0.0f);

            // 右のパターンがない古い state は、左と同じになる
            auto tree = processor.getMetaState().rhythmToTree();
            tree.removeProperty ("gatePatternRight", nullptr);
            restored.getMetaState().rhythmFromTree (tree);
            expect (! restored.getMetaState().getGatePattern().isStereo());
        }
    }
};

TranceGateTests tranceGateTests;
} // namespace
