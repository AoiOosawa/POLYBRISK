#pragma once

#include "fx/Chorus.h"
#include "fx/Delay.h"
#include "fx/Drive.h"
#include "fx/Equalizer.h"
#include "fx/Imager.h"
#include "fx/Phaser.h"
#include "fx/Reverb.h"

namespace polybrisk
{

/** グローバル FX (SPEC §2 / §8): Drive → Phaser → Chorus → EQ → Imager → [Dry + Delay wet + Reverb wet]。
    - Delay と Reverb は同じ信号 (Imager の後) を入力とする並列の wet パスで、互いに接続しない。
    - 各 FX の ON/OFF と一括バイパスは 10 ms のクロスフェードで切り替える。
    - prepare() だけが確保を行う。process() はリアルタイム安全。 */
class FxChain
{
public:
    static constexpr int maxBlock = 128;

    void prepare (double sampleRate);
    void reset() noexcept;

    /** left / right をその場で処理する。numSamples <= maxBlock。
        gateLeft / gateRight が nullptr でなければ、Trance Gate のゲインとして Imager の後・Delay / Reverb の前に掛ける
        (FX の一括バイパスの対象外)。 */
    void process (float* left, float* right, int numSamples, const FxParams& params, double bpm,
                  const float* gateLeft = nullptr, const float* gateRight = nullptr) noexcept;

    /** 入力が無音で、残響・ディレイの尾も消えている。 */
    bool isIdle() const noexcept { return idle; }

    /** Mix (0..1) を dry / wet のゲインに変換する。50% までは dry を保ち、それ以上で dry を下げる。 */
    static float wetGainFor (float mix) noexcept { return mix < 0.5f ? 2.0f * (mix < 0.0f ? 0.0f : mix) : 1.0f; }
    static float dryGainFor (float mix) noexcept { return mix > 0.5f ? 2.0f * (1.0f - (mix > 1.0f ? 1.0f : mix)) : 1.0f; }

private:
    struct Switch
    {
        float gain = 0.0f;      // 0 = バイパス, 1 = 有効
        bool wasActive = false;
    };

    template <typename Fx, typename Params>
    void insert (Fx& fx, Switch& sw, bool enabled, const Params& params, float* left, float* right, int numSamples) noexcept;

    Drive drive;
    Phaser phaser;
    Chorus chorus;
    Equalizer eq;
    Imager imager;
    Delay delay;
    Reverb reverb;

    Switch driveSwitch, phaserSwitch, chorusSwitch, eqSwitch, imagerSwitch, delaySwitch, reverbSwitch;

    double sampleRate = 48000.0;
    float switchStep = 0.01f;
    float mixSmoothing = 1.0f;
    float delayMix = 0.0f, reverbMix = 0.0f;
    bool mixInitialised = false;

    int quietSamples = 0;
    int idleThreshold = 12000;
    bool idle = true;
};

} // namespace polybrisk
