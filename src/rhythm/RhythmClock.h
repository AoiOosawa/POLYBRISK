#pragma once

#include <cmath>
#include <cstdint>

/** RHYTHM セクション (Trance Gate / Arpeggiator) の共通クロック。
    追加仕様 docs/ADDON_TRANCE_GATE_ARPEGGIATOR.md §5。JUCE に依存しない。
    位置の単位は「拍」(4 分音符 = 1)。ホストの PPQ と同じ。 */
namespace polybrisk
{

/** ホストのトランスポート情報 (プロセッサーがブロックごとに詰めて渡す POD)。 */
struct TransportInfo
{
    double bpm = 0.0;           // 0 以下 / 非有限なら不明 → 120 BPM
    bool playing = false;
    bool positionValid = false; // ppq が有効か
    double ppq = 0.0;           // ブロック先頭の位置 (拍)
};

namespace rhythm
{
inline constexpr double fallbackBpm = 120.0;

struct StepLocation
{
    std::int64_t index = 0; // 位置 0 から数えたステップ番号 (負の位置では負)
    double start = 0.0;     // このステップの開始位置 (拍)
    double length = 0.0;    // このステップの長さ (拍)。Swing で伸縮する
};

/** 位置 (拍) がどのステップにあるかを求める。
    Swing k (0..0.5): 2 ステップを 1 組とし、前を T(1+k)、後ろを T(1-k) にする。合計は常に 2T。 */
inline constexpr double boundaryTolerance = 1.0e-9;

inline StepLocation locateStep (double position, double stepBeats, double swing) noexcept
{
    // サンプルごとに積算した位置は、境界のごく手前 (1e-13 拍程度) に来ることがある。
    // それを「まだ前のステップ」と判定すると境界が 1 サンプル遅れ、ブロック長によって結果が変わってしまう。
    const double beats = position + boundaryTolerance;

    const double k = swing < 0.0 ? 0.0 : (swing > 0.5 ? 0.5 : swing);
    const double pair = 2.0 * stepBeats;
    const double pairIndex = std::floor (beats / pair);
    const double pairStart = pairIndex * pair;
    const double firstLength = stepBeats * (1.0 + k);
    const auto even = static_cast<std::int64_t> (pairIndex) * 2;

    if (beats - pairStart < firstLength)
        return { even, pairStart, firstLength };

    return { even + 1, pairStart + firstLength, stepBeats * (1.0 - k) };
}
} // namespace rhythm

/** サンプル単位で進む音楽位置。
    - ホストが再生中で PPQ が有効なら、ホストの位置を正本とする (ブロックごとに取り込み、ブロック内はテンポで進める)。
    - それ以外は内部クロックで進み続ける。
    - ホスト位置のジャンプ (ループ・シーク) や、ホスト駆動 ⇄ 内部クロックの切り替わりは discontinuity として知らせる。 */
class RhythmClock
{
public:
    void prepare (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
        reset();
    }

    void reset() noexcept
    {
        position = 0.0;
        hostDriven = false;
        wasPlaying = false;
        discontinuity = false;
        stopped = false;
        setBpm (rhythm::fallbackBpm);
    }

    /** ホストのブロックの先頭で呼ぶ。 */
    void setTransport (const TransportInfo& info) noexcept
    {
        setBpm ((std::isfinite (info.bpm) && info.bpm >= 20.0 && info.bpm <= 999.0) ? info.bpm : rhythm::fallbackBpm);

        const bool nowHostDriven = info.playing && info.positionValid && std::isfinite (info.ppq);

        if (nowHostDriven)
        {
            // 連続して再生中なら、ホストの位置は手元の位置とほぼ一致する。大きくずれていればジャンプ。
            if (! hostDriven || std::abs (info.ppq - position) > 4.0 * beatsPerSample + 1.0e-6)
                discontinuity = true;

            position = info.ppq;
        }
        else if (hostDriven)
        {
            discontinuity = true;   // 内部クロックへ切り替わる (位置はそのまま進め続ける)
        }

        if (wasPlaying && ! info.playing)
            stopped = true;

        wasPlaying = info.playing;
        hostDriven = nowHostDriven;
    }

    /** これから処理する numSamples 個の各サンプルの位置 (拍) を書き出す。状態は進めない。 */
    void peek (double* beats, int numSamples) const noexcept
    {
        for (int i = 0; i < numSamples; ++i)
            beats[i] = position + static_cast<double> (i) * beatsPerSample;
    }

    void advance (int numSamples) noexcept { position += static_cast<double> (numSamples) * beatsPerSample; }

    double getPosition() const noexcept { return position; }
    double getBeatsPerSample() const noexcept { return beatsPerSample; }
    double getBpm() const noexcept { return bpm; }
    bool isHostDriven() const noexcept { return hostDriven; }

    /** 前回の呼び出し以降に位置の不連続があったか (読むとクリアされる)。 */
    bool consumeDiscontinuity() noexcept
    {
        const bool d = discontinuity;
        discontinuity = false;
        return d;
    }

    /** 前回の呼び出し以降にホストが停止したか (読むとクリアされる)。 */
    bool consumeStopped() noexcept
    {
        const bool s = stopped;
        stopped = false;
        return s;
    }

private:
    void setBpm (double newBpm) noexcept
    {
        bpm = newBpm;
        beatsPerSample = bpm / (60.0 * sampleRate);
    }

    double sampleRate = 44100.0;
    double bpm = rhythm::fallbackBpm;
    double beatsPerSample = rhythm::fallbackBpm / (60.0 * 44100.0);
    double position = 0.0;
    bool hostDriven = false;
    bool wasPlaying = false;
    bool discontinuity = false;
    bool stopped = false;
};

} // namespace polybrisk
