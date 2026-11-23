#pragma once

#include "fx/FxParams.h"

#include <algorithm>
#include <cmath>

namespace polybrisk
{

/** 3 バンド EQ (SPEC §8.3): 100 Hz ローシェルフ、200 Hz〜8 kHz のベル (Q = 1)、8 kHz ハイシェルフ。
    係数は 32 サンプルごとに、スムージングした設定値から計算し直す。 */
class Equalizer
{
public:
    static constexpr double lowShelfHz = 100.0;
    static constexpr double highShelfHz = 8000.0;
    static constexpr double midQ = 1.0;

    void prepare (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate;
        smoothing = static_cast<float> (1.0 - std::exp (-32.0 / (0.02 * sampleRate)));
        reset();
    }

    void reset() noexcept
    {
        for (auto& band : state)
            for (auto& ch : band)
                ch = {};

        initialised = false;
        untilUpdate = 0;
    }

    void process (float* left, float* right, int numSamples, const EqParams& p) noexcept
    {
        const float targetLow = std::clamp (p.lowGainDb, -18.0f, 18.0f);
        const float targetMid = std::clamp (p.midGainDb, -18.0f, 18.0f);
        const float targetHigh = std::clamp (p.highGainDb, -18.0f, 18.0f);
        const float targetFreq = std::log2 (std::clamp (p.midFreqHz, 200.0f, 8000.0f));

        if (! initialised)
        {
            low = targetLow;
            mid = targetMid;
            high = targetHigh;
            freq = targetFreq;
            initialised = true;
            untilUpdate = 0;
        }

        float* const io[2] = { left, right };

        for (int i = 0; i < numSamples; ++i)
        {
            if (untilUpdate <= 0)
            {
                low += (targetLow - low) * smoothing;
                mid += (targetMid - mid) * smoothing;
                high += (targetHigh - high) * smoothing;
                freq += (targetFreq - freq) * smoothing;

                coeffs[0] = shelf (lowShelfHz, low, false);
                coeffs[1] = peak (std::exp2 (static_cast<double> (freq)), mid);
                coeffs[2] = shelf (std::min (highShelfHz, 0.4 * sampleRate), high, true);
                untilUpdate = 32;
            }

            --untilUpdate;

            for (int c = 0; c < 2; ++c)
            {
                double x = io[c][i];

                for (int b = 0; b < 3; ++b)
                {
                    auto& s = state[b][c];
                    const auto& k = coeffs[b];
                    const double y = k.b0 * x + s.z1;
                    s.z1 = k.b1 * x - k.a1 * y + s.z2;
                    s.z2 = k.b2 * x - k.a2 * y;
                    x = y;
                }

                io[c][i] = static_cast<float> (x);
            }
        }
    }

private:
    struct Coeffs { double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0; };
    struct State { double z1 = 0.0, z2 = 0.0; };

    // RBJ Audio EQ Cookbook
    Coeffs peak (double frequency, double gainDb) const noexcept
    {
        const double a = std::pow (10.0, gainDb / 40.0);
        const double w = 6.283185307179586 * std::min (frequency, 0.45 * sampleRate) / sampleRate;
        const double alpha = std::sin (w) / (2.0 * midQ);
        const double a0 = 1.0 + alpha / a;
        return { (1.0 + alpha * a) / a0, -2.0 * std::cos (w) / a0, (1.0 - alpha * a) / a0,
                 -2.0 * std::cos (w) / a0, (1.0 - alpha / a) / a0 };
    }

    Coeffs shelf (double frequency, double gainDb, bool highShelf) const noexcept
    {
        const double a = std::pow (10.0, gainDb / 40.0);
        const double w = 6.283185307179586 * frequency / sampleRate;
        const double cosw = std::cos (w);
        const double alpha = std::sin (w) / 2.0 * std::sqrt (2.0);   // S = 1
        const double beta = 2.0 * std::sqrt (a) * alpha;

        if (highShelf)
        {
            const double a0 = (a + 1.0) - (a - 1.0) * cosw + beta;
            return { a * ((a + 1.0) + (a - 1.0) * cosw + beta) / a0,
                     -2.0 * a * ((a - 1.0) + (a + 1.0) * cosw) / a0,
                     a * ((a + 1.0) + (a - 1.0) * cosw - beta) / a0,
                     2.0 * ((a - 1.0) - (a + 1.0) * cosw) / a0,
                     ((a + 1.0) - (a - 1.0) * cosw - beta) / a0 };
        }

        const double a0 = (a + 1.0) + (a - 1.0) * cosw + beta;
        return { a * ((a + 1.0) - (a - 1.0) * cosw + beta) / a0,
                 2.0 * a * ((a - 1.0) - (a + 1.0) * cosw) / a0,
                 a * ((a + 1.0) - (a - 1.0) * cosw - beta) / a0,
                 -2.0 * ((a - 1.0) + (a + 1.0) * cosw) / a0,
                 ((a + 1.0) + (a - 1.0) * cosw - beta) / a0 };
    }

    double sampleRate = 48000.0;
    float smoothing = 1.0f;
    float low = 0.0f, mid = 0.0f, high = 0.0f, freq = 10.0f;
    Coeffs coeffs[3];
    State state[3][2];
    int untilUpdate = 0;
    bool initialised = false;
};

} // namespace polybrisk
