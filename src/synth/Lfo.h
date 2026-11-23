#pragma once

#include <cmath>
#include <cstdint>

namespace polybrisk
{

/** LFO の波形。順序は lfo.wave の選択肢と一致させる。 */
enum class LfoWave { sine, triangle, saw, square, sampleHold };

namespace lfo
{
/** 整数 (周期の番号) から -1..+1 の値を作る。状態を持たないので、同じ周期では常に同じ値になる。 */
inline double hashToBipolar (std::int64_t cycle, std::uint32_t seed) noexcept
{
    auto x = static_cast<std::uint32_t> (static_cast<std::uint64_t> (cycle) * 0x9e3779b1u) ^ seed;
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return static_cast<double> (x >> 8) * (2.0 / 16777216.0) - 1.0;
}

/** phaseCycles (周期の数。整数部を含む) での LFO の値 (-1..+1)。
    Sine / Triangle は 0 から上へ始まる。Saw は +1 から -1 へ下がる。S&H は周期ごとに一定の乱数。 */
inline double value (LfoWave wave, double phaseCycles, std::uint32_t seed) noexcept
{
    const double whole = std::floor (phaseCycles);
    const double t = phaseCycles - whole;

    switch (wave)
    {
        case LfoWave::triangle:   return t < 0.25 ? 4.0 * t : (t < 0.75 ? 2.0 - 4.0 * t : 4.0 * t - 4.0);
        case LfoWave::saw:        return 1.0 - 2.0 * t;
        case LfoWave::square:     return t < 0.5 ? 1.0 : -1.0;
        case LfoWave::sampleHold: return hashToBipolar (static_cast<std::int64_t> (whole), seed);
        case LfoWave::sine:
        default:                  return std::sin (6.283185307179586 * t);
    }
}
} // namespace lfo
} // namespace polybrisk
