#pragma once

#include "rhythm/RhythmClock.h"

#include <array>
#include <cstdint>

namespace polybrisk
{

/** 順序はパラメーターの選択肢と一致させる。 */
enum class ArpMode { up, down, upDown, random, asPlayed };

struct ArpParams
{
    bool enabled = false;
    ArpMode mode = ArpMode::up;
    double rateBeats = 0.25;    // 1 ステップの長さ (拍)
    int octaves = 1;            // 1..4
    float gate = 0.65f;         // 0.1..1 (ステップの実長に対する比率)
    float swing = 0.0f;         // 0..0.5
    float velocity = 1.0f;      // 0.25..2 (入力ベロシティに対する係数)
    bool latch = false;
    bool restartOnNote = true;  // false = Transport、true = Note On
};

/** Arpeggiator が生成したノートの送り先 (SynthEngine が実装する)。
    生成ノートは内部 Note ID で識別するので、同音が重なっても正しいボイスを終了できる。 */
class ArpSink
{
public:
    virtual ~ArpSink() = default;
    virtual void arpNoteOn (int midiNote, float velocity, std::uint32_t noteId) noexcept = 0;
    virtual void arpNoteOff (std::uint32_t noteId) noexcept = 0;
};

/** Arpeggiator (追加仕様 §4)。押鍵・離鍵・CC64・Latch の状態機械と、ステップごとのノート生成。
    固定長の配列だけを使い、確保を行わない。外部 MIDI 出力はしない (音源内だけで発音する)。 */
class Arpeggiator
{
public:
    static constexpr int maxKeys = 32;
    /** シーケンスの最大長。オクターブ展開で maxKeys × 4、Up/Down はそれを往復するので最大でその 2 倍弱 (2n − 2)。 */
    static constexpr int maxSequence = maxKeys * 4 * 2;

    /** 生成中のノートを終了し、押鍵・Latch・ペダルの状態をすべて捨てる。 */
    void reset (ArpSink& sink) noexcept;

    void setParams (const ArpParams& params, ArpSink& sink) noexcept;

    /** 鍵盤が押された。position は現在のクロック位置 (拍)。 */
    void keyDown (int midiNote, float velocity, double position, ArpSink& sink) noexcept;
    void keyUp (int midiNote, ArpSink& sink) noexcept;
    void setPedal (bool down, ArpSink& sink) noexcept;

    /** クロックの位置が不連続になった (ループ・シーク・再生開始)。生成ノートを終了して同期し直す。 */
    void handleDiscontinuity (double position, ArpSink& sink) noexcept;

    /** ホストが停止した。Latch で保持しているだけの音は捨てる (ノートの張り付き防止)。 */
    void handleHostStop (double position, ArpSink& sink) noexcept;

    /** これから処理する numSamples の各サンプル位置 beats[] を調べ、最初のサンプル位置のイベントを発行する。
        次のイベントまでに処理してよいサンプル数 (1..numSamples) を返す。 */
    int advance (const double* beats, int numSamples, bool hostDriven, double beatsPerSample, ArpSink& sink) noexcept;

    int getNumKeys() const noexcept { return numKeys; }
    bool isSounding() const noexcept { return soundingId != 0; }

    /** 現在の押鍵集合から、1 周分の発音順序を作る。戻り値は長さ。テストからも使う。 */
    int buildSequence (std::array<int, maxSequence>& notes, std::array<float, maxSequence>& velocities) const noexcept;

private:
    struct Key
    {
        int note = 0;
        float velocity = 0.0f;
        std::uint32_t order = 0;    // 押された順
        bool physical = false;      // 物理的に押されている
    };

    void removeReleasedKeys (ArpSink& sink) noexcept;
    void stopSounding (ArpSink& sink) noexcept;
    void startPhrase (double position) noexcept;
    bool usesOrigin (bool hostDriven) const noexcept { return params.restartOnNote || ! hostDriven; }
    int physicalCount() const noexcept;

    ArpParams params;
    std::array<Key, maxKeys> keys {};
    int numKeys = 0;
    std::uint32_t orderCounter = 0;
    bool pedalDown = false;

    double origin = 0.0;            // Note On 起点 (拍)
    bool needsSync = true;          // 次の advance で、いまいるステップを鳴らすか待つかを決める
    bool hasStep = false;
    std::int64_t currentStep = 0;   // 最後に発音したステップ番号
    std::int64_t phraseCounter = 0; // フレーズ開始からの発音回数

    std::uint32_t soundingId = 0;   // 発音中の生成ノート (0 = なし)
    double noteOffBeats = 0.0;      // その Note Off 位置 (起点からの拍)
    std::uint32_t idCounter = 0;
};

} // namespace polybrisk
