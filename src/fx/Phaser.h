#pragma once

#include "fx/FxParams.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace polybrisk
{

/** PHASER / FLANGER (D-041)。
    - Phaser : 1 次オールパス 6 段を LFO で掃引し、原音と足してノッチを作る。
    - Flanger: 0.3〜7 ms のディレイを LFO で揺らし、原音と足してコムフィルターを作る。
    左右の LFO は 90° ずらす。Feedback は 0..0.9 に制限するので、ループは常に安定する。
    Mix 100 % で原音とエフェクト音が 1:1 (効果が最大) になる。 */
class Phaser
{
public:
    void prepare (double newSampleRate)
    {
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
        size = 1;
        while (size < static_cast<int> (sampleRate * 0.012) + 8)
            size <<= 1;

        for (auto& b : buffer)
            b.assign (static_cast<size_t> (size), 0.0f);

        smoothing = static_cast<float> (1.0 - std::exp (-1.0 / (0.02 * sampleRate)));
        reset();
    }

    void reset() noexcept
    {
        for (auto& b : buffer)
            std::fill (b.begin(), b.end(), 0.0f);

        for (auto& channel : stages)
            for (auto& s : channel)
                s = {};

        feedbackSample[0] = feedbackSample[1] = 0.0f;
        writeIndex = 0;
        phase = 0.0;
        subCount = 0;
        initialised = false;
    }

    void process (float* left, float* right, int numSamples, const PhaserParams& p) noexcept
    {
        if (buffer[0].empty())
            return;

        const float targetDepth = std::clamp (p.depth, 0.0f, 1.0f);
        const float targetFeedback = std::clamp (p.feedback, 0.0f, 0.9f);
        const float targetMix = std::clamp (p.mix, 0.0f, 1.0f);
        const float targetRate = std::clamp (p.rateHz, 0.02f, 8.0f);

        // Type を切り替えたら、掃引している値を作り直す。Phaser の係数 (-1..1) と Flanger のディレイ長 (サンプル数) は
        // 単位が違うので、前の値を引き継ぐとオールパスが不安定になる
        if (initialised && p.type != activeType)
        {
            const float keptDepth = depth, keptFeedback = feedback, keptMix = mix, keptRate = rate;
            const double keptPhase = phase;
            reset();
            depth = keptDepth;
            feedback = keptFeedback;
            mix = keptMix;
            rate = keptRate;
            phase = keptPhase;
            initialised = true;
            activeType = p.type;
            updateModulation (p.type);
            current[0] = target[0];
            current[1] = target[1];
        }

        if (! initialised)
        {
            activeType = p.type;
            depth = targetDepth;
            feedback = targetFeedback;
            mix = targetMix;
            rate = targetRate;
            initialised = true;
            updateModulation (p.type);
            current[0] = target[0];
            current[1] = target[1];
            subCount = 0;
        }

        float* const channels[2] = { left, right };
        const int mask = size - 1;

        for (int i = 0; i < numSamples; ++i)
        {
            depth += (targetDepth - depth) * smoothing;
            feedback += (targetFeedback - feedback) * smoothing;
            mix += (targetMix - mix) * smoothing;
            rate += (targetRate - rate) * smoothing;

            // LFO から求める値 (オールパスの係数 / ディレイ長) は 16 サンプルごとに計算し、その間は直線でつなぐ
            if (subCount <= 0)
            {
                updateModulation (p.type);
                step[0] = (target[0] - current[0]) / static_cast<float> (subBlock);
                step[1] = (target[1] - current[1]) / static_cast<float> (subBlock);
                subCount = subBlock;
            }

            --subCount;
            const float wetAmount = 0.5f * mix;

            for (int ch = 0; ch < 2; ++ch)
            {
                current[ch] += step[ch];
                const float dry = channels[ch][i];
                const float input = dry + feedbackSample[ch] * feedback;
                float wet;

                if (p.type == PhaserType::flanger)
                {
                    auto& line = buffer[static_cast<size_t> (ch)];
                    line[static_cast<size_t> (writeIndex)] = input;

                    const float position = static_cast<float> (writeIndex) - std::clamp (current[ch], 1.0f, static_cast<float> (size - 4));
                    const float floored = std::floor (position);
                    const float fraction = position - floored;
                    const int index = static_cast<int> (floored);
                    const float a = line[static_cast<size_t> (index & mask)];
                    const float b = line[static_cast<size_t> ((index + 1) & mask)];
                    wet = a + (b - a) * fraction;
                }
                else
                {
                    wet = input;
                    const float coefficient = std::clamp (current[ch], -0.9999f, 0.9999f);   // 念のため、常に安定な範囲に保つ

                    for (auto& s : stages[ch])
                    {
                        const float y = coefficient * wet + s.x1 - coefficient * s.y1;
                        s.x1 = wet;
                        s.y1 = y;
                        wet = y;
                    }
                }

                feedbackSample[ch] = wet;
                channels[ch][i] = dry + (wet - dry) * wetAmount;
            }

            writeIndex = (writeIndex + 1) & mask;
        }

        // 不正な入力などで状態が壊れた場合は捨てる
        if (! (std::abs (feedbackSample[0]) < 1.0e6f) || ! (std::abs (feedbackSample[1]) < 1.0e6f))
            reset();
    }

private:
    static constexpr int numStages = 6;
    static constexpr int subBlock = 16;

    struct Stage { float x1 = 0.0f, y1 = 0.0f; };

    /** LFO を進め、次の 16 サンプル後の目標値 (Phaser: オールパス係数、Flanger: ディレイ長 [サンプル]) を求める。 */
    void updateModulation (PhaserType type) noexcept
    {
        constexpr double twoPi = 6.283185307179586;

        for (int ch = 0; ch < 2; ++ch)
        {
            // 0..1 の三角波に近い滑らかな形 (サインを 0..1 に写す)。右は 90° 進める
            const double lfo = 0.5 + 0.5 * std::sin (twoPi * (phase + 0.25 * ch));
            const double swept = 0.5 + (lfo - 0.5) * static_cast<double> (depth);

            if (type == PhaserType::flanger)
            {
                // 0.3 ms 〜 7 ms (指数的に掃引)
                const double seconds = 0.0003 * std::pow (7.0 / 0.3, swept);
                target[ch] = static_cast<float> (std::min (seconds * sampleRate, static_cast<double> (size - 4)));
            }
            else
            {
                // 200 Hz 〜 4 kHz (指数的に掃引)
                const double frequency = std::min (200.0 * std::pow (20.0, swept), 0.45 * sampleRate);
                const double t = std::tan (3.141592653589793 * frequency / sampleRate);
                target[ch] = static_cast<float> ((t - 1.0) / (t + 1.0));
            }
        }

        phase += static_cast<double> (rate) * subBlock / sampleRate;
        phase -= std::floor (phase);
    }

    double sampleRate = 48000.0;
    std::vector<float> buffer[2];
    int size = 0;
    int writeIndex = 0;
    Stage stages[2][numStages];
    float feedbackSample[2] = {};
    float current[2] = {}, target[2] = {}, step[2] = {};
    int subCount = 0;
    double phase = 0.0;
    float depth = 0.0f, feedback = 0.0f, mix = 0.0f, rate = 0.4f;
    float smoothing = 1.0f;
    bool initialised = false;
    PhaserType activeType = PhaserType::phaser;
};

} // namespace polybrisk
