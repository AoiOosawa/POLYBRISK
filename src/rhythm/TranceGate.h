#pragma once

#include "rhythm/GatePattern.h"
#include "rhythm/RhythmClock.h"

#include <algorithm>
#include <cmath>

namespace polybrisk
{

struct GateParams
{
    bool enabled = false;
    int steps = 16;                 // 8 / 16 / 32
    double rateBeats = 0.25;        // 1 ステップの長さ (拍)。1/16 = 0.25
    float depth = 1.0f;             // 0..1
    float smoothSeconds = 0.003f;   // 0.0005..0.03
    float swing = 0.0f;             // 0..0.5
    bool restartOnNote = false;     // false = Transport、true = Note On
    bool stereo = false;            // true なら左右で別のパターンを使う
    float width = 1.0f;             // 0..1。Stereo のときの左右の分離 (0 = 左右同じ、1 = 完全に別)
    GatePattern pattern;
};

/** Trance Gate (追加仕様 §3)。音声には触れず、サンプルごとの左右のゲインだけを計算する。
    Stereo が OFF なら左右は同じゲインで、定位は動かない。ON なら pattern.values が左、pattern.right が右。
    ON/OFF は 10 ms のクロスフェードで 1 倍との間を行き来する。 */
class TranceGate
{
public:
    void prepare (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
        enableStep = static_cast<float> (1.0 / (0.01 * sampleRate));
        reset();
    }

    void reset() noexcept
    {
        gain[0] = gain[1] = 1.0f;
        enableGain = 0.0f;
        currentStep = -1;
        primed = false;
    }

    /** ステップ値とDepth から目標ゲインを求める: 1 - d * (1 - s)。 */
    static float targetGain (float stepValue, float depth) noexcept
    {
        return 1.0f - std::clamp (depth, 0.0f, 1.0f) * (1.0f - std::clamp (stepValue, 0.0f, 1.0f));
    }

    /** あるステップの左右のステップ値。Width を下げると、左右とも「どちらかが開いていれば開く」形 (左右の大きい方) に近づく。
        Width 0 でもリズムは残り、定位だけが中央に寄る。 */
    static void stepValues (const GateParams& p, int step, float& left, float& right) noexcept
    {
        left = p.pattern.values[static_cast<std::size_t> (step)];
        right = left;

        if (p.stereo)
        {
            right = p.pattern.right[static_cast<std::size_t> (step)];
            const float both = std::max (left, right);
            const float width = std::clamp (p.width, 0.0f, 1.0f);
            left = both + (left - both) * width;
            right = both + (right - both) * width;
        }
    }

    /** Gate が OFF で process を呼ばないブロックで呼ぶ。「リセット直後」の扱いを終わらせ、
        このあと ON にしたときに、パターンの状態へ跳ばずにフェードで入るようにする。 */
    void idle() noexcept { primed = true; }

    /** beats[i] (起点からの拍) に対するゲインを left[i] / right[i] に書く。すべて 1 (処理不要) なら false を返す。 */
    bool process (float* left, float* right, const double* beats, int numSamples, const GateParams& p) noexcept
    {
        const int steps = p.steps <= 8 ? 8 : (p.steps <= 16 ? 16 : 32);
        const double rate = std::clamp (p.rateBeats, 1.0 / 64.0, 4.0);
        const auto wrap = [steps] (long long index) { return static_cast<int> (((index % steps) + steps) % steps); };

        // リセット後の最初の処理: 最初から ON なら、フェードインせずにパターンの状態から始める
        if (! primed)
        {
            primed = true;

            if (p.enabled && numSamples > 0)
            {
                float l = 1.0f, r = 1.0f;
                stepValues (p, wrap (rhythm::locateStep (beats[0], rate, p.swing).index), l, r);
                gain[0] = targetGain (l, p.depth);
                gain[1] = targetGain (r, p.depth);
                enableGain = 1.0f;
            }
        }

        if (! p.enabled && enableGain <= 0.0f)
        {
            gain[0] = gain[1] = 1.0f;
            currentStep = -1;
            return false;
        }

        // Smooth: 指定時間でおおむね遷移し終える 1 次スムージング (時定数 = Smooth / 3)
        const double smooth = std::clamp (static_cast<double> (p.smoothSeconds), 0.0005, 0.03);
        const float coeff = static_cast<float> (1.0 - std::exp (-3.0 / (smooth * sampleRate)));

        int lastStep = -1;
        float targetLeft = 1.0f, targetRight = 1.0f;

        for (int i = 0; i < numSamples; ++i)
        {
            const int step = wrap (rhythm::locateStep (beats[i], rate, p.swing).index);

            if (step != lastStep)
            {
                lastStep = step;
                float l = 1.0f, r = 1.0f;
                stepValues (p, step, l, r);
                targetLeft = targetGain (l, p.depth);
                targetRight = targetGain (r, p.depth);
            }

            currentStep = step;
            gain[0] += (targetLeft - gain[0]) * coeff;
            gain[1] += (targetRight - gain[1]) * coeff;
            enableGain = std::clamp (enableGain + (p.enabled ? enableStep : -enableStep), 0.0f, 1.0f);
            left[i] = 1.0f + (gain[0] - 1.0f) * enableGain;
            right[i] = 1.0f + (gain[1] - 1.0f) * enableGain;
        }

        return true;
    }

    /** いま再生しているステップ (UI のインジケーター用)。動作していなければ -1。 */
    int getCurrentStep() const noexcept { return currentStep; }

private:
    double sampleRate = 44100.0;
    float gain[2] = { 1.0f, 1.0f };
    float enableGain = 0.0f;
    float enableStep = 0.01f;
    int currentStep = -1;
    bool primed = false;
};

} // namespace polybrisk
