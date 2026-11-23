#pragma once

#include <cmath>

namespace polybrisk
{

/** 発振波形。順序は osc.a.wave / osc.b.wave の選択肢順 (SPEC §4.1) と一致させる。 */
enum class Waveform : int
{
    saw = 0,
    superSaw, // 波形は Saw。ユニゾンのデチューンが上下で非対称になる (D-045。既定のユニゾン本数は D-016)
    pulse,
    square,
    triangle,
    sine,
    noise
};

/** バンド制限した 1 サンプル生成関数群。
    phase は 0..1、inc は 1 サンプルあたりの位相増分 (0 < inc < 0.5)。
    いずれも位相 0 でゼロ付近から立ち上がるよう位相をそろえてある (Pulse を除く)。 */
namespace osc
{

inline constexpr double twoPi = 6.283185307179586476925286766559;

/** 2 サンプル幅の PolyBLEP 残差 (段差 2 の不連続に対応)。 */
inline double polyBlep (double t, double dt) noexcept
{
    if (t < dt)
    {
        const double x = t / dt;
        return x + x - x * x - 1.0;
    }

    if (t > 1.0 - dt)
    {
        const double x = (t - 1.0) / dt;
        return x * x + x + x + 1.0;
    }

    return 0.0;
}

/** 2 サンプル幅の PolyBLAMP 残差 (傾きの不連続に対応)。 */
inline double polyBlamp (double t, double dt) noexcept
{
    if (t < dt)
    {
        const double x = t / dt - 1.0;
        return -(x * x * x) / 3.0;
    }

    if (t > 1.0 - dt)
    {
        const double x = (t - 1.0) / dt + 1.0;
        return (x * x * x) / 3.0;
    }

    return 0.0;
}

inline double wrap01 (double x) noexcept
{
    return x >= 1.0 ? x - 1.0 : x;
}

inline double saw (double phase, double inc) noexcept
{
    const double t = wrap01 (phase + 0.5);
    return 2.0 * t - 1.0 - polyBlep (t, inc);
}

/** width はデューティー比 (0..1)。DC を除去して平均ゼロにする。 */
inline double pulse (double phase, double inc, double width) noexcept
{
    double v = phase < width ? 1.0 : -1.0;
    v += polyBlep (phase, inc);
    v -= polyBlep (wrap01 (phase + 1.0 - width), inc);
    return v - (2.0 * width - 1.0);
}

inline double triangle (double phase, double inc) noexcept
{
    const double t = wrap01 (phase + 0.75);
    double v = 4.0 * std::abs (t - 0.5) - 1.0;
    v += 4.0 * inc * (polyBlamp (wrap01 (t + 0.5), inc) - polyBlamp (t, inc));
    return v;
}

inline double sine (double phase) noexcept
{
    return std::sin (twoPi * phase);
}

} // namespace osc
} // namespace polybrisk
