#pragma once

#include <algorithm>
#include <cmath>

namespace polybrisk
{

/** フィルターのキャラクター (SPEC §6.1)。順序はパラメーターの選択肢と一致させる。 */
enum class FilterCharacter { analog, clean, punch };

/** フィルターのモード (SPEC §6.1)。順序はパラメーターの選択肢と一致させる。 */
enum class FilterMode { lp12, lp24, hp12, hp24, bp12 };

namespace filter
{
inline constexpr double minCutoffHz = 20.0;
inline constexpr double maxCutoffHz = 20000.0;

/** tanh の有理近似 (|x| <= 3 で誤差 2.4% 以下、それ以上は ±1 に固定)。単調で連続。 */
inline float fastTanh (float x) noexcept
{
    const float c = std::clamp (x, -3.0f, 3.0f);
    const float c2 = c * c;
    return c * (27.0f + c2) / (27.0f + 9.0f * c2);
}

/** 3 次のソフトクリップ。|x| <= 1.5 で x - (4/27)x^3、それ以上は ±1 (傾きが 0 でつながる)。 */
inline float cubicClip (float x) noexcept
{
    const float c = std::clamp (x, -1.5f, 1.5f);
    return c - (4.0f / 27.0f) * c * c * c;
}

/** カットオフ周波数を TPT の係数 g = tan(pi * fc / fs) に変換する。Nyquist の安全域にクランプする。 */
inline float cutoffToG (double cutoffHz, double sampleRate) noexcept
{
    const double limited = std::clamp (cutoffHz, minCutoffHz, std::min (maxCutoffHz, 0.45 * sampleRate));
    return static_cast<float> (std::tan (3.141592653589793 * limited / sampleRate));
}
} // namespace filter

/** 全ボイス共通のフィルター設定。エンジンがコントロールレートで 1 回だけ計算する。 */
struct FilterSettings
{
    FilterCharacter character = FilterCharacter::clean;
    FilterMode mode = FilterMode::lp24;
    float k1 = 1.8477591f;      // 1 段目のダンピング (1/Q)
    float k2 = 0.7653669f;      // 2 段目のダンピング (24 dB のみ)
    float preGain = 1.0f;       // Filter Drive: 入力ゲイン
    float postGain = 1.0f;      // Drive とレゾナンスに対する音量補正
    bool shapeInput = false;    // 入力のサチュレーターを通すか
    float inputScale = 1.0f;    // 入力サチュレーターの飽和レベル
    float stateScale = 10.0f;   // Analog: 積分器の飽和レベル
    float outputScale = 1.5f;   // Punch: 出力クリッパーの飽和レベル

    /** resonance01 / drive01 は 0..1。 */
    static FilterSettings make (FilterCharacter character, FilterMode mode, float resonance01, float drive01) noexcept;
};

/** 1 ボイス分のステレオフィルター (TPT / SVF 2 段)。
    係数 g はサンプルごとに線形補間して受け取る (カットオフ変調のジッパーノイズを防ぐ)。
    - Clean : 線形。Drive 0 では入力段も完全に線形。
    - Analog: 入力と積分器に軽い飽和を持つ (レゾナンスが自己制限される)。
    - Punch : 線形コア + レゾナンスの強い音量補正 + 出力のソフトクリップ。 */
class StereoFilter
{
public:
    void reset() noexcept
    {
        for (auto& channel : state)
            for (auto& s : channel)
                s = 0.0f;
    }

    /** left/right をその場で処理する。g は gStart から 1 サンプルごとに gStep ずつ進む。 */
    void process (float* left, float* right, int numSamples,
                  const FilterSettings& settings, float gStart, float gStep) noexcept;

private:
    // [channel][ic1eq(1段目), ic2eq(1段目), ic1eq(2段目), ic2eq(2段目)]
    float state[2][4] = {};
};

} // namespace polybrisk
