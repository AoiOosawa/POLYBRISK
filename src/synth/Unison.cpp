#include "synth/Unison.h"

#include <algorithm>
#include <cmath>

namespace polybrisk
{

namespace
{
constexpr double goldenRatioFraction = 0.6180339887498949;
constexpr double maxIncrement = 0.45; // Nyquist の安全域
constexpr float centreGain = 0.70710678f;

inline std::uint32_t nextRandom (std::uint32_t& state) noexcept
{
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

inline float randomBipolar (std::uint32_t& state) noexcept
{
    // 上位 24bit を -1..+1 に写像
    return static_cast<float> (nextRandom (state) >> 8) * (2.0f / 16777216.0f) - 1.0f;
}

template <typename Generator>
inline void renderVoice (float* left, float* right, int numSamples,
                         double& phaseRef, double increment, float gainL, float gainR,
                         Generator&& generate) noexcept
{
    double p = phaseRef;

    for (int i = 0; i < numSamples; ++i)
    {
        const auto s = static_cast<float> (generate (p, increment));
        left[i] += s * gainL;
        right[i] += s * gainR;

        p += increment;
        if (p >= 1.0)
            p -= 1.0;
    }

    phaseRef = p;
}
} // namespace

double detuneAmountToCents (float detune01) noexcept
{
    // 小さい値で細かく、大きい値で広く効くカーブ。100% で ±100 cents、既定 35% で約 ±18 cents。
    const double d = std::clamp (static_cast<double> (detune01), 0.0, 1.0);
    return 100.0 * (0.25 * d + 0.75 * d * d);
}

void computeUnisonLayout (UnisonLayout& layout, int numVoices, float detune01, float spread01,
                          float mix01, bool asymmetric) noexcept
{
    const int n = std::clamp (numVoices, 1, maxUnisonVoices);
    const double maxCents = detuneAmountToCents (detune01);
    const double spread = std::clamp (static_cast<double> (spread01), 0.0, 1.0);
    const int pairs = n / 2;

    // Mix: 中央の声と外側の声の重み。0.5 で両方 1 (全声が同じ音量)。合計のパワーが 1 になるよう正規化する。
    const bool odd = (n & 1) != 0;
    const int centreCount = odd ? 1 : 2;                // 偶数本では最も内側の対が中央の役
    const int sideCount = n - centreCount;
    float centreWeight = 1.0f, sideWeight = 1.0f;

    if (sideCount > 0)
    {
        const double angle = std::clamp (static_cast<double> (mix01), 0.0, 1.0) * (osc::twoPi / 4.0);
        centreWeight = static_cast<float> (std::sqrt (2.0) * std::cos (angle));
        sideWeight = static_cast<float> (std::sqrt (2.0) * std::sin (angle));
    }

    const float normalisation = 1.0f / std::sqrt (static_cast<float> (centreCount) * centreWeight * centreWeight
                                                  + static_cast<float> (sideCount) * sideWeight * sideWeight);

    // 非対称デチューン: 対ごとの上下の倍率。平均はほぼ 1 で、どの対も上下が同じ量にならない。
    static constexpr double lowFactor[4] = { 0.93, 1.04, 0.96, 0.98 };
    static constexpr double highFactor[4] = { 1.02, 0.95, 1.03, 1.00 };

    layout.numVoices = n;

    const auto setVoice = [&] (int index, double centsValue, double panValue, float weight)
    {
        const auto i = static_cast<std::size_t> (index);
        layout.cents[i] = centsValue;
        layout.ratio[i] = std::exp2 (centsValue / 1200.0);
        layout.pan[i] = static_cast<float> (panValue);

        // 等パワーパン。Spread 0 でも全声がセンターに残る。
        const double angle = (panValue + 1.0) * (osc::twoPi / 8.0);
        layout.gainL[i] = static_cast<float> (std::cos (angle)) * normalisation * weight;
        layout.gainR[i] = static_cast<float> (std::sin (angle)) * normalisation * weight;
    };

    int index = 0;

    if (odd)
        setVoice (index++, 0.0, 0.0, centreWeight);

    for (int k = 1; k <= pairs; ++k)
    {
        const double position = static_cast<double> (k) / static_cast<double> (pairs);

        // 等間隔を避けて外側ほど広げ、うなりの周期がそろうのを防ぐ
        const double magnitude = maxCents * std::pow (position, 1.5);

        // 低い側・高い側を対にして左右へ振り分け、対ごとに左右を入れ替える
        const double side = ((k & 1) != 0) ? -1.0 : 1.0;
        const double panValue = spread * position;

        const float weight = (! odd && k == 1) ? centreWeight : sideWeight;
        const auto factorIndex = static_cast<std::size_t> ((k - 1) & 3);
        const double low = asymmetric ? magnitude * lowFactor[factorIndex] : magnitude;
        const double high = asymmetric ? magnitude * highFactor[factorIndex] : magnitude;

        setVoice (index++, -low, side * panValue, weight);
        setVoice (index++, high, -side * panValue, weight);
    }

    for (int i = index; i < maxUnisonVoices; ++i)
    {
        const auto u = static_cast<std::size_t> (i);
        layout.cents[u] = 0.0;
        layout.ratio[u] = 1.0;
        layout.pan[u] = 0.0f;
        layout.gainL[u] = 0.0f;
        layout.gainR[u] = 0.0f;
    }
}

void UnisonOscillator::seed (std::uint32_t seedValue) noexcept
{
    noiseState = seedValue != 0 ? seedValue : 0x9e3779b9u;

    // 系列を少し進めてシード間の相関を減らす
    for (int i = 0; i < 8; ++i)
        nextRandom (noiseState);

    for (auto& p : phase)
        p = static_cast<double> (nextRandom (noiseState) >> 8) / 16777216.0;
}

void UnisonOscillator::retrigger() noexcept
{
    // 声 0 は位相 0。他の声は黄金比で散らした固定位相 (全声同位相による過大なピークを避ける)。
    for (std::size_t i = 0; i < phase.size(); ++i)
    {
        const double p = static_cast<double> (i) * goldenRatioFraction;
        phase[i] = p - std::floor (p);
    }
}

void UnisonOscillator::render (float* left, float* right, int numSamples,
                               Waveform waveform, double baseIncrement,
                               const UnisonLayout& layout, double pulseWidth) noexcept
{
    if (waveform == Waveform::noise)
    {
        // ノイズは音程・ユニゾンに追従しない (SPEC §4.3)。ボイスごとに独立した系列。
        std::uint32_t state = noiseState;

        for (int i = 0; i < numSamples; ++i)
        {
            const float s = randomBipolar (state) * centreGain;
            left[i] += s;
            right[i] += s;
        }

        noiseState = state;
        return;
    }

    const double width = waveform == Waveform::square ? 0.5 : std::clamp (pulseWidth, 0.05, 0.95);

    for (int v = 0; v < layout.numVoices; ++v)
    {
        const auto u = static_cast<std::size_t> (v);
        const double increment = std::clamp (baseIncrement * layout.ratio[u], 0.0, maxIncrement);
        const float gl = layout.gainL[u];
        const float gr = layout.gainR[u];

        switch (waveform)
        {
            case Waveform::sine:
                renderVoice (left, right, numSamples, phase[u], increment, gl, gr,
                             [] (double p, double) { return osc::sine (p); });
                break;

            case Waveform::triangle:
                renderVoice (left, right, numSamples, phase[u], increment, gl, gr,
                             [] (double p, double inc) { return osc::triangle (p, inc); });
                break;

            case Waveform::pulse:
            case Waveform::square:
                renderVoice (left, right, numSamples, phase[u], increment, gl, gr,
                             [width] (double p, double inc) { return osc::pulse (p, inc, width); });
                break;

            case Waveform::saw:
            case Waveform::superSaw:
            case Waveform::noise:
            default:
                renderVoice (left, right, numSamples, phase[u], increment, gl, gr,
                             [] (double p, double inc) { return osc::saw (p, inc); });
                break;
        }
    }
}

} // namespace polybrisk
