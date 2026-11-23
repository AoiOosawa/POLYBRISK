#include "PluginProcessor.h"
#include "TestHelpers.h"
#include "fx/FxChain.h"
#include "state/Parameters.h"
#include "synth/SynthEngine.h"

#include <cmath>
#include <random>
#include <vector>

namespace
{
using namespace polybrisk;
namespace ids = polybrisk::params::id;
constexpr double pi = 3.141592653589793;

double toDb (double ratio) { return 20.0 * std::log10 (std::max (ratio, 1.0e-12)); }

struct Stereo
{
    std::vector<float> l, r;
    explicit Stereo (int n = 0) : l (static_cast<size_t> (n), 0.0f), r (static_cast<size_t> (n), 0.0f) {}
    int size() const { return static_cast<int> (l.size()); }
};

Stereo sine (int n, double sampleRate, double frequency, float amplitude = 0.5f, double phaseR = 0.0)
{
    Stereo s (n);
    for (int i = 0; i < n; ++i)
    {
        s.l[static_cast<size_t> (i)] = amplitude * static_cast<float> (std::sin (2.0 * pi * frequency * i / sampleRate));
        s.r[static_cast<size_t> (i)] = amplitude * static_cast<float> (std::sin (2.0 * pi * frequency * i / sampleRate + phaseR));
    }
    return s;
}

/** 先頭をフェードインさせた正弦波 (信号そのものの立ち上がりの段差を作らない)。 */
Stereo fadedSine (int n, double sampleRate, double frequency, float amplitude, double phaseR)
{
    auto s = sine (n, sampleRate, frequency, amplitude, phaseR);
    const int fade = static_cast<int> (0.02 * sampleRate);
    for (int i = 0; i < fade && i < n; ++i)
    {
        s.l[static_cast<size_t> (i)] *= static_cast<float> (i) / static_cast<float> (fade);
        s.r[static_cast<size_t> (i)] *= static_cast<float> (i) / static_cast<float> (fade);
    }
    return s;
}

Stereo noise (int n, float amplitude = 0.5f, unsigned seed = 7)
{
    Stereo s (n);
    std::mt19937 rng (seed);
    std::uniform_real_distribution<float> d (-amplitude, amplitude);
    for (int i = 0; i < n; ++i)
    {
        s.l[static_cast<size_t> (i)] = d (rng);
        s.r[static_cast<size_t> (i)] = d (rng);
    }
    return s;
}

Stereo impulse (int n, float amplitude = 0.5f)
{
    Stereo s (n);
    s.l[0] = s.r[0] = amplitude;
    return s;
}

/** FxChain を 128 サンプル以下のブロックで駆動する。perBlock でブロックごとに設定を変えられる。 */
template <typename PerBlock>
Stereo runChain (FxChain& chain, Stereo signal, FxParams params, double bpm, PerBlock&& perBlock, int block = 128)
{
    for (int pos = 0, index = 0; pos < signal.size(); pos += block, ++index)
    {
        perBlock (params, index, pos);
        chain.process (signal.l.data() + pos, signal.r.data() + pos, std::min (block, signal.size() - pos), params, bpm);
    }
    return signal;
}

Stereo runChain (double sampleRate, const Stereo& signal, const FxParams& params, double bpm = 120.0, int block = 128)
{
    FxChain chain;
    chain.prepare (sampleRate);
    return runChain (chain, signal, params, bpm, [] (FxParams&, int, int) {}, block);
}

bool finite (const Stereo& s)
{
    for (int i = 0; i < s.size(); ++i)
        if (! std::isfinite (s.l[static_cast<size_t> (i)]) || ! std::isfinite (s.r[static_cast<size_t> (i)]))
            return false;
    return true;
}

float peak (const std::vector<float>& x, int start = 0, int length = -1)
{
    const int end = length < 0 ? static_cast<int> (x.size()) : std::min (static_cast<int> (x.size()), start + length);
    float p = 0.0f;
    for (int i = start; i < end; ++i)
        p = std::max (p, std::abs (x[static_cast<size_t> (i)]));
    return p;
}

double rmsOf (const std::vector<float>& x, int start, int length)
{
    return polybrisk::test::rms (x.data() + start, length);
}

int firstAbove (const std::vector<float>& x, float threshold, int start = 0)
{
    for (int i = start; i < static_cast<int> (x.size()); ++i)
        if (std::abs (x[static_cast<size_t> (i)]) > threshold)
            return i;
    return -1;
}

void setParam (PolyBriskAudioProcessor& processor, const char* parameterId, float realValue)
{
    auto* param = processor.getValueTreeState().getParameter (parameterId);
    param->setValueNotifyingHost (param->convertTo0to1 (realValue));
}

class FxTests final : public juce::UnitTest
{
public:
    FxTests() : juce::UnitTest ("FX", "fx") {}

    void runTest() override
    {
        const double sr = 48000.0;

        beginTest ("All FX off (default) is an exact pass-through; so is FX Bypass with everything enabled");
        {
            const auto in = noise (9600);
            FxParams p;
            auto out = runChain (sr, in, p);
            expect (out.l == in.l && out.r == in.r);

            p.drive.enabled = p.chorus.enabled = p.eq.enabled = p.imager.enabled = p.delay.enabled = p.reverb.enabled = true;
            p.eq.lowGainDb = 12.0f;
            p.bypassAll = true;
            out = runChain (sr, in, p);
            expect (out.l == in.l && out.r == in.r);
        }

        beginTest ("Switching each FX (and the global bypass) on and off while playing does not click");
        {
            // 220 Hz 正弦波の最大傾き。FX による音量変化の分だけ余裕を見る
            const float slope = 0.5f * static_cast<float> (2.0 * pi * 220.0 / sr);

            for (int which = 0; which < 7; ++which)
            {
                FxChain chain;
                chain.prepare (sr);
                FxParams base;
                base.eq.lowGainDb = 6.0f;
                base.imager.width = 0.5f;
                base.drive.drive = 0.4f;
                base.delay.sync = false;
                base.delay.timeSeconds = 0.05f;

                if (which == 6) // 一括バイパスは、全 FX を ON にした状態で切り替える
                    base.drive.enabled = base.chorus.enabled = base.eq.enabled = base.imager.enabled = base.delay.enabled = base.reverb.enabled = true;

                const auto out = runChain (chain, fadedSine (48000, sr, 220.0, 0.5f, 0.6), base, 120.0,
                                           [which] (FxParams& p, int index, int)
                                           {
                                               const bool on = (index / 40) % 2 == 1;
                                               switch (which)
                                               {
                                                   case 0: p.drive.enabled = on; break;
                                                   case 1: p.chorus.enabled = on; break;
                                                   case 2: p.eq.enabled = on; break;
                                                   case 3: p.imager.enabled = on; break;
                                                   case 4: p.delay.enabled = on; break;
                                                   case 5: p.reverb.enabled = on; break;
                                                   default: p.bypassAll = on; break;
                                               }
                                           });

                expect (finite (out));
                const float delta = std::max (polybrisk::test::maxAbsDelta (out.l.data(), out.size()),
                                              polybrisk::test::maxAbsDelta (out.r.data(), out.size()));

                // 常時 ON のときの波形そのものの最大傾き (Drive で波形が急峻になる分) を基準に含める
                FxChain steadyChain;
                steadyChain.prepare (sr);
                auto steadyParams = base;
                if (which == 0) steadyParams.drive.enabled = true;
                const auto steady = runChain (steadyChain, fadedSine (48000, sr, 220.0, 0.5f, 0.6), steadyParams, 120.0, [] (FxParams&, int, int) {});
                const float steadyDelta = std::max (polybrisk::test::maxAbsDelta (steady.l.data() + 4800, 43200),
                                                    polybrisk::test::maxAbsDelta (steady.r.data() + 4800, 43200));

                expect (delta < std::max (slope * 3.0f, steadyDelta * 1.5f),
                        "fx " + juce::String (which) + " delta " + juce::String (delta / slope, 2) + "x, steady " + juce::String (steadyDelta / slope, 2) + "x");
            }
        }

        // ------------------------------------------------------------------ Drive
        beginTest ("Drive: six curves are bounded, distinct, add harmonics, and Mix 0 is dry");
        {
            const DriveType types[] = { DriveType::warm, DriveType::tape, DriveType::tube, DriveType::softClip, DriveType::hardClip, DriveType::wavefold };
            std::vector<std::vector<float>> outputs;
            const auto in = sine (1 << 15, sr, 1000.0, 0.5f);

            for (const auto type : types)
            {
                FxParams p;
                p.drive.enabled = true;
                p.drive.type = type;
                p.drive.drive = 0.4f;
                const auto out = runChain (sr, in, p);
                expect (finite (out));
                expect (peak (out.l) < 1.5f);
                expect (std::abs (polybrisk::test::dcOffset (out.l.data() + 9600, 19200)) < 0.01);  // Tube の非対称でも DC は残らない

                const auto spectrum = polybrisk::test::magnitudeSpectrum (out.l.data(), 15);
                const double fundamental = polybrisk::test::magnitudeNear (spectrum, sr, 1000.0);
                const double second = polybrisk::test::magnitudeNear (spectrum, sr, 2000.0);
                const double third = polybrisk::test::magnitudeNear (spectrum, sr, 3000.0);
                expect (toDb (std::max (second, third) / fundamental) > -30.0);

                if (type == DriveType::tube)
                    expect (toDb (second / fundamental) > -30.0);   // 偶数次倍音
                else
                    expect (toDb (second / fundamental) < -60.0);   // 対称カーブは奇数次だけ

                // Auto Gain: 歪ませても音量が大きく変わらない
                expectWithinAbsoluteError (toDb (rmsOf (out.l, 9600, 19200) / rmsOf (in.l, 9600, 19200)), 0.0, 6.0);
                outputs.push_back (out.l);
            }

            for (size_t a = 0; a < outputs.size(); ++a)
                for (size_t b = a + 1; b < outputs.size(); ++b)
                {
                    std::vector<float> diff (outputs[a].size());
                    for (size_t i = 0; i < diff.size(); ++i)
                        diff[i] = outputs[a][i] - outputs[b][i];
                    expect (toDb (rmsOf (diff, 9600, 19200) / rmsOf (outputs[a], 9600, 19200)) > -32.0,
                            "curves " + juce::String (static_cast<int> (a)) + " / " + juce::String (static_cast<int> (b)) + " too similar");
                }

            FxParams dry;
            dry.drive.enabled = true;
            dry.drive.drive = 1.0f;
            dry.drive.mix = 0.0f;
            // Mix 0 は原音だけ: 歪みは乗らず、音量も変わらない。ただし歪ませた側と位相をそろえるフィルターを通るので、
            // 数サンプル遅れる (サンプル単位では入力と一致しない。D-069)
            const auto out = runChain (sr, in, dry);
            expectWithinAbsoluteError (toDb (rmsOf (out.l, 9600, 19200) / rmsOf (in.l, 9600, 19200)), 0.0, 0.01);
            const auto drySpectrum = polybrisk::test::magnitudeSpectrum (out.l.data(), 15);
            const double dryFundamental = polybrisk::test::magnitudeNear (drySpectrum, sr, 1000.0);
            expect (toDb (polybrisk::test::magnitudeNear (drySpectrum, sr, 3000.0) / dryFundamental) < -80.0);
            expect (toDb (polybrisk::test::magnitudeNear (drySpectrum, sr, 2000.0) / dryFundamental) < -80.0);
        }

        beginTest ("Drive: Mix blends phase-aligned signals, so a partial Mix has no comb notch (D-069)");
        {
            // ほぼ線形な設定 (Drive 0、小さい入力) では、Mix を途中にしても周波数特性は平らなはず。
            // 以前は遅れのない原音と、オーバーサンプリングで数サンプル遅れた音を混ぜていて、高域に谷ができていた
            double worstDb = 0.0;
            juce::String report;

            for (const double hz : { 500.0, 1000.0, 2000.0, 3000.0, 4000.0, 5000.0, 6000.0, 8000.0, 10000.0, 12000.0 })
            {
                const auto in = sine (1 << 14, sr, hz, 0.01f);
                FxParams p;
                p.drive.enabled = true;
                p.drive.type = DriveType::warm;
                p.drive.drive = 0.0f;
                p.drive.autoGain = false;
                p.drive.tone = 1.0f;

                p.drive.mix = 1.0f;
                const auto wet = runChain (sr, in, p);
                p.drive.mix = 0.5f;
                const auto half = runChain (sr, in, p);

                // Mix 100 % (Tone のフィルターを含む) と Mix 0 % (原音) の間に収まること
                const double wetDb = toDb (rmsOf (wet.l, 4800, 9600) / rmsOf (in.l, 4800, 9600));
                const double halfDb = toDb (rmsOf (half.l, 4800, 9600) / rmsOf (in.l, 4800, 9600));
                worstDb = std::min (worstDb, halfDb - std::min (wetDb, 0.0));
                report << juce::String (hz, 0) << " Hz: " << juce::String (halfDb, 2) << " dB (wet " << juce::String (wetDb, 2) << ")  ";
            }

            logMessage (report);
            expectGreaterThan (worstDb, -0.5, report);
        }

        beginTest ("Drive: oversampling keeps aliasing low; Output and Tone work");
        {
            // 5 kHz を強く歪ませる。倍音 (15 / 25 / 35 kHz …) のうち 24 kHz を超えるものが折り返す
            const auto in = sine (1 << 15, sr, 5000.0, 0.5f);

            for (const auto type : { DriveType::warm, DriveType::hardClip, DriveType::wavefold })
            {
                FxParams p;
                p.drive.enabled = true;
                p.drive.type = type;
                p.drive.drive = 0.8f;
                const auto out = runChain (sr, in, p);
                const auto spectrum = polybrisk::test::magnitudeSpectrum (out.l.data(), 15);
                const double alias = polybrisk::test::worstInharmonicDb (spectrum, sr, 5000.0, 20000.0);
                logMessage ("  drive type " + juce::String (static_cast<int> (type)) + " (4x): worst aliasing below 20 kHz " + juce::String (alias, 1) + " dB");
                expect (alias < -40.0);
            }

            FxParams p;
            p.drive.enabled = true;
            const auto reference = runChain (sr, sine (19200, sr, 1000.0, 0.1f), p);
            p.drive.outputDb = -12.0f;
            const auto quieter = runChain (sr, sine (19200, sr, 1000.0, 0.1f), p);
            expectWithinAbsoluteError (toDb (rmsOf (quieter.l, 9600, 9600) / rmsOf (reference.l, 9600, 9600)), -12.0, 0.2);

            p.drive.outputDb = 0.0f;
            const auto bright = runChain (sr, sine (19200, sr, 8000.0, 0.1f), p);
            p.drive.tone = 0.0f;
            const auto dark = runChain (sr, sine (19200, sr, 8000.0, 0.1f), p);
            expect (toDb (rmsOf (dark.l, 9600, 9600) / rmsOf (bright.l, 9600, 9600)) < -12.0);
        }

        // ------------------------------------------------------------------ Chorus
        beginTest ("Chorus: stereo from mono, Width 0 is mono, Mix 0 is dry, no pitch jumps at extreme settings");
        {
            const auto in = sine (48000, sr, 440.0, 0.5f);

            for (const auto type : { ChorusType::classic, ChorusType::wide })
            {
                FxParams p;
                p.chorus.enabled = true;
                p.chorus.type = type;
                auto out = runChain (sr, in, p);
                expect (finite (out));

                std::vector<float> side (out.l.size());
                for (size_t i = 0; i < side.size(); ++i)
                    side[i] = out.l[i] - out.r[i];
                expect (rmsOf (side, 9600, 38400) > 0.02);      // モノラル入力がステレオになる

                p.chorus.width = 0.0f;
                out = runChain (sr, in, p);
                for (size_t i = 0; i < side.size(); ++i)
                    side[i] = out.l[i] - out.r[i];
                expect (peak (side) < 1.0e-6f);

                p.chorus.width = 1.0f;
                p.chorus.mix = 0.0f;
                out = runChain (sr, in, p);
                float worst = 0.0f;
                for (size_t i = 0; i < in.l.size(); ++i)
                    worst = std::max (worst, std::abs (out.l[i] - in.l[i]));
                expect (worst < 1.0e-6f);

                // 最大の Rate / Depth でも、440 Hz を 1 オクターブ上げた傾きを超えない
                p.chorus.mix = 1.0f;
                p.chorus.rateHz = 8.0f;
                p.chorus.depth = 1.0f;
                out = runChain (sr, in, p);
                const float slope = 0.5f * static_cast<float> (2.0 * pi * 880.0 / sr) * (type == ChorusType::wide ? 1.8f : 1.0f);
                expect (polybrisk::test::maxAbsDelta (out.l.data() + 4800, 43200) < slope);
            }

            // Classic と Wide は別の音
            FxParams p;
            p.chorus.enabled = true;
            const auto classic = runChain (sr, in, p);
            p.chorus.type = ChorusType::wide;
            const auto wide = runChain (sr, in, p);
            std::vector<float> diff (classic.l.size());
            for (size_t i = 0; i < diff.size(); ++i)
                diff[i] = classic.l[i] - wide.l[i];
            expect (toDb (rmsOf (diff, 9600, 38400) / rmsOf (classic.l, 9600, 38400)) > -20.0);
        }

        // ------------------------------------------------------------------ EQ
        beginTest ("EQ: flat at 0 dB; each band applies its gain at the right frequency, at every sample rate");
        for (const double rate : { 44100.0, 48000.0, 96000.0 })
        {
            const int n = static_cast<int> (rate);
            const auto gainAt = [&] (const EqParams& eq, double frequency)
            {
                FxParams p;
                p.eq = eq;
                p.eq.enabled = true;
                const auto in = sine (n, rate, frequency, 0.1f);
                const auto out = runChain (rate, in, p);
                return toDb (rmsOf (out.l, n / 2, n / 2) / rmsOf (in.l, n / 2, n / 2));
            };

            EqParams flat;
            for (const double f : { 50.0, 1000.0, 12000.0 })
                expectWithinAbsoluteError (gainAt (flat, f), 0.0, 0.01);

            EqParams low;
            low.lowGainDb = 12.0f;
            expectWithinAbsoluteError (gainAt (low, 25.0), 12.0, 0.7);
            expectWithinAbsoluteError (gainAt (low, 100.0), 6.0, 0.7);      // シェルフの中点
            expectWithinAbsoluteError (gainAt (low, 2000.0), 0.0, 0.3);

            EqParams mid;
            mid.midGainDb = -9.0f;
            mid.midFreqHz = 2500.0f;
            expectWithinAbsoluteError (gainAt (mid, 2500.0), -9.0, 0.3);
            expectWithinAbsoluteError (gainAt (mid, 100.0), 0.0, 0.3);
            expectWithinAbsoluteError (gainAt (mid, 16000.0), 0.0, 1.0);

            EqParams high;
            high.highGainDb = -12.0f;
            expectWithinAbsoluteError (gainAt (high, 8000.0), -6.0, 0.8);
            expectWithinAbsoluteError (gainAt (high, 19000.0), -12.0, 1.5);
            expectWithinAbsoluteError (gainAt (high, 300.0), 0.0, 0.3);
        }

        beginTest ("EQ: sweeping all bands while playing stays smooth and finite");
        {
            FxChain chain;
            chain.prepare (sr);
            FxParams p;
            p.eq.enabled = true;
            const auto out = runChain (chain, sine (48000, sr, 220.0, 0.25f), p, 120.0,
                                       [] (FxParams& q, int index, int)
                                       {
                                           const bool flip = (index / 20) % 2 == 0;
                                           q.eq.lowGainDb = flip ? 18.0f : -18.0f;
                                           q.eq.midGainDb = flip ? -18.0f : 18.0f;
                                           q.eq.midFreqHz = flip ? 200.0f : 8000.0f;
                                           q.eq.highGainDb = flip ? 18.0f : -18.0f;
                                       });
            expect (finite (out));
            const float slope = 0.25f * static_cast<float> (2.0 * pi * 220.0 / sr) * 7.94f;  // +18 dB の持ち上げ
            expect (polybrisk::test::maxAbsDelta (out.l.data(), out.size()) < slope * 1.5f);
        }

        // ------------------------------------------------------------------ Imager
        beginTest ("Imager: 0% is mono, 100% is unchanged, 200% widens with level compensation, mono sum survives");
        {
            const auto in = sine (19200, sr, 500.0, 0.4f, 1.2);
            const auto run = [&] (float width, bool monoBass = false)
            {
                FxParams p;
                p.imager.enabled = true;
                p.imager.width = width;
                p.imager.monoBass = monoBass;
                return runChain (sr, in, p);
            };

            const auto mono = run (0.0f);
            const auto same = run (1.0f);
            const auto wide = run (2.0f);

            float monoDiff = 0.0f, sameDiff = 0.0f, sumDiff = 0.0f;
            for (size_t i = 0; i < in.l.size(); ++i)
            {
                monoDiff = std::max (monoDiff, std::abs (mono.l[i] - mono.r[i]));
                sameDiff = std::max ({ sameDiff, std::abs (same.l[i] - in.l[i]), std::abs (same.r[i] - in.r[i]) });
                sumDiff = std::max (sumDiff, std::abs ((mono.l[i] + mono.r[i]) - (in.l[i] + in.r[i])));
            }
            expect (monoDiff < 1.0e-6f);
            expect (sameDiff < 1.0e-6f);
            expect (sumDiff < 1.0e-6f);     // Width を絞ってもモノラル合成は変わらない

            // 200%: Side / Mid の比が 2 倍、ピークは補正で抑えられる
            const auto ratio = [] (const Stereo& s)
            {
                double side = 0.0, mid = 0.0;
                for (size_t i = 4800; i < s.l.size(); ++i)
                {
                    side += std::pow (s.l[i] - s.r[i], 2.0);
                    mid += std::pow (s.l[i] + s.r[i], 2.0);
                }
                return std::sqrt (side / mid);
            };
            expectWithinAbsoluteError (ratio (wide) / ratio (in), 2.0, 0.01);
            expect (peak (wide.l, 4800) < peak (in.l, 4800) * 1.3f);
            expectWithinAbsoluteError (Imager::outputCompensation (2.0f), 0.632f, 0.001f);
            expectEquals (Imager::outputCompensation (1.0f), 1.0f);
        }

        beginTest ("Imager Mono Bass: removes the low side signal only");
        {
            const auto sideDb = [&] (double frequency)
            {
                FxParams p;
                p.imager.enabled = true;
                p.imager.monoBass = true;
                const auto in = sine (48000, sr, frequency, 0.4f, pi);     // 完全な逆相 = Side だけ
                const auto out = runChain (sr, in, p);
                return toDb (rmsOf (out.l, 24000, 24000) / rmsOf (in.l, 24000, 24000));
            };

            expect (sideDb (50.0) < -17.0);
            expectWithinAbsoluteError (sideDb (150.0), -3.0, 0.5);
            expectWithinAbsoluteError (sideDb (2000.0), 0.0, 0.1);

            // 同相 (Mid) の低音はそのまま残る
            FxParams p;
            p.imager.enabled = true;
            p.imager.monoBass = true;
            const auto in = sine (48000, sr, 50.0, 0.4f);
            const auto out = runChain (sr, in, p);
            expectWithinAbsoluteError (toDb (rmsOf (out.l, 24000, 24000) / rmsOf (in.l, 24000, 24000)), 0.0, 0.01);
        }

        // ------------------------------------------------------------------ Delay
        beginTest ("Delay: tempo-synced divisions land on the exact time; 120 BPM fallback; free time");
        {
            expectWithinAbsoluteError (Delay::timeSeconds ({ .sync = true, .division = DelayDivision::quarter }, 120.0), 0.5, 1.0e-9);
            expectWithinAbsoluteError (Delay::timeSeconds ({ .sync = true, .division = DelayDivision::eighthDotted }, 120.0), 0.375, 1.0e-9);
            expectWithinAbsoluteError (Delay::timeSeconds ({ .sync = true, .division = DelayDivision::sixteenthTriplet }, 120.0), 1.0 / 12.0, 1.0e-9);
            expectWithinAbsoluteError (Delay::timeSeconds ({ .sync = true, .division = DelayDivision::eighth }, 150.0), 0.2, 1.0e-9);
            expectWithinAbsoluteError (Delay::timeSeconds ({ .sync = true, .division = DelayDivision::eighth }, 0.0), 0.25, 1.0e-9);     // フォールバック
            expectWithinAbsoluteError (Delay::timeSeconds ({ .sync = true, .division = DelayDivision::eighth }, std::nan ("")), 0.25, 1.0e-9);
            expectWithinAbsoluteError (Delay::timeSeconds ({ .sync = false, .timeSeconds = 0.123f }, 90.0), 0.123, 1.0e-6);
            expect (Delay::timeSeconds ({ .sync = true, .division = DelayDivision::quarterDotted }, 20.0) < Delay::maxSeconds);

            for (const double rate : { 44100.0, 48000.0, 96000.0 })
            {
                FxParams p;
                p.delay.enabled = true;
                p.delay.sync = true;
                p.delay.division = DelayDivision::eighthDotted;
                p.delay.feedback = 0.5f;
                p.delay.tone = 1.0f;
                p.delay.mix = 0.5f;
                const auto out = runChain (rate, impulse (static_cast<int> (rate * 1.5)), p, 120.0);

                const int first = firstAbove (out.l, 0.05f, 16);
                expectWithinAbsoluteError (static_cast<double> (first), 0.375 * rate, 3.0);
                const int second = firstAbove (out.l, 0.02f, first + 100);
                expectWithinAbsoluteError (static_cast<double> (second), 0.75 * rate, 4.0);

                // 2 回目は 1 回目の Feedback 倍 (フィルターの分だけわずかに小さい)
                const float ratio = peak (out.l, second - 8, 64) / peak (out.l, first - 8, 64);
                expect (ratio > 0.3f && ratio < 0.52f, "echo ratio " + juce::String (ratio));
            }
        }

        beginTest ("Delay: Ping Pong alternates left / right; Mix law keeps the dry signal up to 50%");
        {
            FxParams p;
            p.delay.enabled = true;
            p.delay.mode = DelayMode::pingPong;
            p.delay.sync = false;
            p.delay.timeSeconds = 0.1f;
            p.delay.feedback = 0.6f;
            p.delay.mix = 1.0f;     // wet のみ
            const auto out = runChain (sr, impulse (48000), p);

            expect (peak (out.l, 0, 64) < 1.0e-6f);                             // Mix 100% では dry は出ない
            expect (peak (out.l, 4790, 64) > 0.1f && peak (out.r, 4790, 64) < 0.01f);   // 1 回目: 左
            expect (peak (out.r, 9590, 64) > 0.05f && peak (out.l, 9590, 64) < 0.01f);  // 2 回目: 右
            expect (peak (out.l, 14390, 64) > 0.03f && peak (out.r, 14390, 64) < 0.01f); // 3 回目: 左

            expectEquals (FxChain::dryGainFor (0.25f), 1.0f);
            expectEquals (FxChain::dryGainFor (0.5f), 1.0f);
            expectEquals (FxChain::dryGainFor (1.0f), 0.0f);
            expectEquals (FxChain::wetGainFor (0.0f), 0.0f);
            expectEquals (FxChain::wetGainFor (0.5f), 1.0f);
            expectEquals (FxChain::wetGainFor (1.0f), 1.0f);
        }

        beginTest ("Delay: maximum feedback stays bounded; time changes do not click; ducking lowers the wet signal");
        {
            FxParams p;
            p.delay.enabled = true;
            p.delay.sync = false;
            p.delay.timeSeconds = 0.03f;
            p.delay.feedback = 0.95f;
            p.delay.tone = 1.0f;
            p.delay.mix = 0.5f;

            for (const auto mode : { DelayMode::stereo, DelayMode::pingPong })
            {
                p.delay.mode = mode;
                const auto out = runChain (sr, noise (48000 * 20, 0.5f), p);
                expect (finite (out));
                expect (peak (out.l) < 12.0f, "peak " + juce::String (peak (out.l)));
            }

            // 演奏中に Time を大きく動かす
            FxChain chain;
            chain.prepare (sr);
            p.delay.mode = DelayMode::stereo;
            p.delay.feedback = 0.3f;
            const auto swept = runChain (chain, sine (96000, sr, 220.0, 0.25f), p, 120.0,
                                         [] (FxParams& q, int index, int) { q.delay.timeSeconds = (index / 30) % 2 == 0 ? 0.03f : 0.7f; });
            const float slope = 0.25f * static_cast<float> (2.0 * pi * 220.0 / sr);
            expect (polybrisk::test::maxAbsDelta (swept.l.data(), swept.size()) < slope * 3.0f);

            // Ducking: 入力が鳴っている間の wet が下がる
            const auto wetLevel = [&] (float ducking)
            {
                FxParams q;
                q.delay.enabled = true;
                q.delay.sync = false;
                q.delay.timeSeconds = 0.2f;
                q.delay.feedback = 0.0f;
                q.delay.mix = 1.0f;
                q.delay.ducking = ducking;
                const auto out = runChain (sr, sine (48000, sr, 440.0, 0.5f), q);
                return rmsOf (out.l, 24000, 24000);
            };
            expect (toDb (wetLevel (1.0f) / wetLevel (0.0f)) < -20.0);
            expect (toDb (wetLevel (0.5f) / wetLevel (0.0f)) < -4.0);
        }

        // ------------------------------------------------------------------ Reverb
        beginTest ("Reverb: decay time follows the Decay setting; Pre-delay shifts the onset; stays bounded");
        {
            const auto render = [&] (ReverbParams reverb, double seconds)
            {
                FxParams p;
                p.reverb = reverb;
                p.reverb.enabled = true;
                p.reverb.mix = 1.0f;
                return runChain (sr, impulse (static_cast<int> (sr * seconds), 1.0f), p);
            };

            for (const auto type : { ReverbType::room, ReverbType::plate, ReverbType::hall })
            {
                for (const float decay : { 1.0f, 4.0f })
                {
                    ReverbParams reverb;
                    reverb.type = type;
                    reverb.decaySeconds = decay;
                    reverb.tone = 1.0f;
                    reverb.preDelaySeconds = 0.0f;
                    const auto out = render (reverb, decay * 1.2);

                    // 0.2 T と 0.7 T の区間のレベル差から RT60 を見積もる (半分の時間で -30 dB のはず)
                    const int window = static_cast<int> (0.1 * decay * sr);
                    const double early = rmsOf (out.l, static_cast<int> (0.2 * decay * sr), window);
                    const double late = rmsOf (out.l, static_cast<int> (0.7 * decay * sr), window);
                    const double measured = -60.0 * (0.5 * decay) / toDb (late / early);
                    expectWithinAbsoluteError (measured / decay, 1.0, 0.3,
                                               "type " + juce::String (static_cast<int> (type)) + " decay " + juce::String (decay) + " -> " + juce::String (measured, 2));
                    expect (finite (out));
                }
            }

            ReverbParams late;
            late.preDelaySeconds = 0.1f;
            const auto delayed = render (late, 0.5);
            expect (peak (delayed.l, 0, 4700) < 1.0e-6f);
            expect (peak (delayed.l, 4700, 2400) > 1.0e-3f);

            ReverbParams extreme;
            extreme.type = ReverbType::hall;
            extreme.decaySeconds = 15.0f;
            extreme.size = 1.0f;
            extreme.tone = 1.0f;
            FxParams p;
            p.reverb = extreme;
            p.reverb.enabled = true;
            p.reverb.mix = 1.0f;
            const auto loud = runChain (sr, noise (48000 * 20, 0.5f), p);
            expect (finite (loud));
            expect (peak (loud.l) < 8.0f, "peak " + juce::String (peak (loud.l)));
        }

        beginTest ("Reverb: Room / Plate / Hall are clearly different; switching type or size does not click");
        {
            const auto envelope = [&] (ReverbType type)
            {
                FxParams p;
                p.reverb.enabled = true;
                p.reverb.type = type;
                p.reverb.mix = 1.0f;
                p.reverb.preDelaySeconds = 0.0f;
                const auto out = runChain (sr, impulse (48000, 1.0f), p);

                // 最初の 30 ms のエネルギーが全体に占める割合 (初期反射の近さ) と、左右の相関
                double early = 0.0, total = 0.0;
                for (int i = 0; i < out.size(); ++i)
                {
                    const double e = std::pow (out.l[static_cast<size_t> (i)], 2.0);
                    total += e;
                    if (i < 1440)
                        early += e;
                }
                return early / total;
            };

            const double room = envelope (ReverbType::room);
            const double plate = envelope (ReverbType::plate);
            const double hall = envelope (ReverbType::hall);
            logMessage ("  energy in the first 30 ms: room " + juce::String (100.0 * room, 1) + " %, plate "
                        + juce::String (100.0 * plate, 1) + " %, hall " + juce::String (100.0 * hall, 1) + " %");
            expect (room > plate * 1.3);
            expect (plate > hall * 1.3);

            FxChain chain;
            chain.prepare (sr);
            FxParams p;
            p.reverb.enabled = true;
            p.reverb.mix = 0.5f;
            const auto out = runChain (chain, sine (96000, sr, 220.0, 0.25f), p, 120.0,
                                       [] (FxParams& q, int index, int)
                                       {
                                           q.reverb.type = static_cast<ReverbType> ((index / 60) % 3);
                                           q.reverb.size = (index / 45) % 2 == 0 ? 0.1f : 0.9f;
                                       });
            expect (finite (out));
            const float slope = 0.25f * static_cast<float> (2.0 * pi * 220.0 / sr);
            expect (polybrisk::test::maxAbsDelta (out.l.data(), out.size()) < slope * 4.0f);
        }

        // ------------------------------------------------------------------ Chain / engine
        beginTest ("Delay and Reverb are parallel: each wet path is independent of the other");
        {
            const auto in = impulse (48000, 1.0f);
            FxParams both;
            both.delay.enabled = both.reverb.enabled = true;
            both.delay.sync = false;
            both.delay.timeSeconds = 0.2f;
            both.delay.mix = 0.5f;
            both.reverb.mix = 0.5f;

            auto onlyDelay = both;
            onlyDelay.reverb.enabled = false;
            auto onlyReverb = both;
            onlyReverb.delay.enabled = false;

            const auto a = runChain (sr, in, both);
            const auto b = runChain (sr, in, onlyDelay);
            const auto c = runChain (sr, in, onlyReverb);

            // both = dry + delayWet + reverbWet なので、(delay のみ) + (reverb のみ) - dry と一致する
            float worst = 0.0f;
            for (size_t i = 0; i < in.l.size(); ++i)
                worst = std::max (worst, std::abs (a.l[i] - (b.l[i] + c.l[i] - in.l[i])));
            expect (worst < 1.0e-5f);
        }

        beginTest ("Engine: tails ring out after note-off, then the output becomes exactly silent");
        {
            SynthEngine engine;
            engine.prepare (sr);
            EngineParams p;
            p.amp = { 0.001f, 0.05f, 1.0f, 0.01f };
            p.fx.delay.enabled = true;
            p.fx.delay.sync = false;
            p.fx.delay.timeSeconds = 0.25f;
            p.fx.delay.feedback = 0.4f;
            p.fx.reverb.enabled = true;
            p.fx.reverb.decaySeconds = 1.0f;
            engine.setParams (p);
            engine.noteOn (60, 1.0f);

            const auto render = [&] (int n)
            {
                juce::AudioBuffer<float> out (2, n);
                for (int pos = 0; pos < n; pos += 256)
                    engine.render (out.getWritePointer (0, pos), out.getWritePointer (1, pos), std::min (256, n - pos));
                return out;
            };

            render (4800);
            engine.noteOff (60);
            const auto tail = render (48000);
            expectEquals (engine.getActiveVoiceCount(), 0);
            expect (polybrisk::test::peakLevel (tail, 9600, 9600) > 1.0e-3f);   // 0.2〜0.4 秒後も尾が鳴っている
            expect (! engine.isOutputSilent());

            render (48000 * 12);
            const auto silence = render (4800);
            expect (engine.isOutputSilent());
            expectEquals (polybrisk::test::peakLevel (silence), 0.0f);
        }

        beginTest ("Processor: every FX on, 16-note chords, sample rate x block size matrix is finite and bounded");
        for (const double rate : { 44100.0, 48000.0, 96000.0 })
        {
            for (const int block : { 32, 128, 512, 2048 })
            {
                PolyBriskAudioProcessor processor;
                expectEquals (processor.getLatencySamples(), 0);

                for (const auto* enabled : { ids::driveEnabled, ids::chorusEnabled, ids::eqEnabled, ids::imagerEnabled, ids::delayEnabled, ids::reverbEnabled, ids::oscB.enabled })
                    setParam (processor, enabled, 1.0f);

                setParam (processor, ids::oscA.unisonVoices, 7.0f);
                setParam (processor, ids::driveDrive, 80.0f);
                setParam (processor, ids::eqLowGain, 9.0f);
                setParam (processor, ids::imagerWidth, 180.0f);
                setParam (processor, ids::delayFeedback, 95.0f);
                setParam (processor, ids::reverbDecay, 15.0f);
                setParam (processor, ids::reverbMix, 60.0f);

                std::vector<polybrisk::test::TimedMidi> events;
                for (int hit = 0; hit < 6; ++hit)
                    for (int n = 0; n < 16; ++n)
                        events.push_back ({ static_cast<int> (hit * 0.15 * rate), juce::MidiMessage::noteOn (1, 30 + n * 4 + hit, 1.0f) });

                const auto out = polybrisk::test::renderProcessor (processor, rate, { block }, static_cast<int> (rate * 1.5), events);
                expect (polybrisk::test::allFinite (out));
                expect (polybrisk::test::peakLevel (out) <= 1.0f);
                expect (polybrisk::test::peakLevel (out) > 0.1f);
            }
        }
    }
};

FxTests fxTests;
} // namespace
