#pragma once

#include "fx/FxParams.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace polybrisk
{

/** DELAY: Stereo / Ping Pong (SPEC §8.5)。wet だけを出力する (dry とのミックスは FxChain)。
    - ディレイタイムの変更は 2 つの読み出し位置のクロスフェードで行う (クリックもピッチの滑りも出さない)。
    - フィードバック経路にローパス (Tone) と 80 Hz のハイパスを置き、ループゲインは常に 1 未満。
    - Ducking: 入力が鳴っている間は wet を下げる。 */
class Delay
{
public:
    static constexpr double maxSeconds = 2.5;
    static constexpr double fallbackBpm = 120.0;

    /** 設定とテンポから、実際のディレイタイム (秒) を求める。テンポが不正なら 120 BPM。 */
    static double timeSeconds (const DelayParams& p, double bpm) noexcept
    {
        if (! p.sync)
            return std::clamp (static_cast<double> (p.timeSeconds), 0.001, 2.0);

        const double tempo = (std::isfinite (bpm) && bpm >= 20.0 && bpm <= 999.0) ? bpm : fallbackBpm;
        return std::clamp (delayDivisionBeats (p.division) * 60.0 / tempo, 0.001, maxSeconds - 0.01);
    }

    void prepare (double newSampleRate)
    {
        sampleRate = newSampleRate;
        size = 1;
        while (size < static_cast<int> (sampleRate * maxSeconds) + 8)
            size <<= 1;

        for (auto& b : buffer)
            b.assign (static_cast<size_t> (size), 0.0f);

        smoothing = static_cast<float> (1.0 - std::exp (-1.0 / (0.02 * sampleRate)));
        fadeStep = static_cast<float> (1.0 / (0.04 * sampleRate));
        highPassCoeff = static_cast<float> (1.0 - std::exp (-2.0 * 3.141592653589793 * 80.0 / sampleRate));
        duckAttack = static_cast<float> (1.0 - std::exp (-1.0 / (0.005 * sampleRate)));
        duckRelease = static_cast<float> (1.0 - std::exp (-1.0 / (0.25 * sampleRate)));
        reset();
    }

    void reset() noexcept
    {
        for (auto& b : buffer)
            std::fill (b.begin(), b.end(), 0.0f);

        writeIndex = 0;
        fade = 1.0f;
        initialised = false;
        envelope = 0.0f;

        for (auto& f : filters)
            f = {};
    }

    /** inL / inR を入力として、wetL / wetR に wet 成分を書き出す。 */
    void process (const float* inL, const float* inR, float* wetL, float* wetR, int numSamples,
                  const DelayParams& p, double bpm) noexcept
    {
        if (buffer[0].empty())
            return;

        const float targetDelay = static_cast<float> (std::clamp (timeSeconds (p, bpm) * sampleRate, 1.0, static_cast<double> (size - 4)));
        const float targetFeedback = std::clamp (p.feedback, 0.0f, 0.95f);
        const float targetDuck = std::clamp (p.ducking, 0.0f, 1.0f);
        const double toneHz = 600.0 * std::pow (30.0, static_cast<double> (std::clamp (p.tone, 0.0f, 1.0f)));
        const float targetTone = static_cast<float> (1.0 - std::exp (-2.0 * 3.141592653589793 * std::min (toneHz, 0.45 * sampleRate) / sampleRate));

        if (! initialised)
        {
            delayCurrent = delayNext = targetDelay;
            fade = 1.0f;
            feedback = targetFeedback;
            duck = targetDuck;
            tone = targetTone;
            initialised = true;
        }

        const bool pingPong = p.mode == DelayMode::pingPong;
        const int mask = size - 1;

        for (int i = 0; i < numSamples; ++i)
        {
            feedback += (targetFeedback - feedback) * smoothing;
            duck += (targetDuck - duck) * smoothing;
            tone += (targetTone - tone) * smoothing;

            // ディレイタイムが変わったら、新しい読み出し位置へクロスフェードする
            if (fade >= 1.0f && std::abs (targetDelay - delayNext) > 0.5f)
            {
                delayCurrent = delayNext;
                delayNext = targetDelay;
                fade = 0.0f;
            }

            float tap[2];
            for (int c = 0; c < 2; ++c)
            {
                const float next = read (buffer[c], delayNext, mask);
                tap[c] = fade >= 1.0f ? next : read (buffer[c], delayCurrent, mask) * (1.0f - fade) + next * fade;
            }

            if (fade < 1.0f)
                fade = std::min (1.0f, fade + fadeStep);

            // フィードバック経路のフィルター (繰り返すごとに高域と超低域が減る)
            float filtered[2];
            for (int c = 0; c < 2; ++c)
            {
                auto& f = filters[c];
                f.low += (tap[c] - f.low) * tone;
                f.high += (f.low - f.high) * highPassCoeff;
                filtered[c] = f.low - f.high;
            }

            float writeL, writeR;

            if (pingPong)
            {
                // 入力はモノラルで左へ。左 → 右 → 左 … と跳ね返る
                const float g = std::sqrt (feedback);
                writeL = 0.5f * (inL[i] + inR[i]) + filtered[1] * g;
                writeR = filtered[0] * g;
            }
            else
            {
                writeL = inL[i] + filtered[0] * feedback;
                writeR = inR[i] + filtered[1] * feedback;
            }

            buffer[0][static_cast<size_t> (writeIndex)] = limit (writeL);
            buffer[1][static_cast<size_t> (writeIndex)] = limit (writeR);
            writeIndex = (writeIndex + 1) & mask;

            // Ducking: 入力のピークを追い、鳴っている間は wet を下げる
            const float level = std::max (std::abs (inL[i]), std::abs (inR[i]));
            envelope += (level - envelope) * (level > envelope ? duckAttack : duckRelease);
            const float duckGain = 1.0f - duck * std::min (1.0f, envelope * 4.0f);

            wetL[i] = filtered[0] * duckGain;
            wetR[i] = filtered[1] * duckGain;
        }
    }

private:
    struct Filter { float low = 0.0f, high = 0.0f; };

    // 万一の過大入力でもループ内の値が際限なく大きくならないようにする (±4 以内は線形)
    static float limit (float x) noexcept
    {
        if (! (std::abs (x) <= 4.0f))
            return std::isfinite (x) ? (x > 0.0f ? 4.0f : -4.0f) : 0.0f;

        return x;
    }

    float read (const std::vector<float>& b, float delaySamples, int mask) const noexcept
    {
        const float position = static_cast<float> (writeIndex) - delaySamples;
        const float floored = std::floor (position);
        const float f = position - floored;
        const int i0 = static_cast<int> (floored) & mask;
        return b[static_cast<size_t> (i0)] * (1.0f - f) + b[static_cast<size_t> ((i0 + 1) & mask)] * f;
    }

    std::vector<float> buffer[2];
    Filter filters[2];
    int size = 0;
    int writeIndex = 0;
    double sampleRate = 48000.0;
    float smoothing = 1.0f, fadeStep = 0.001f, highPassCoeff = 0.01f, duckAttack = 0.1f, duckRelease = 0.001f;
    float delayCurrent = 1.0f, delayNext = 1.0f, fade = 1.0f;
    float feedback = 0.0f, duck = 0.0f, tone = 1.0f, envelope = 0.0f;
    bool initialised = false;
};

} // namespace polybrisk
