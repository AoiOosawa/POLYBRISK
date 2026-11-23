#pragma once

#include "fx/FxParams.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace polybrisk
{

/** REVERB: 8 本のディレイによる FDN (Householder 行列) + 入力拡散 (SPEC §8.6)。wet だけを出力する。
    Room / Plate / Hall はディレイ長・拡散・減衰特性・変調が異なる:
      Room  短いディレイ、弱めの拡散。初期反射が近く、低い密度で始まる
      Plate 中くらいのディレイ、強い拡散、明るい。立ち上がりが速く密
      Hall  長いディレイ、高域の減衰が大きい、ゆっくりした変調。遅く立ち上がり広がる
    どのタイプも、ディレイ長はほぼ等比に並べ (等間隔だと特定の周波数が強調される)、半分のラインを
    わずかに変調して余韻の金属的な響きを抑え、ループ内のローカットで低域の溜まりを防ぐ (D-045)。
    Type / Size の変更は、wet を短くフェードアウトしてから内部をクリアして切り替える。 */
class Reverb
{
public:
    static constexpr int numLines = 8;
    static constexpr int numDiffusers = 4;

    void prepare (double newSampleRate)
    {
        sampleRate = newSampleRate;

        lineSize = nextPowerOfTwo (static_cast<int> (sampleRate * 0.17) + 8);
        for (auto& l : lines)
            l.assign (static_cast<size_t> (lineSize), 0.0f);

        diffuserSize = nextPowerOfTwo (static_cast<int> (sampleRate * 0.02) + 8);
        for (auto& channel : diffusers)
            for (auto& d : channel)
                d.assign (static_cast<size_t> (diffuserSize), 0.0f);

        preDelaySize = nextPowerOfTwo (static_cast<int> (sampleRate * 0.21) + 8);
        for (auto& b : preDelay)
            b.assign (static_cast<size_t> (preDelaySize), 0.0f);

        fadeStep = static_cast<float> (1.0 / (0.02 * sampleRate));
        smoothing = static_cast<float> (1.0 - std::exp (-1.0 / (0.03 * sampleRate)));
        reset();
    }

    void reset() noexcept
    {
        clearBuffers();
        configured = false;
        fade = 1.0f;
        fadingOut = false;
    }

    void process (const float* inL, const float* inR, float* wetL, float* wetR, int numSamples, const ReverbParams& p) noexcept
    {
        if (lines[0].empty())
            return;

        const float size = std::clamp (p.size, 0.0f, 1.0f);

        if (! configured)
        {
            configure (p.type, size);
            preDelaySamples = targetPreDelay (p);
            configured = true;
        }
        else if (p.type != currentType || std::abs (size - currentSize) > 0.005f)
        {
            fadingOut = true;   // フェードアウトが終わった時点で切り替える
        }

        // 各ラインのフィードバックゲイン: 1 周で T60 に対応する分だけ減衰させる
        const double decay = std::clamp (static_cast<double> (p.decaySeconds), 0.2, 15.0);
        std::array<float, numLines> gain;
        for (int n = 0; n < numLines; ++n)
            gain[static_cast<size_t> (n)] = static_cast<float> (std::pow (10.0, -3.0 * lengths[static_cast<size_t> (n)] / (decay * sampleRate)));

        // Tone: フィードバック内のローパス (高域ほど速く減衰する)
        const double tone = std::clamp (static_cast<double> (p.tone), 0.0, 1.0);
        const double dampHz = dampBaseHz * std::pow (dampRange, tone);
        const float targetDamp = static_cast<float> (1.0 - std::exp (-2.0 * 3.141592653589793 * std::min (dampHz, 0.45 * sampleRate) / sampleRate));
        const float targetPre = targetPreDelay (p);

        const int lineMask = lineSize - 1;
        const int diffuserMask = diffuserSize - 1;
        const int preMask = preDelaySize - 1;

        for (int i = 0; i < numSamples; ++i)
        {
            damp += (targetDamp - damp) * smoothing;
            preDelaySamples += (targetPre - preDelaySamples) * smoothing * 0.05f;   // ゆっくり動かしてピッチの滑りを小さくする

            if (fadingOut)
            {
                fade -= fadeStep;

                if (fade <= 0.0f)
                {
                    fade = 0.0f;
                    fadingOut = false;
                    clearBuffers();
                    configure (p.type, size);
                }
            }
            else if (fade < 1.0f)
            {
                fade = std::min (1.0f, fade + fadeStep);
            }

            // Pre-delay
            preDelay[0][static_cast<size_t> (preIndex)] = inL[i];
            preDelay[1][static_cast<size_t> (preIndex)] = inR[i];
            const auto preRead = static_cast<size_t> ((preIndex - static_cast<int> (preDelaySamples)) & preMask);
            float x[2] = { preDelay[0][preRead], preDelay[1][preRead] };
            preIndex = (preIndex + 1) & preMask;

            // 入力拡散 (直列オールパス)
            for (int c = 0; c < 2; ++c)
            {
                for (int d = 0; d < numDiffusers; ++d)
                {
                    auto& b = diffusers[static_cast<size_t> (c)][static_cast<size_t> (d)];
                    const int length = diffuserLengths[static_cast<size_t> (c)][static_cast<size_t> (d)];
                    const float delayed = b[static_cast<size_t> ((diffuserIndex - length) & diffuserMask)];
                    const float v = x[c] - diffusion * delayed;
                    b[static_cast<size_t> (diffuserIndex)] = v;
                    x[c] = delayed + diffusion * v;
                }
            }
            diffuserIndex = (diffuserIndex + 1) & diffuserMask;

            // FDN: 読み出し → 減衰 → Householder → 入力を足して書き込み
            lfoPhase += lfoIncrement;
            if (lfoPhase >= 1.0)
                lfoPhase -= 1.0;

            std::array<float, numLines> out;
            float sum = 0.0f;

            for (int n = 0; n < numLines; ++n)
            {
                const auto idx = static_cast<size_t> (n);
                float delaySamples = static_cast<float> (lengths[idx]);

                // 偶数番のラインだけを変調する (位相は 90° ずつずらす)。残りは補間なしで高域を保つ
                if ((n & 1) == 0)
                    delaySamples += modulationSamples * static_cast<float> (std::sin (6.283185307179586 * (lfoPhase + 0.125 * n)));

                const float position = static_cast<float> (lineIndex) - delaySamples;
                const float floored = std::floor (position);
                const float f = position - floored;
                const int i0 = static_cast<int> (floored) & lineMask;
                const float raw = lines[idx][static_cast<size_t> (i0)] * (1.0f - f) + lines[idx][static_cast<size_t> ((i0 + 1) & lineMask)] * f;

                lowPass[idx] += (raw - lowPass[idx]) * damp;

                // ループ内のローカット: 低域ほど速く減衰させ、長い余韻で低域が溜まらないようにする (係数は configure で決める)
                lowState[idx] += (lowPass[idx] - lowState[idx]) * lowCut[idx];
                out[idx] = (lowPass[idx] - lowState[idx]) * gain[idx];
                sum += out[idx];
            }

            const float reflect = sum * (2.0f / numLines);

            for (int n = 0; n < numLines; ++n)
            {
                const auto idx = static_cast<size_t> (n);
                const float input = (n & 1) == 0 ? x[0] : x[1];
                float v = out[idx] - reflect + input * ((n & 2) == 0 ? 1.0f : -1.0f);

                if (! (std::abs (v) <= 8.0f))
                    v = std::isfinite (v) ? (v > 0.0f ? 8.0f : -8.0f) : 0.0f;

                lines[idx][static_cast<size_t> (lineIndex)] = v;
            }

            lineIndex = (lineIndex + 1) & lineMask;

            // 出力: ラインを符号つきで左右に振り分け、左右の相関を下げる
            const float l = out[0] - out[2] + out[4] - out[6];
            const float r = out[1] - out[3] + out[5] - out[7];
            wetL[i] = l * outputGain * fade;
            wetR[i] = r * outputGain * fade;
        }
    }

private:
    static int nextPowerOfTwo (int n) noexcept
    {
        int p = 1;
        while (p < n)
            p <<= 1;
        return p;
    }

    float targetPreDelay (const ReverbParams& p) const noexcept
    {
        return static_cast<float> (std::clamp (static_cast<double> (p.preDelaySeconds), 0.0, 0.2) * sampleRate);
    }

    void clearBuffers() noexcept
    {
        for (auto& l : lines)
            std::fill (l.begin(), l.end(), 0.0f);

        for (auto& channel : diffusers)
            for (auto& d : channel)
                std::fill (d.begin(), d.end(), 0.0f);

        for (auto& b : preDelay)
            std::fill (b.begin(), b.end(), 0.0f);

        lowPass.fill (0.0f);
        lowState.fill (0.0f);
    }

    void configure (ReverbType type, float size) noexcept
    {
        // ほぼ等比に並べた、互いに素に近い長さ (ms)。Size で 0.6〜1.4 倍に伸縮する。
        // 最短と最長は当初のまま (D-045 で中間を等間隔から等比に改めた)
        static constexpr double roomMs[numLines]  = { 11.3, 13.3, 15.7, 18.1, 21.3, 24.7, 28.9, 33.7 };
        static constexpr double plateMs[numLines] = { 19.1, 21.9, 25.1, 28.7, 32.9, 37.7, 43.3, 49.7 };
        static constexpr double hallMs[numLines]  = { 41.3, 47.1, 53.7, 61.1, 69.7, 79.3, 90.7, 103.1 };
        static constexpr double diffuserMs[2][numDiffusers] = { { 4.77, 3.59, 12.73, 9.31 }, { 5.03, 3.89, 11.87, 8.69 } };

        const double* base = type == ReverbType::room ? roomMs : (type == ReverbType::plate ? plateMs : hallMs);
        const double scale = 0.6 + 0.8 * static_cast<double> (size);

        for (int n = 0; n < numLines; ++n)
            lengths[static_cast<size_t> (n)] = std::clamp (base[n] * scale * 0.001 * sampleRate, 8.0, static_cast<double> (lineSize - 64));

        // ローカットの周波数はライン長の平方根に比例させる。1 周あたりの損失は周波数の 2 乗に比例し、
        // 1 秒あたりの周回数は長さに反比例するので、こうすると低域の減衰の速さがライン長によらずそろう
        // (50 ms のラインで 20 Hz。100 Hz で毎秒約 3.4 dB、200 Hz で約 0.9 dB の追加減衰)
        for (int n = 0; n < numLines; ++n)
        {
            const double lengthMs = lengths[static_cast<size_t> (n)] * 1000.0 / sampleRate;
            const double hz = 20.0 * std::sqrt (lengthMs / 50.0);
            lowCut[static_cast<size_t> (n)] = static_cast<float> (1.0 - std::exp (-2.0 * 3.141592653589793 * hz / sampleRate));
        }

        double diffuserScale = 1.0;

        switch (type)
        {
            case ReverbType::room:
                diffusion = 0.45f; diffuserScale = 0.6; dampBaseHz = 1500.0; dampRange = 8.0;
                modulationSamples = static_cast<float> (0.00015 * sampleRate); outputGain = 0.40f;
                break;
            case ReverbType::plate:
                diffusion = 0.72f; diffuserScale = 1.0; dampBaseHz = 3000.0; dampRange = 6.0;
                modulationSamples = static_cast<float> (0.00025 * sampleRate); outputGain = 0.36f;
                break;
            case ReverbType::hall:
            default:
                diffusion = 0.62f; diffuserScale = 1.5; dampBaseHz = 900.0; dampRange = 9.0;
                modulationSamples = static_cast<float> (0.0006 * sampleRate); outputGain = 0.34f;
                break;
        }

        for (int c = 0; c < 2; ++c)
            for (int d = 0; d < numDiffusers; ++d)
                diffuserLengths[static_cast<size_t> (c)][static_cast<size_t> (d)] =
                    std::clamp (static_cast<int> (diffuserMs[c][d] * diffuserScale * 0.001 * sampleRate), 1, diffuserSize - 2);

        lfoIncrement = 0.37 / sampleRate;
        currentType = type;
        currentSize = size;
    }

    std::array<std::vector<float>, numLines> lines;
    std::array<std::array<std::vector<float>, numDiffusers>, 2> diffusers;
    std::vector<float> preDelay[2];
    std::array<double, numLines> lengths {};
    std::array<std::array<int, numDiffusers>, 2> diffuserLengths {};
    std::array<float, numLines> lowPass {};
    std::array<float, numLines> lowState {};
    std::array<float, numLines> lowCut {};

    int lineSize = 0, diffuserSize = 0, preDelaySize = 0;
    int lineIndex = 0, diffuserIndex = 0, preIndex = 0;
    double sampleRate = 48000.0;
    double dampBaseHz = 3000.0, dampRange = 6.0;
    double lfoPhase = 0.0, lfoIncrement = 0.0;
    float diffusion = 0.6f, modulationSamples = 0.0f, outputGain = 0.35f;
    float damp = 1.0f, preDelaySamples = 0.0f, smoothing = 1.0f;
    float fade = 1.0f, fadeStep = 0.001f;
    bool fadingOut = false;
    bool configured = false;
    ReverbType currentType = ReverbType::plate;
    float currentSize = 0.5f;
};

} // namespace polybrisk
