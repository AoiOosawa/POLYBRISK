#pragma once

#include "fx/FxParams.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace polybrisk
{

/** CHORUS: モジュレーテッド・ショートディレイ (SPEC §8.2)。
    Classic は左右 1 タップずつ (LFO 位相 90° 差)、Wide は左右 3 タップずつ (120° 間隔、左右で逆相)。
    読み出しは 3 次エルミート補間で、Rate / Depth の変更はスムージングする。 */
class Chorus
{
public:
    void prepare (double newSampleRate)
    {
        sampleRate = newSampleRate;
        size = 1;
        while (size < static_cast<int> (sampleRate * 0.05) + 8)
            size <<= 1;

        for (auto& b : buffer)
            b.assign (static_cast<size_t> (size), 0.0f);

        smoothing = static_cast<float> (1.0 - std::exp (-1.0 / (0.02 * sampleRate)));
        primeStep = static_cast<float> (1.0 / (0.01 * sampleRate));
        reset();
    }

    void reset() noexcept
    {
        for (auto& b : buffer)
            std::fill (b.begin(), b.end(), 0.0f);

        writeIndex = 0;
        phase = 0.0;
        prime = 0.0f;
        initialised = false;
    }

    void process (float* left, float* right, int numSamples, const ChorusParams& p) noexcept
    {
        if (buffer[0].empty())
            return;

        const float targetDepth = std::clamp (p.depth, 0.0f, 1.0f);
        const float targetMix = std::clamp (p.mix, 0.0f, 1.0f);
        const float targetWidth = std::clamp (p.width, 0.0f, 1.0f);
        const float targetRate = std::clamp (p.rateHz, 0.05f, 8.0f);

        if (! initialised)
        {
            depth = targetDepth;
            mix = targetMix;
            width = targetWidth;
            rate = targetRate;
            initialised = true;
        }

        const bool wide = p.type == ChorusType::wide;
        const int taps = wide ? 3 : 1;
        const int mask = size - 1;
        constexpr double twoPi = 6.283185307179586;

        for (int i = 0; i < numSamples; ++i)
        {
            depth += (targetDepth - depth) * smoothing;
            mix += (targetMix - mix) * smoothing;
            width += (targetWidth - width) * smoothing;
            rate += (targetRate - rate) * smoothing;

            // リセット直後はディレイラインへの入力をフェードインする (途中から始まる波形の段差を作らない)
            prime = std::min (1.0f, prime + primeStep);
            buffer[0][static_cast<size_t> (writeIndex)] = left[i] * prime;
            buffer[1][static_cast<size_t> (writeIndex)] = right[i] * prime;

            // 速い Rate では変調幅を狭め、ピッチの揺れが過大にならないようにする
            const double sweep = depth * (wide ? 0.004 : 0.005) / (1.0 + 0.35 * rate);
            float wet[2] = { 0.0f, 0.0f };

            for (int c = 0; c < 2; ++c)
            {
                for (int t = 0; t < taps; ++t)
                {
                    const double base = wide ? 0.008 + 0.006 * t : 0.012;
                    const double offset = wide ? t / 3.0 + (c == 1 ? 0.5 : 0.0) : (c == 1 ? 0.25 : 0.0);
                    const double lfo = std::sin (twoPi * (phase + offset));
                    wet[c] += read (buffer[c], (base + sweep * lfo) * sampleRate, mask);
                }

                wet[c] *= wide ? 0.57735027f : 1.0f;
            }

            // Width: 0 で wet をモノラルにする
            const float mid = 0.5f * (wet[0] + wet[1]);
            const float wetL = mid + (wet[0] - mid) * width;
            const float wetR = mid + (wet[1] - mid) * width;

            // 等パワーに近いミックス (50% で dry / wet が同量)
            const float dryGain = std::cos (1.5707963f * mix);
            const float wetGain = std::sin (1.5707963f * mix);
            left[i] = left[i] * dryGain + wetL * wetGain;
            right[i] = right[i] * dryGain + wetR * wetGain;

            writeIndex = (writeIndex + 1) & mask;
            phase += rate / sampleRate;
            if (phase >= 1.0)
                phase -= 1.0;
        }
    }

private:
    float read (const std::vector<float>& b, double delaySamples, int mask) const noexcept
    {
        const double position = static_cast<double> (writeIndex) - std::max (2.0, delaySamples);
        const double floored = std::floor (position);
        const auto f = static_cast<float> (position - floored);
        const int i1 = static_cast<int> (floored) & mask;
        const float y0 = b[static_cast<size_t> ((i1 - 1) & mask)];
        const float y1 = b[static_cast<size_t> (i1)];
        const float y2 = b[static_cast<size_t> ((i1 + 1) & mask)];
        const float y3 = b[static_cast<size_t> ((i1 + 2) & mask)];

        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * f + c2) * f + c1) * f + y1;
    }

    std::vector<float> buffer[2];
    int size = 0;
    int writeIndex = 0;
    double sampleRate = 48000.0;
    double phase = 0.0;
    float smoothing = 1.0f;
    float depth = 0.5f, mix = 0.5f, width = 1.0f, rate = 0.8f;
    float prime = 0.0f, primeStep = 0.01f;
    bool initialised = false;
};

} // namespace polybrisk
