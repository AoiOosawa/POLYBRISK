#pragma once

#include "fx/FxParams.h"

#include <algorithm>
#include <cmath>

namespace polybrisk
{

/** STEREO IMAGER: M/S で最終的なステレオ幅を変える (SPEC §8.4)。
    Mid には触れないので、Width <= 100% ではモノラル合成 (L + R) は Width や Mono Bass の影響を受けない
    (100% を超えると、ピークを抑える出力補正の分だけ全体が下がる)。
    Mono Bass は Side だけに 150 Hz の 2 次ハイパスを掛ける。 */
class Imager
{
public:
    static constexpr double monoBassHz = 150.0;

    void prepare (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate;
        smoothing = static_cast<float> (1.0 - std::exp (-1.0 / (0.01 * sampleRate)));

        // 2 次 Butterworth ハイパス (TPT SVF)
        g = std::tan (3.141592653589793 * monoBassHz / sampleRate);
        k = 1.4142135623730951;
        reset();
    }

    void reset() noexcept
    {
        ic1 = ic2 = 0.0;
        initialised = false;
    }

    /** Side を増やしたときに L / R のピークが上がりすぎないようにする補正。Width <= 1 では 1。 */
    static float outputCompensation (float width) noexcept
    {
        return width <= 1.0f ? 1.0f : 1.0f / std::sqrt (0.5f * (1.0f + width * width));
    }

    void process (float* left, float* right, int numSamples, const ImagerParams& p) noexcept
    {
        const float targetWidth = std::clamp (p.width, 0.0f, 2.0f);
        const float targetBass = p.monoBass ? 1.0f : 0.0f;

        if (! initialised)
        {
            width = targetWidth;
            bass = targetBass;
            initialised = true;
        }

        const double a1 = 1.0 / (1.0 + g * (g + k));
        const double a2 = g * a1;
        const double a3 = g * a2;

        for (int i = 0; i < numSamples; ++i)
        {
            width += (targetWidth - width) * smoothing;
            bass += (targetBass - bass) * smoothing;

            const float mid = 0.5f * (left[i] + right[i]);
            const float side = 0.5f * (left[i] - right[i]);

            const double v3 = side - ic2;
            const double v1 = a1 * ic1 + a2 * v3;
            const double v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2.0 * v1 - ic1;
            ic2 = 2.0 * v2 - ic2;
            const auto highPassed = static_cast<float> (side - k * v1 - v2);

            const float shapedSide = (side + (highPassed - side) * bass) * width;
            const float gain = outputCompensation (width);
            left[i] = (mid + shapedSide) * gain;
            right[i] = (mid - shapedSide) * gain;
        }
    }

private:
    double sampleRate = 48000.0;
    double g = 0.01, k = 1.414, ic1 = 0.0, ic2 = 0.0;
    float smoothing = 1.0f;
    float width = 1.0f, bass = 0.0f;
    bool initialised = false;
};

} // namespace polybrisk
