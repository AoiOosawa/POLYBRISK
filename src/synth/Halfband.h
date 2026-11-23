#pragma once

#include <array>

namespace polybrisk
{

/** 2:1 デシメーション用のポリフェーズ・オールパス IIR ハーフバンドフィルター。
    ボイス処理を 2 倍のレートで行い、折り返し成分を落としてから元のレートへ戻すために使う。

    設計値 (係数 8 個、遷移幅 0.0465 fs、数値検証済み。docs/DECISIONS.md D-019):
      - 通過域 (〜0.2268 fs: 44.1 kHz 動作時に 20 kHz) のリップル 0.000001 dB 未満
      - 阻止域 (0.2733 fs〜) の減衰 104 dB
      - 最小位相に近い IIR のため、群遅延は低域で約 1.8 サンプル (出力レート換算)。ホストへのレイテンシー申告は行わない。 */
class HalfbandDecimator
{
public:
    void reset() noexcept
    {
        for (auto& s : stages)
            s = {};
    }

    /** 高レート側の連続する 2 サンプル (older, newer) から、低レート側の 1 サンプルを得る。 */
    float process (float older, float newer) noexcept
    {
        double a = newer; // 偶数番の係数 (A0)
        double b = older; // 奇数番の係数 (A1)。1 サンプル遅れの経路

        for (std::size_t i = 0; i < numCoefficients; i += 2)
        {
            a = stages[i].process (a, coefficients[i]);
            b = stages[i + 1].process (b, coefficients[i + 1]);
        }

        return static_cast<float> (0.5 * (a + b));
    }

    static constexpr std::size_t numCoefficients = 8;

private:
    struct Allpass
    {
        double x1 = 0.0;
        double y1 = 0.0;

        double process (double x, double c) noexcept
        {
            const double y = c * (x - y1) + x1;
            x1 = x;
            y1 = y;
            return y;
        }
    };

    static constexpr std::array<double, numCoefficients> coefficients {
        0.037358376062309746, 0.13933870119659977, 0.28130536543865164, 0.43620995090558884,
        0.58440754758823799,  0.71701908837848716, 0.83455010817689157, 0.9442921184231069
    };

    std::array<Allpass, numCoefficients> stages {};
};

/** 1:2 インターポレーション用のハーフバンドフィルター (デシメーターと同じ係数の転置形)。
    FX Drive の内部オーバーサンプリングで使う。 */
class HalfbandInterpolator
{
public:
    void reset() noexcept
    {
        for (auto& s : stages)
            s = {};
    }

    /** 低レート側の 1 サンプルから、高レート側の連続する 2 サンプル (first, second) を得る。 */
    void process (float input, float& first, float& second) noexcept
    {
        double a = input;
        double b = input;

        for (std::size_t i = 0; i < numCoefficients; i += 2)
        {
            a = stages[i].process (a, coefficients[i]);
            b = stages[i + 1].process (b, coefficients[i + 1]);
        }

        first = static_cast<float> (a);
        second = static_cast<float> (b);
    }

    static constexpr std::size_t numCoefficients = 8;

private:
    struct Allpass
    {
        double x1 = 0.0;
        double y1 = 0.0;

        double process (double x, double c) noexcept
        {
            const double y = c * (x - y1) + x1;
            x1 = x;
            y1 = y;
            return y;
        }
    };

    static constexpr std::array<double, numCoefficients> coefficients {
        0.037358376062309746, 0.13933870119659977, 0.28130536543865164, 0.43620995090558884,
        0.58440754758823799,  0.71701908837848716, 0.83455010817689157, 0.9442921184231069
    };

    std::array<Allpass, numCoefficients> stages {};
};

} // namespace polybrisk
