#pragma once

#include <array>
#include <cstdint>

namespace polybrisk
{

inline constexpr int maxGateSteps = 32;

/** Trance Gate のステップパターン。常に 32 要素を持ち、先頭の Steps 個 (8 / 16 / 32) を使う。
    各要素は音量値 (0 = 閉じる、1 = 開く、中間値はアクセント)。固定長の POD。
    values は左 (Stereo が OFF のときは左右共通)、right は Stereo が ON のときの右チャンネル。 */
struct GatePattern
{
    std::array<float, maxGateSteps> values {};
    std::array<float, maxGateSteps> right {};

    bool isStereo() const noexcept { return values != right; }
    bool operator== (const GatePattern& other) const noexcept { return values == other.values && right == other.right; }
};

/** 収録パターン。16 ステップで定義し、32 ステップには 2 回繰り返して使う。
    先頭の 6 つは追加仕様 §3.3 の初期パターン (順序を変えない)。以降は D-039 で追加した。
    Fast 1/32 以外は Rate 1/16 (16 ステップ = 1 小節) を前提に書いてある。
    隣り合う ON のステップは途切れずにつながる (長い音価として働く)。 */
struct FactoryGatePattern
{
    const char* group;
    const char* name;
    const char* steps16;    // '1' / 'x' = 全開、'h' = 75 %、'm' = 50 %、'l' = 25 %、それ以外 = 閉じる
    const char* right16 = nullptr;  // 右チャンネル用 (ステレオのパターンだけ)。nullptr なら左と同じ
    int rateChoice = 1;             // このパターンが前提とする gate.rate の選択肢 (1 = 1/16、2 = 1/32、5 = 1/64)。選ぶと Rate がこの値になる (D-044)
};

/** パターン文字 1 つを音量値に変換する。 */
constexpr float gateStepLevel (char c) noexcept
{
    return (c == '1' || c == 'x') ? 1.0f : c == 'h' ? 0.75f : c == 'm' ? 0.5f : c == 'l' ? 0.25f : 0.0f;
}

inline constexpr auto factoryGatePatterns = std::to_array<FactoryGatePattern> ({
    // --- 追加仕様 §3.3 ---
    { "Basic", "Straight 8ths",  "1010101010101010" },
    { "Basic", "Pulse 16",       "1110111011101110" },
    { "Basic", "Offbeat",        "0101010101010101" },
    { "Basic", "Syncopated",     "1011010010110100" },
    { "Basic", "Build Up",       "1000100010101111" },
    { "Basic", "Full",           "1111111111111111" },

    // --- 拍に沿った刻み ---
    { "Straight", "Quarter Stabs",   "1000100010001000" },   // 4 分の頭だけ (短い)
    { "Straight", "Half Notes",      "1111111100000000" },   // 2 拍 ON / 2 拍 OFF
    { "Straight", "Half Chop",       "1100110011001100" },   // 8 分の長さで ON / OFF
    { "Straight", "Long Short",      "1110101011101010" },   // 付点 8 分 + 16 分 ×2
    { "Straight", "Quarter Chop",    "1111000011110000" },   // 1 拍 ON / 1 拍 OFF

    // --- トランス定番 ---
    { "Trance", "Gallop",            "1011101110111011" },   // タッ・タタ
    { "Trance", "Reverse Gallop",    "1101110111011101" },   // タタ・タッ
    { "Trance", "Offbeat Long",      "0011001100110011" },   // 裏拍を 8 分の長さで
    { "Trance", "Pump",              "0111011101110111" },   // 拍頭だけ閉じる (サイドチェイン風)
    { "Trance", "Late 16ths",        "0001000100010001" },   // 拍の最後の 16 分だけ
    { "Trance", "Trance Drive",      "1011011010110110" },   // 3+3+2 を 16 分で埋める
    { "Trance", "Rolling 16",        "1011101010111010" },
    { "Trance", "Uplift",            "1010101110101011" },   // 8 分 + 小節後半の 16 分

    // --- 1/32 で使う形 (16 ステップ = 2 拍)。16 分の刻みに 8 分以上の長い音が混ざる。選ぶと Rate がそのパターンの速さ (1/32 または 1/64) になる ---
    { "Fast 1/32", "Chop Tail",      "1100110011001110", nullptr, 5 },   // これだけ 1/64
    { "Fast 1/32", "Long Start",     "1111101010101110", nullptr, 2 },
    { "Fast 1/32", "Double Long",    "1010111011101011", nullptr, 2 },
    { "Fast 1/32", "Front Heavy",    "1110110010101010", nullptr, 2 },
    { "Fast 1/32", "Shifted Long",   "1010111010111010", nullptr, 2 },
    { "Fast 1/32", "Short Long",     "1010111010101110", nullptr, 2 },

    // --- シンコペーション ---
    { "Syncopated", "Dotted 8ths",   "1001001001001001" },   // 3 ステップ周期
    { "Syncopated", "Tresillo",      "1001001010010010" },   // 3+3+2
    { "Syncopated", "Tresillo Long", "1101101011011010" },   // 3+3+2 を長めの音価で
    { "Syncopated", "Clave 3-2",     "1001001000101000" },
    { "Syncopated", "Clave 2-3",     "0010100010010010" },
    { "Syncopated", "Push",          "1001001100100110" },   // 拍の手前で食う
    { "Syncopated", "Two Step",      "1010011010100110" },
    { "Syncopated", "Skip",          "1101011011010110" },

    // --- アクセント (中間の音量を使う) ---
    { "Accent", "Accent 4",          "xlllxlllxlllxlll" },   // 拍頭を強く、残りは弱く開く
    { "Accent", "Accent Offbeat",    "llxlllxlllxlllxl" },
    { "Accent", "Ghost Gallop",      "xlxxxlxxxlxxxlxx" },
    { "Accent", "Decay",             "xhmlxhmlxhmlxhml" },   // 拍ごとに減衰
    { "Accent", "Swell",             "lmhxlmhxlmhxlmhx" },   // 拍ごとに立ち上がる
    { "Accent", "Half Duck",         "mxxxmxxxmxxxmxxx" },   // 拍頭を半分だけ下げる (浅いポンピング)
    { "Accent", "Ramp Up",           "llllmmmmhhhhxxxx" },   // 1 小節かけて開く
    { "Accent", "Ramp Down",         "xxxxhhhhmmmmllll" },

    // --- フィル ---
    { "Fill", "Stutter End",         "1000100010001111" },
    { "Fill", "Double Time End",     "1010101011111111" },
    { "Fill", "Break",               "1110111011100000" },   // 4 拍目を空ける
    { "Fill", "Pickup",              "0000000000001011" },   // 小節の終わりだけ鳴らす

    // --- ステレオ (左 / 右)。選ぶと Gate の Stereo が ON になる ---
    { "Stereo", "Ping Pong 16",      "1010101010101010", "0101010101010101" },   // 16 分で左右交互
    { "Stereo", "Ping Pong 8",       "1100110011001100", "0011001100110011" },   // 8 分で左右交互
    { "Stereo", "Ping Pong Beat",    "1111000011110000", "0000111100001111" },   // 1 拍ごとに左右交互
    { "Stereo", "Side Echo",         "1000100010001000", "0010001000100010" },   // 拍頭は左、8 分裏は右
    { "Stereo", "Split Gallop",      "1000100010001000", "0011001100110011" },   // タッ (左)・タタ (右)
    { "Stereo", "Cross Pulse",       "1110111011101110", "1011101110111011" },   // 左右で休符の位置をずらす
    { "Stereo", "Call Response",     "1110000011100000", "0000111000001110" },
});

inline constexpr int numSpecGatePatterns = 6;        // 追加仕様 §3.3 の数
inline constexpr int defaultFactoryGatePattern = 23; // Shifted Long (Rate 1/32。gate.rate の既定値と対にする。D-044)

inline GatePattern makeGatePattern (const FactoryGatePattern& factory) noexcept
{
    GatePattern pattern;

    const char* right = factory.right16 != nullptr ? factory.right16 : factory.steps16;

    for (int i = 0; i < maxGateSteps; ++i)
    {
        pattern.values[static_cast<std::size_t> (i)] = gateStepLevel (factory.steps16[i % 16]);
        pattern.right[static_cast<std::size_t> (i)] = gateStepLevel (right[i % 16]);
    }

    return pattern;
}

/** ランダムなパターンを作る (16 ステップを 2 回繰り返す)。同じ seed なら同じ結果になる。
    1 拍目の頭は必ず開け、開くステップは全体の 6 割前後、そのうち一部を弱いアクセント (50 %) にする。 */
inline GatePattern makeRandomGatePattern (std::uint32_t seed, bool stereo) noexcept
{
    std::uint32_t state = seed != 0 ? seed : 0x9e3779b9u;
    const auto next = [&state]
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return static_cast<float> (state >> 8) * (1.0f / 16777216.0f);
    };

    const auto fill = [&next] (std::array<float, maxGateSteps>& lane, bool openFirst)
    {
        int open = 0;

        for (std::size_t i = 0; i < 16; ++i)
        {
            const bool on = (i == 0 && openFirst) || next() < 0.6f;
            lane[i] = on ? (next() < 0.2f && i != 0 ? 0.5f : 1.0f) : 0.0f;
            open += on ? 1 : 0;
        }

        // 開いたステップが少なすぎるときは、8 分の位置を開けて補う
        for (std::size_t i = 0; i < 16 && open < 5; i += 2)
            if (lane[i] <= 0.0f)
            {
                lane[i] = 1.0f;
                ++open;
            }

        for (std::size_t i = 0; i < 16; ++i)
            lane[i + 16] = lane[i];
    };

    GatePattern pattern;
    fill (pattern.values, true);

    if (stereo)
        fill (pattern.right, false);
    else
        pattern.right = pattern.values;

    return pattern;
}

} // namespace polybrisk
