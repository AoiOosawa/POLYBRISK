#pragma once

#include "synth/Envelopes.h"
#include "synth/Filters.h"
#include "synth/Lfo.h"
#include "synth/Transient.h"
#include "synth/Unison.h"

#include <cstdint>

namespace polybrisk
{

/** コントロールレートの最大チャンク長。エンジンはこの長さ以下に分割して各ボイスを処理する。 */
inline constexpr int controlBlockSize = 32;

/** 1 つの OSC について、全ボイス共通のコントロールレート値。 */
struct OscControl
{
    bool enabled = false;
    Waveform waveform = Waveform::saw;
    bool retrigger = true;
    double pitchOffset = 0.0;   // semitones (octave + semitone + fine)
    double pulseWidth = 0.5;
    UnisonLayout layout;
};

/** フィルターについて、全ボイス共通のコントロールレート値。カットオフはオクターブ (log2 Hz) で扱う。 */
struct FilterControl
{
    FilterSettings settings;
    AdsrCoefficients env;
    double cutoffOctaves = 14.287712379549449; // log2 (20000)
    double envOctaves = 0.0;       // Filter EG が 1 のときの移動量 (バイポーラ)
    double keyTrack = 0.0;         // 0..1。1 でノート 60 を基準に音程へ 1:1 で追従
    double velocityOctaves = 0.0;  // ベロシティ 0 のときに下がる量 (ベロシティ 1 では 0)
    double lagCoeff = 1.0;         // コントロール周期ごとの追従係数 (1 = 遅れなし)
};

/** Sub オシレーター (ユニゾンなし・センター定位)。ノートのピッチに追従し、OSC A / B の Tune の影響は受けない。 */
enum class SubWave { sine, square };

struct SubControl
{
    bool enabled = false;       // レベルが 0 で落ち着いているあいだは false (処理しない)
    SubWave wave = SubWave::sine;
    double pitchOffset = -12.0; // semitones (-12 / -24)
};

/** 持続するノイズ層 (Transient の Noise Burst とは別)。 */
struct NoiseControl
{
    bool enabled = false;
    float colourCoeff = 1.0f;   // カラー用 1 次ローパスの係数
    bool colourHighPass = false;
    float gain = 1.0f;          // カラーで失われる音量の補正
};

/** LFO について、全ボイス共通のコントロールレート値。深さがすべて 0 なら active = false で、ボイスは何も計算しない。 */
struct LfoControl
{
    bool active = false;
    LfoWave wave = LfoWave::sine;
    bool retrigger = false;         // true: ノートごとに位相 0 から。false: 全ボイス共通の位相
    double globalPhase = 0.0;       // 周期の数 (整数部を含む)
    double cyclesPerSample = 0.0;   // 内部レートの 1 サンプルあたり
    double fadeSamples = 0.0;       // ノートオンからこの時間でなだらかに立ち上げる
    double cutoffOctaves = 0.0;     // LFO が ±1 のときのカットオフの移動量
    double pulseWidth = 0.0;        // 同、Pulse Width の移動量 (デューティー比)
    double pitchSemitones = 0.0;    // 同、ピッチの移動量
    float amp = 0.0f;               // 0..1。トレモロの深さ (1 で谷が無音)
};

/** サンプルごとのレベル (エンジン側でスムージング済み)。 */
struct VoiceLevels
{
    const float* a = nullptr;
    const float* b = nullptr;
    const float* sub = nullptr;
    const float* noise = nullptr;
};

struct VoiceControl
{
    OscControl oscA;
    OscControl oscB;
    SubControl sub;
    NoiseControl noise;
    LfoControl lfo;
    double glideSamples = 0.0;         // グライドにかける時間 (内部レート)。0 ならなし
    AdsrCoefficients amp;
    FilterControl filter;
    TransientControl transient;
    float velocityToAmp = 1.0f;    // 0 = ベロシティを無視、1 = ベロシティに比例
    double pitchBendSemitones = 0.0;   // 全ボイス共通 (スムージング済み)
    double vibratoCents = 0.0;         // 共通 LFO × Depth × CC1
    double vibratoDelaySamples = 0.0;  // ノートオンからこの時間は Vibrato なし
    double vibratoFadeSamples = 1.0;   // その後この時間でなだらかに立ち上げる
    double sampleRate = 44100.0;
};

/** 1 発音。Voice は自分で確保を行わず、すべての状態を固定長で持つ。 */
class Voice
{
public:
    void prepare (double sampleRate, std::uint32_t seedValue) noexcept;
    void reset() noexcept;

    /** 発音を開始する。発音中 (スティール) の場合は短いフェードアウトの後に開始する。 */
    void start (int midiNote, float velocity, std::uint64_t noteAge, const VoiceControl& control, std::uint32_t id = 0,
                double glideFromNote = -1.0) noexcept;

    /** 発音を続けたまま別のノートへ移る (Mono / Legato)。グライドが設定されていれば、いまの音程から滑らかに移る。
        retrigger = true ならエンベロープ・Transient・LFO の Fade を、いまのレベルから再始動する。 */
    void changeNote (int midiNote, float velocity, bool retrigger, std::uint64_t noteAge, const VoiceControl& control) noexcept;

    /** 鍵盤が離された。pedalDown ならサステイン保持、そうでなければ Release へ。 */
    void keyReleased (bool pedalDown) noexcept;

    /** サステインペダルが離された。保持中なら Release へ。 */
    void pedalReleased() noexcept;

    /** 鍵盤・ペダルの状態にかかわらず Release へ (All Notes Off)。 */
    void forceRelease() noexcept;

    /** 短いフェードで消音する (All Sound Off)。 */
    void kill() noexcept;

    /** コントロール周期の先頭でエンジンが呼ぶ。カットオフと Pitch Snap の目標値を更新する。 */
    void controlTick (const VoiceControl& control) noexcept { updateModulation (control, false); }

    /** numSamples (<= controlBlockSize) 分を left/right に加算する。 */
    void render (float* left, float* right, int numSamples, const VoiceControl& control, const VoiceLevels& levels) noexcept;

    bool isActive() const noexcept { return active; }
    bool isKeyDown() const noexcept { return keyDown; }
    bool isSustained() const noexcept { return sustained; }
    bool isFadingOut() const noexcept { return fadeSamplesLeft > 0; }

    /** スティール候補として「Release 中」とみなせるか。 */
    bool isReleasing() const noexcept { return active && ! isFadingOut() && amp.isReleasing(); }

    int getNote() const noexcept { return note; }

    /** Arpeggiator が生成したノートの内部 ID (鍵盤からの直接のノートは 0)。 */
    std::uint32_t getNoteId() const noexcept { return noteId; }
    std::uint64_t getAge() const noexcept { return age; }
    float getEnvelopeLevel() const noexcept { return amp.getLevel(); }

    /** 単音あたりの内部ヘッドルーム (docs/DECISIONS.md D-018)。 */
    static constexpr float voiceGain = 0.18f;

private:
    void beginNote (const VoiceControl& control) noexcept;
    void updateModulation (const VoiceControl& control, bool jump) noexcept;
    void releaseEnvelopes() noexcept;
    void renderChunk (float* left, float* right, int numSamples, const VoiceControl& control, const VoiceLevels& levels, int offset) noexcept;
    void beginGlide (double fromPitch, const VoiceControl& control) noexcept;

    UnisonOscillator oscA;
    UnisonOscillator oscB;
    NoiseBurst noiseBurst;
    StereoFilter filter;
    Adsr amp;
    Adsr filterEnv;

    // カットオフ: コントロール周期ごとに目標を決め、その間はサンプル単位で線形補間する
    double cutoffLagOctaves = 0.0;
    float filterG = 0.0f;
    float filterGStep = 0.0f;
    int filterRampLeft = 0;

    double pitchSnapOffset = 0.0;      // semitones。OSC A/B 共通 (Pitch Snap + Bend + Vibrato + LFO)

    // グライド: glidePitch が note へ向かってコントロール周期ごとに進む。グライドしていなければ note と等しい
    double glidePitch = 0.0;
    double glideStep = 0.0;
    int glideTicksLeft = 0;
    double pendingGlideFrom = -1.0;

    // Sub / Noise
    double subPhase = 0.0;
    std::uint32_t noiseState = 0x2545f491u;
    float noiseLowPass = 0.0f;

    // LFO (ボイスごとの値。コントロール周期で更新)
    std::uint32_t lfoSeed = 1;
    double lfoPulseWidth = 0.0;        // Pulse Width への加算分
    float ampMod = 1.0f;               // トレモロのゲイン (サンプル単位でなだらかに追従)
    float ampModTarget = 1.0f;
    std::int64_t samplesSinceStart = 0;

    bool active = false;
    bool keyDown = false;
    bool sustained = false;
    bool pendingStart = false;   // フェードアウト後に新しいノートを開始する

    int note = -1;
    std::uint32_t noteId = 0;
    float velocity = 0.0f;
    float gainVelocity = 0.0f;     // 音量に使うベロシティ。ノートの開始で velocity に合わせ、鳴っている途中の変更は数 ms でなめらかに追う
    std::uint64_t age = 0;

    int fadeLength = 96;
    int fadeSamplesLeft = 0;
};

} // namespace polybrisk
