#include "synth/Filters.h"

namespace polybrisk
{

namespace
{
// レゾナンス 0 のときの Q。12 dB は Butterworth、24 dB は 4 次 Butterworth の 2 段。
constexpr float butterworthQ = 0.70710678f;
constexpr float butterworthQ1 = 0.54119610f;
constexpr float butterworthQ2 = 1.30656296f;

constexpr float maxDriveGain = 15.848932f; // +24 dB

struct CharacterTuning
{
    float maxQ;             // レゾナンス 100% の Q
    float resonanceComp;    // レゾナンスに対する音量補正の強さ (0 = 補正なし、1 = ピークを完全に打ち消す)
};

constexpr CharacterTuning tuningFor (FilterCharacter character) noexcept
{
    switch (character)
    {
        case FilterCharacter::analog: return { 10.0f, 0.30f };
        case FilterCharacter::punch:  return { 8.0f, 0.55f };
        case FilterCharacter::clean:
        default:                      return { 14.0f, 0.20f };
    }
}

enum class Tap { low, band, high };

/** TPT SVF の係数 (Simper 形式)。g と k だけで決まるので、左右のチャンネルで共有する。 */
struct Coefficients
{
    float k, a1, a2, a3;

    Coefficients (float g, float damping) noexcept
        : k (damping), a1 (1.0f / (1.0f + g * (g + damping))), a2 (g * a1), a3 (g * a2) {}
};

/** TPT SVF の 1 サンプル。積分器の状態を更新して指定のタップを返す。 */
template <bool saturateState>
inline float tick (float input, float& ic1, float& ic2, const Coefficients& c, Tap tap, float stateScale, float stateInverse) noexcept
{
    const float v3 = input - ic2;
    const float v1 = c.a1 * ic1 + c.a2 * v3;
    const float v2 = ic2 + c.a2 * ic1 + c.a3 * v3;

    ic1 = 2.0f * v1 - ic1;
    ic2 = 2.0f * v2 - ic2;

    if constexpr (saturateState)
    {
        // 帯域通過側の積分器だけを飽和させる。エネルギーを減らす方向にしか働かないので安定性を損なわない。
        ic1 = stateScale * filter::fastTanh (ic1 * stateInverse);
    }

    switch (tap)
    {
        case Tap::low:  return v2;
        case Tap::band: return v1;
        case Tap::high:
        default:        return input - c.k * v1 - v2;
    }
}

template <bool saturateState>
void processStereo (float* left, float* right, int numSamples, float (&state)[2][4], const FilterSettings& settings,
                    float gStart, float gStep) noexcept
{
    const bool twoStages = settings.mode == FilterMode::lp24 || settings.mode == FilterMode::hp24;
    const Tap tap = (settings.mode == FilterMode::lp12 || settings.mode == FilterMode::lp24) ? Tap::low
                  : (settings.mode == FilterMode::bp12 ? Tap::band : Tap::high);

    const float k1 = twoStages ? settings.k1 : settings.k2;
    const float k2 = settings.k2;
    const float inScale = settings.inputScale;
    const float inInverse = 1.0f / inScale;
    const float outScale = settings.outputScale;
    const float outInverse = 1.0f / outScale;
    const float stateScale = settings.stateScale;
    const float stateInverse = 1.0f / stateScale;
    const bool clipOutput = settings.character == FilterCharacter::punch;
    float* const channels[2] = { left, right };

    float g = gStart;

    for (int i = 0; i < numSamples; ++i)
    {
        const Coefficients first (g, k1);
        const Coefficients second (g, k2);

        for (int ch = 0; ch < 2; ++ch)
        {
            float* s = state[ch];
            float x = channels[ch][i] * settings.preGain;

            if (settings.shapeInput)
                x = inScale * filter::fastTanh (x * inInverse);

            float y = tick<saturateState> (x, s[0], s[1], first, tap, stateScale, stateInverse);

            if (twoStages)
                y = tick<saturateState> (y, s[2], s[3], second, tap, stateScale, stateInverse);

            y *= settings.postGain;

            if (clipOutput)
                y = outScale * filter::cubicClip (y * outInverse);

            channels[ch][i] = y;
        }

        g += gStep;
    }

    // 不正な入力などで状態が壊れた場合は捨てる (NaN/Inf を鳴らし続けない)
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 4; ++i)
            if (! (std::abs (state[ch][i]) < 1.0e6f))
            {
                for (auto& channel : state)
                    for (auto& value : channel)
                        value = 0.0f;
                return;
            }
}
} // namespace

FilterSettings FilterSettings::make (FilterCharacter character, FilterMode mode, float resonance01, float drive01) noexcept
{
    const float resonance = std::clamp (resonance01, 0.0f, 1.0f);
    const float drive = std::clamp (drive01, 0.0f, 1.0f);
    const auto tuning = tuningFor (character);
    const bool twoStages = mode == FilterMode::lp24 || mode == FilterMode::hp24;

    // Q は指数カーブで上げる (ノブの前半は穏やか、後半で鋭く)。24 dB は 2 段目だけに持たせる。
    const float baseQ = twoStages ? butterworthQ2 : butterworthQ;
    const float q = baseQ * std::pow (tuning.maxQ / baseQ, resonance);

    FilterSettings s;
    s.character = character;
    s.mode = mode;
    s.k1 = 1.0f / butterworthQ1;
    s.k2 = 1.0f / q;

    // Filter Drive: 0..+24 dB の入力ゲイン (dB で線形)。
    s.preGain = std::pow (maxDriveGain, drive);

    // Clean は Drive 0 で入力段を完全に素通しにする。Analog は常に軽い飽和を通す。
    s.shapeInput = character == FilterCharacter::analog || drive > 0.0f;
    s.inputScale = character == FilterCharacter::analog ? 3.0f : (character == FilterCharacter::punch ? 2.0f : 2.5f);
    s.stateScale = 10.0f;
    s.outputScale = 1.5f;

    // 音量補正: Drive による音圧の上昇と、レゾナンスのピークを抑える。
    const float driveComp = std::pow (s.preGain, -0.6f);
    const float resonanceComp = std::pow (baseQ / q, tuning.resonanceComp);
    s.postGain = driveComp * resonanceComp;

    return s;
}

void StereoFilter::process (float* left, float* right, int numSamples,
                            const FilterSettings& settings, float gStart, float gStep) noexcept
{
    if (settings.character == FilterCharacter::analog)
        processStereo<true> (left, right, numSamples, state, settings, gStart, gStep);
    else
        processStereo<false> (left, right, numSamples, state, settings, gStart, gStep);
}

} // namespace polybrisk
