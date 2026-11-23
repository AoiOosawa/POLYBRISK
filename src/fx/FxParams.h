#pragma once

/** FX セクションのパラメーター (SPEC §8)。POD。選択肢の順序はパラメーターの選択肢と一致させる。 */
namespace polybrisk
{

enum class DriveType { warm, tape, tube, softClip, hardClip, wavefold };
enum class ChorusType { classic, wide };
enum class DelayMode { stereo, pingPong };
enum class DelayDivision { quarter, quarterDotted, quarterTriplet, eighth, eighthDotted, eighthTriplet,
                           sixteenth, sixteenthDotted, sixteenthTriplet };
enum class ReverbType { room, plate, hall };
enum class PhaserType { phaser, flanger };

struct DriveParams
{
    bool enabled = false;
    DriveType type = DriveType::warm;
    float drive = 0.3f;         // 0..1
    float tone = 1.0f;          // 0..1 (0 = 暗い, 1 = 全開)
    float mix = 1.0f;           // 0..1
    float outputDb = 0.0f;      // -24..+12
    bool autoGain = true;
};

struct ChorusParams
{
    bool enabled = false;
    ChorusType type = ChorusType::classic;
    float rateHz = 0.8f;        // 0.05..8
    float depth = 0.5f;         // 0..1
    float width = 1.0f;         // 0..1
    float mix = 0.5f;           // 0..1
};

struct EqParams
{
    bool enabled = false;
    float lowGainDb = 0.0f;     // -18..+18 (100 Hz シェルフ)
    float midFreqHz = 1000.0f;  // 200..8000
    float midGainDb = 0.0f;     // -18..+18 (Q = 1)
    float highGainDb = 0.0f;    // -18..+18 (8 kHz シェルフ)
};

struct ImagerParams
{
    bool enabled = false;
    float width = 1.0f;         // 0..2 (1 = 元の幅, 0 = モノ)
    bool monoBass = false;      // 150 Hz 未満の Side を取り除く
};

struct DelayParams
{
    bool enabled = false;
    DelayMode mode = DelayMode::stereo;
    bool sync = true;
    DelayDivision division = DelayDivision::eighthDotted;
    float timeSeconds = 0.35f;  // 0.001..2 (sync = false のとき)
    float feedback = 0.35f;     // 0..0.95
    float tone = 0.6f;          // 0..1
    float mix = 0.25f;          // 0..1
    float ducking = 0.0f;       // 0..1
};

struct ReverbParams
{
    bool enabled = false;
    ReverbType type = ReverbType::plate;
    float size = 0.5f;          // 0..1
    float decaySeconds = 1.8f;  // 0.2..15
    float preDelaySeconds = 0.01f; // 0..0.2
    float tone = 0.6f;          // 0..1
    float mix = 0.2f;           // 0..1
};

struct PhaserParams
{
    bool enabled = false;
    PhaserType type = PhaserType::phaser;
    float rateHz = 0.4f;        // 0.02..8
    float depth = 0.6f;         // 0..1
    float feedback = 0.4f;      // 0..0.9
    float mix = 0.5f;           // 0..1 (1 で原音とエフェクト音が 1:1)
};

struct FxParams
{
    bool bypassAll = false;
    DriveParams drive;
    PhaserParams phaser;
    ChorusParams chorus;
    EqParams eq;
    ImagerParams imager;
    DelayParams delay;
    ReverbParams reverb;
};

/** 同期音価の長さ (拍 = 4 分音符を 1 とする)。 */
inline double delayDivisionBeats (DelayDivision division) noexcept
{
    switch (division)
    {
        case DelayDivision::quarter:          return 1.0;
        case DelayDivision::quarterDotted:    return 1.5;
        case DelayDivision::quarterTriplet:   return 2.0 / 3.0;
        case DelayDivision::eighth:           return 0.5;
        case DelayDivision::eighthDotted:     return 0.75;
        case DelayDivision::eighthTriplet:    return 1.0 / 3.0;
        case DelayDivision::sixteenth:        return 0.25;
        case DelayDivision::sixteenthDotted:  return 0.375;
        case DelayDivision::sixteenthTriplet: return 1.0 / 6.0;
        default:                              return 0.5;
    }
}

} // namespace polybrisk
