#pragma once

#include "fx/FxChain.h"
#include "rhythm/Arpeggiator.h"
#include "rhythm/RhythmClock.h"
#include "rhythm/TranceGate.h"
#include "synth/Halfband.h"
#include "synth/Voice.h"

#include <array>
#include <cstdint>

namespace polybrisk
{

inline constexpr int maxPolyphony = 16;

struct OscParams
{
    bool enabled = true;
    Waveform waveform = Waveform::saw;
    int octave = 0;             // -2..+2
    int semitone = 0;           // -12..+12
    float fineCents = 0.0f;     // -100..+100
    float level = 1.0f;         // 0..1
    int unisonVoices = 1;       // 1..9
    float detune = 0.35f;       // 0..1
    float spread = 0.5f;        // 0..1
    float unisonMix = 0.5f;     // 0..1 (0.5 で全声が同じ音量。D-045)
    bool retrigger = true;      // Phase Mode: true = Retrigger, false = Free
    float pulseWidth = 0.5f;    // 0.05..0.95
};

struct FilterParams
{
    FilterCharacter character = FilterCharacter::clean;
    FilterMode mode = FilterMode::lp24;
    float cutoffHz = 20000.0f;  // 20..20000
    float resonance = 0.0f;     // 0..1
    float drive = 0.0f;         // 0..1
    float envAmount = 0.0f;     // -1..+1 (±1 で ±envRangeOctaves)
    float keyTrack = 0.0f;      // 0..1
    float velocity = 0.0f;      // 0..1 (Velocity → Cutoff)
};

struct PerformanceParams
{
    float bendUp = 2.0f;             // semitones (0..48)。ベンド最大で上がる量
    float bendDown = 2.0f;           // semitones (0..48)。ベンド最小で下がる量
    float vibratoDepthCents = 35.0f; // CC1 = 127 のときの深さ (0..200)
    float vibratoRateHz = 5.5f;      // 0.1..12
    float vibratoDelay = 0.0f;       // 秒 (0..2)
    float aftertouchVibratoCents = 0.0f; // アフタータッチ最大のときに加わるビブラートの深さ (0..200)
    float aftertouchCutoff = 0.0f;       // -1..+1 (±1 で ±modRangeOctaves)
};

/** 発音モード。順序は voice.mode の選択肢と一致させる。 */
enum class VoiceMode { poly, mono, legato };

struct SubParams
{
    float level = 0.0f;         // 0..1
    int octave = -1;            // -1 / -2
    SubWave wave = SubWave::sine;
};

struct NoiseParams
{
    float level = 0.0f;         // 0..1
    float color = 0.5f;         // 0 = Dark, 0.5 = ほぼホワイト, 1 = Bright
};

struct LfoParams
{
    LfoWave wave = LfoWave::sine;
    bool sync = false;
    float rateHz = 2.0f;        // 0.05..20 (sync = false のとき)
    double divisionBeats = 4.0; // 1 周期の長さ (拍)。sync = true のとき
    float fadeSeconds = 0.0f;   // 0..5
    bool retrigger = false;     // Key Sync
    float toCutoff = 0.0f;      // -1..+1 (±1 で ±modRangeOctaves)
    float toPulseWidth = 0.0f;  // 0..1 (1 でデューティー比 ±0.45)
    float toPitchCents = 0.0f;  // 0..1200
    float toAmp = 0.0f;         // 0..1
};

/** ブロック単位で DSP に渡すパラメーターのスナップショット (POD)。
    DSP 層は APVTS / JSON / UI を知らず、この構造体だけを入力とする。 */
struct EngineParams
{
    OscParams oscA;
    OscParams oscB { .enabled = false, .level = 0.7f };
    AdsrParams amp;
    float velocityToAmp = 1.0f; // 0..1 (Velocity → Amp Level)
    FilterParams filter;
    AdsrParams filterEnv { 0.002f, 0.3f, 0.3f, 0.2f };
    TransientParams transient;
    PerformanceParams performance;
    VoiceMode voiceMode = VoiceMode::poly;
    float glideSeconds = 0.0f;  // 0..2
    SubParams sub;
    NoiseParams noise;
    LfoParams lfo;
    FxParams fx;
    GateParams gate;
    ArpParams arp;
    float outputGain = 1.0f;    // linear
};

/** 16 ボイスのポリフォニック・エンジン (SPEC §3)。
    すべてのメソッドはリアルタイム安全 (確保・ロック・I/O なし)。 */
class SynthEngine final : private ArpSink
{
public:
    void prepare (double sampleRate) noexcept;
    void reset() noexcept;

    void setParams (const EngineParams& p) noexcept;

    void noteOn (int midiNote, float velocity) noexcept;
    void noteOff (int midiNote) noexcept;
    void setSustainPedal (bool down) noexcept;
    void allNotesOff() noexcept;   // CC123: 全ボイスを Release へ
    void allSoundOff() noexcept;   // CC120: 短いフェードで即時消音

    /** ホストのトランスポート (テンポ・再生状態・位置)。ホストのブロックごとに、レンダリングの前に呼ぶ。
        呼ばなければ内部クロック (120 BPM) で動く。 */
    void setTransport (const TransportInfo& info) noexcept;

    /** テンポだけを指定する簡易版 (停止中・位置不明として扱う)。 */
    void setTempo (double beatsPerMinute) noexcept { setTransport ({ beatsPerMinute, false, false, 0.0 }); }

    /** Arpeggiator の生成ノートを終了し、押鍵・Latch の保持を捨てる (プリセット切替時)。 */
    void resetArp() noexcept { arp.reset (*this); heldKeys = 0; }

    /** Trance Gate がいま再生しているステップ (UI 用。動作していなければ -1)。 */
    int getGateStep() const noexcept { return gate.getCurrentStep(); }
    const Arpeggiator& getArpeggiator() const noexcept { return arp; }

    /** FX の尾 (Delay / Reverb) まで含めて完全に無音か。 */
    bool isOutputSilent() const noexcept { return getActiveVoiceCount() == 0 && fx.isIdle(); }

    /** ピッチベンド。-1 (最小) .. 0 (中心) .. +1 (最大)。 */
    void setPitchBend (float normalised) noexcept { pitchBend = normalised < -1.0f ? -1.0f : (normalised > 1.0f ? 1.0f : normalised); }

    /** モジュレーションホイール (CC1)。0..1。 */
    void setModWheel (float normalised) noexcept { modWheel = normalised < 0.0f ? 0.0f : (normalised > 1.0f ? 1.0f : normalised); }

    /** アフタータッチ (チャンネルプレッシャー)。0..1。 */
    void setAftertouch (float normalised) noexcept { aftertouch = normalised < 0.0f ? 0.0f : (normalised > 1.0f ? 1.0f : normalised); }

    /** 14 bit の MIDI ピッチベンド値 (0..16383、中心 8192) を -1..+1 に変換する。両端で厳密に ±1。 */
    static float pitchWheelToNormalised (int value14Bit) noexcept
    {
        const int v = value14Bit < 0 ? 0 : (value14Bit > 16383 ? 16383 : value14Bit);
        return v >= 8192 ? static_cast<float> (v - 8192) / 8191.0f : static_cast<float> (v - 8192) / 8192.0f;
    }

    /** numSamples 分を left/right に上書きレンダリングする。 */
    void render (float* left, float* right, int numSamples) noexcept;

    int getActiveVoiceCount() const noexcept;
    bool isSilent() const noexcept { return getActiveVoiceCount() == 0; }
    const Voice& getVoice (int index) const noexcept { return voices[static_cast<std::size_t> (index)]; }

    static double midiNoteToHz (double midiNote) noexcept;

    /** 出力の安全用ソフトリミッター。|x| <= limiterThreshold では完全に線形、出力は ±1 を超えない。 */
    static float softLimit (float x) noexcept;
    static constexpr float limiterThreshold = 0.8f;

    /** Filter Env Amount ±100% のときのカットオフ移動量 (オクターブ)。docs/DECISIONS.md D-025 */
    static constexpr double envRangeOctaves = 8.0;

    /** LFO → Cutoff / Aftertouch → Cutoff が ±100% のときの移動量 (オクターブ)。 */
    static constexpr double modRangeOctaves = 4.0;

    /** Velocity → Cutoff 100% のとき、ベロシティ 0 で下がる量 (オクターブ)。 */
    static constexpr double velocityRangeOctaves = 5.0;

    /** ボイス処理の内部オーバーサンプリング倍率 (1 または 2)。prepare() で決まる。 */
    int getOversamplingFactor() const noexcept { return oversampling; }

    /** この値未満のサンプルレートでは、ボイス処理を 2 倍のレートで行う (docs/DECISIONS.md D-019)。 */
    static constexpr double oversamplingRateLimit = 70000.0;

private:
    void arpNoteOn (int midiNote, float velocity, std::uint32_t noteId) noexcept override;
    void arpNoteOff (std::uint32_t noteId) noexcept override;
    void startVoice (int midiNote, float velocity, std::uint32_t noteId) noexcept;

    void updateControl (int offsetInChunk = 0) noexcept;
    Voice* findHeldDirectVoice() noexcept;
    void pushHeldNote (int midiNote, float velocity) noexcept;
    void removeHeldNote (int midiNote) noexcept;
    void renderVoices (float* left, float* right, int numSamples) noexcept; // 内部レート
    Voice& chooseVoice() noexcept;

    struct OscSmoothingState
    {
        double pitchOffset = 0.0;
        double pulseWidth = 0.5;
        float detune = 0.0f;
        float spread = 0.0f;
        float unisonMix = 0.5f;
        float level = 0.0f;
    };

    void smoothOsc (OscControl& control, const OscParams& target, OscSmoothingState& state) noexcept;
    void smoothFilterControl() noexcept;

    double sampleRate = 44100.0;         // ホスト側のレート
    double internalSampleRate = 44100.0; // ボイス処理のレート (sampleRate × oversampling)
    int oversampling = 1;
    HalfbandDecimator decimatorL;
    HalfbandDecimator decimatorR;
    FxChain fx;
    RhythmClock clock;
    TranceGate gate;
    Arpeggiator arp;
    double gateOrigin = 0.0;             // Gate の Note On 起点 (拍)
    int heldKeys = 0;                    // 物理的に押されている鍵の数
    int silentSamples = 0;               // 全ボイス停止後に経過した出力サンプル数
    EngineParams params;
    VoiceControl control;

    std::array<Voice, maxPolyphony> voices;
    std::uint64_t noteCounter = 0;
    bool pedalDown = false;

    OscSmoothingState smoothA;
    OscSmoothingState smoothB;

    struct FilterSmoothingState
    {
        double cutoffOctaves = 14.287712379549449;
        double envOctaves = 0.0;
        double keyTrack = 0.0;
        double velocityOctaves = 0.0;
        float resonance = 0.0f;
        float drive = 0.0f;
    };

    FilterSmoothingState smoothFilter;
    float smoothedVelocityToAmp = 1.0f;

    float pitchBend = 0.0f;              // -1..+1 (MIDI からの目標値)
    float modWheel = 0.0f;               // 0..1
    double smoothedBendSemitones = 0.0;
    double smoothedModWheel = 0.0;
    double vibratoPhase = 0.0;           // 全ボイス共通の LFO 位相 (0..1)
    float smoothedOutputGain = 1.0f;

    float aftertouch = 0.0f;             // 0..1 (MIDI からの目標値)
    double smoothedAftertouch = 0.0;

    // Mono / Legato: 押さえている鍵の履歴 (新しいものが後ろ)。Poly では使わない
    struct HeldNote { int note; float velocity; };
    std::array<HeldNote, 32> heldNotes {};
    int numHeldNotes = 0;
    int lastNote = -1;                   // グライドの起点 (直前に発音したノート)

    // Sub / Noise のレベル (内部レートでスムージング)
    float smoothedSubLevel = 0.0f;
    float smoothedNoiseLevel = 0.0f;

    // LFO
    double lfoPhase = 0.0;               // 全ボイス共通の位相 (周期の数)
    double chunkStartBeats = 0.0;        // いま処理している区間の先頭の音楽位置 (拍)
    double beatsPerInternalSample = 0.0;
    struct LfoSmoothingState { double cutoff = 0.0, pulseWidth = 0.0, pitch = 0.0; float amp = 0.0f; };
    LfoSmoothingState smoothLfo;

    bool controlInitialised = false;
    int samplesUntilControl = 0;
    double controlCoeff = 1.0;   // コントロールレートの一次スムージング係数
    double analogLagCoeff = 1.0; // Analog フィルターのカットオフ追従係数 (コントロール周期ごと)
    float levelCoeff = 1.0f;     // OSC レベルの一次スムージング係数 (内部レート)
    float gainCoeff = 1.0f;      // 出力ゲインの一次スムージング係数 (ホスト側レート)
};

} // namespace polybrisk
