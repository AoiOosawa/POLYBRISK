#pragma once

#include "synth/Oscillator.h"

#include <array>
#include <cstdint>

namespace polybrisk
{

inline constexpr int maxUnisonVoices = 9;

/** ユニゾン各声のピッチ比とステレオゲイン。全ボイス共通なのでエンジンがコントロールレートで 1 回だけ計算する。 */
struct UnisonLayout
{
    int numVoices = 1;
    std::array<double, maxUnisonVoices> ratio {};   // 周波数比
    std::array<double, maxUnisonVoices> cents {};   // デチューン量 (テスト・表示用)
    std::array<float, maxUnisonVoices> pan {};      // -1..+1
    std::array<float, maxUnisonVoices> gainL {};
    std::array<float, maxUnisonVoices> gainR {};
};

/** Detune 0..1 を、最も外側の声のデチューン量 (cents) に変換する。 */
double detuneAmountToCents (float detune01) noexcept;

/** 左右対称のデチューン分布、均等なパン配置、パワー一定のゲイン正規化 (SPEC §4.3)。
    奇数本では声 0 が中央 (デチューン 0・センター定位)。

    mix01 は中央の声と外側の声の音量比 (D-045)。0.5 で全声が同じ音量、0 で中央だけ、1 で外側だけ。
    どの値でも合計のパワーは変わらない。偶数本では最も内側の対を「中央」として扱う。1〜2 本では効かない。

    asymmetric が true なら、対ごとに上下のデチューン量を少しずらす (SuperSAW 用)。
    上下が同じ量だと中央の声に対するうなりの速さがそろい、揺れが周期的に聞こえるため。 */
void computeUnisonLayout (UnisonLayout& layout, int numVoices, float detune01, float spread01,
                          float mix01 = 0.5f, bool asymmetric = false) noexcept;

/** 1 つの OSC (最大 9 声のユニゾン)。出力は left/right に加算する。 */
class UnisonOscillator
{
public:
    /** Free モード用の初期位相とノイズ系列を、ボイス固有のシードで初期化する。 */
    void seed (std::uint32_t seedValue) noexcept;

    /** Retrigger モード: 毎回同じ (再現性のある) 位相配置にそろえる。 */
    void retrigger() noexcept;

    /** numSamples 分を生成して加算する。baseIncrement は中央ピッチの位相増分 (cycles/sample)。 */
    void render (float* left, float* right, int numSamples,
                 Waveform waveform, double baseIncrement,
                 const UnisonLayout& layout, double pulseWidth) noexcept;

private:
    std::array<double, maxUnisonVoices> phase {};
    std::uint32_t noiseState = 0x12345678u;
};

} // namespace polybrisk
