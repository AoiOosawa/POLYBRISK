#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace polybrisk
{

/** Transient レイヤーのモード (SPEC §5)。順序はパラメーターの選択肢と一致させる。 */
enum class TransientMode { noiseBurst, pitchSnap, both };

struct TransientParams
{
    bool enabled = false;
    TransientMode mode = TransientMode::noiseBurst;
    float amount = 0.5f;        // 0..1。ノイズの音量と Pitch Snap の深さの両方に掛かる
    float duration = 0.02f;     // 秒 (0.001..0.15)。この時間で -60 dB まで減衰する
    float noiseColor = 0.5f;    // 0 = Dark, 0.5 = ほぼホワイト, 1 = Bright
    float pitchSnap = 12.0f;    // semitones (-24..+24)。ここから 0 へ収束する
};

/** 全ボイス共通のコントロールレート値。 */
struct TransientControl
{
    bool noiseActive = false;
    bool pitchActive = false;
    float noiseLevel = 0.0f;        // amount × カラー補正
    float noiseDecay = 0.0f;        // 1 サンプルあたりの減衰係数
    float noiseAttackStep = 1.0f;   // ポップ防止の短い立ち上がり (1 サンプルあたり)
    float colourCoeff = 1.0f;       // カラー用 1 次ローパスの係数
    bool colourHighPass = false;    // true なら (ホワイト - ローパス) を出力
    double pitchSemitones = 0.0;    // amount × pitchSnap
    double decayRate = 0.0;         // 1 サンプルあたりの指数減衰率 (ln 単位)
    double endSamples = 0.0;        // これ以降は Pitch Snap を厳密に 0 とする

    static TransientControl make (const TransientParams& params, double sampleRate) noexcept
    {
        constexpr double sixtyDb = 6.907755278982137; // ln(1000)
        constexpr double pi = 3.141592653589793;

        TransientControl c;
        const float amount = std::clamp (params.amount, 0.0f, 1.0f);
        const double duration = std::clamp (static_cast<double> (params.duration), 0.001, 0.15);
        const bool wantsNoise = params.mode != TransientMode::pitchSnap;
        const bool wantsPitch = params.mode != TransientMode::noiseBurst;

        c.decayRate = sixtyDb / (duration * sampleRate);
        c.endSamples = 1.5 * duration * sampleRate;
        c.noiseDecay = static_cast<float> (std::exp (-c.decayRate));
        c.noiseAttackStep = static_cast<float> (1.0 / std::max (1.0, 0.0005 * sampleRate));

        // Dark 側はローパス (300 Hz → 20 kHz)、Bright 側はハイパス (100 Hz → 8 kHz)。中央はほぼホワイト。
        const double colour = std::clamp (static_cast<double> (params.noiseColor), 0.0, 1.0);
        c.colourHighPass = colour > 0.5;
        const double cutoff = c.colourHighPass ? 100.0 * std::pow (80.0, (colour - 0.5) * 2.0)
                                               : 300.0 * std::pow (20000.0 / 300.0, colour * 2.0);
        const double a = 1.0 - std::exp (-2.0 * pi * std::min (cutoff, 0.45 * sampleRate) / sampleRate);
        c.colourCoeff = static_cast<float> (a);

        // ローパスで失われる音量を半分 (dB) だけ補う。完全に補うと Dark 側が重くなりすぎる。
        const double lowPassPower = a / (2.0 - a);
        const double keptPower = c.colourHighPass ? std::max (1.0e-3, 1.0 - lowPassPower) : std::max (1.0e-3, lowPassPower);
        c.noiseLevel = amount * static_cast<float> (std::pow (keptPower, -0.25));

        c.pitchSemitones = static_cast<double> (amount) * std::clamp (static_cast<double> (params.pitchSnap), -24.0, 24.0);

        c.noiseActive = params.enabled && wantsNoise && amount > 0.0f;
        c.pitchActive = params.enabled && wantsPitch && c.pitchSemitones != 0.0;
        return c;
    }

    /** ノート開始から elapsedSamples 後の Pitch Snap (semitones)。 */
    double pitchOffsetAt (double elapsedSamples) const noexcept
    {
        if (! pitchActive || elapsedSamples >= endSamples)
            return 0.0;

        return pitchSemitones * std::exp (-decayRate * elapsedSamples);
    }
};

/** 1 ボイス分の Noise Burst。独立した短いエンベロープを持つ (SPEC §5)。 */
class NoiseBurst
{
public:
    void seed (std::uint32_t seedValue) noexcept { random = seedValue != 0 ? seedValue : 0x9e3779b9u; }

    void trigger() noexcept
    {
        envelope = 1.0f;
        attack = 0.0f;
        lowPass = 0.0f;
    }

    void stop() noexcept { envelope = 0.0f; }
    bool isActive() const noexcept { return envelope > 1.0e-5f; }

    /** モノラルのバーストを left/right の両方へ加算する。 */
    void render (float* left, float* right, int numSamples, const TransientControl& c) noexcept
    {
        for (int i = 0; i < numSamples; ++i)
        {
            random ^= random << 13;
            random ^= random >> 17;
            random ^= random << 5;
            const float white = static_cast<float> (random >> 8) * (2.0f / 16777216.0f) - 1.0f;

            lowPass += (white - lowPass) * c.colourCoeff;
            const float coloured = c.colourHighPass ? white - lowPass : lowPass;

            const float s = coloured * envelope * attack * c.noiseLevel;
            left[i] += s;
            right[i] += s;

            envelope *= c.noiseDecay;
            attack = std::min (1.0f, attack + c.noiseAttackStep);
        }
    }

private:
    std::uint32_t random = 0x9e3779b9u;
    float envelope = 0.0f;
    float attack = 0.0f;
    float lowPass = 0.0f;
};

} // namespace polybrisk
