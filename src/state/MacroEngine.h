#pragma once

#include <algorithm>
#include <array>

/** マクロの評価 (SPEC §10.2)。JUCE に依存しない POD と純粋関数だけで構成し、
    オーディオスレッドから安全に使える。割当の編集は MetaState (メッセージスレッド) が行う。 */
namespace polybrisk::macro
{

inline constexpr int numMacros = 4;
inline constexpr int maxMappingsPerMacro = 8;   // SPEC §10.1 [暫定]
inline constexpr int maxEntries = numMacros * maxMappingsPerMacro;

/** 順序は保存値と一致させる (追加のみ可)。 */
enum class Curve { linear, easeIn, easeOut };

inline float shape (Curve curve, float x) noexcept
{
    const float c = std::clamp (x, 0.0f, 1.0f);

    switch (curve)
    {
        case Curve::easeIn:  return c * c;
        case Curve::easeOut: return 1.0f - (1.0f - c) * (1.0f - c);
        case Curve::linear:
        default:             return c;
    }
}

struct Entry
{
    int parameterIndex = -1;    // params::all() の添字
    int macroIndex = 0;         // 0..3
    float deltaMin = 0.0f;      // マクロ 0 のときの正規化オフセット
    float deltaMax = 0.0f;      // マクロ 1 のときの正規化オフセット
    Curve curve = Curve::linear;
};

/** マクロ値 (0..1) に対する正規化オフセット。0.5 では常に 0。 */
inline float delta (const Entry& e, float macroValue) noexcept
{
    const float m = std::clamp (macroValue, 0.0f, 1.0f);

    if (m < 0.5f)
        return e.deltaMin * (1.0f - shape (e.curve, m * 2.0f));

    return e.deltaMax * shape (e.curve, (m - 0.5f) * 2.0f);
}

/** オーディオスレッドへ渡す immutable な設定。固定長で、コピーしても確保が起きない。 */
struct Snapshot
{
    std::array<Entry, maxEntries> entries {};
    int numEntries = 0;
    std::array<int, numMacros> cc { -1, -1, -1, -1 };   // MIDI Learn で割り当てた CC 番号 (-1 = なし)
};

/** MIDI Learn で割り当てられる CC か (SPEC §10.1: CC2〜119、CC64 は Sustain に予約)。 */
inline bool isAssignableCc (int controller) noexcept
{
    return controller >= 2 && controller <= 119 && controller != 64;
}

} // namespace polybrisk::macro
