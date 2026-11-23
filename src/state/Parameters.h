#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <span>

/** パラメーターの単一レジストリ (CLAUDE.md ルール6 / SPEC §19)。
    Parameter ID・範囲・既定値はここだけで定義し、APVTS レイアウト、
    プリセット JSON のキー、docs/PARAMETER_IDS.md はここから導く。
    公開後は ID を変更・削除しない。 */
namespace polybrisk::params
{

/** APVTS の ParameterID に付与するバージョンヒント (AU のパラメーター順序安定化)。
    パラメーターを追加するリリースでは、新規分にのみ大きい値を与える。 */
inline constexpr int versionHint = 1;

struct OscIds
{
    const char* enabled;
    const char* wave;
    const char* octave;
    const char* semitone;
    const char* fine;
    const char* level;
    const char* unisonVoices;
    const char* detune;
    const char* spread;
    const char* phaseMode;
    const char* pulseWidth;
    const char* unisonMix;      // D-045 で追加
};

namespace id
{
inline constexpr OscIds oscA { "osc.a.enabled", "osc.a.wave", "osc.a.octave", "osc.a.semitone", "osc.a.fine", "osc.a.level",
                               "osc.a.unisonVoices", "osc.a.detune", "osc.a.spread", "osc.a.phaseMode", "osc.a.pulseWidth", "osc.a.unisonMix" };
inline constexpr OscIds oscB { "osc.b.enabled", "osc.b.wave", "osc.b.octave", "osc.b.semitone", "osc.b.fine", "osc.b.level",
                               "osc.b.unisonVoices", "osc.b.detune", "osc.b.spread", "osc.b.phaseMode", "osc.b.pulseWidth", "osc.b.unisonMix" };

inline constexpr const char* ampAttack = "amp.attack";
inline constexpr const char* ampDecay = "amp.decay";
inline constexpr const char* ampSustain = "amp.sustain";
inline constexpr const char* ampRelease = "amp.release";
inline constexpr const char* ampVelocity = "amp.velocity";

inline constexpr const char* transientEnabled = "transient.enabled";
inline constexpr const char* transientMode = "transient.mode";
inline constexpr const char* transientAmount = "transient.amount";
inline constexpr const char* transientDuration = "transient.duration";
inline constexpr const char* transientNoiseColor = "transient.noiseColor";
inline constexpr const char* transientPitchSnap = "transient.pitchSnap";

inline constexpr const char* filterCharacter = "filter.character";
inline constexpr const char* filterMode = "filter.mode";
inline constexpr const char* filterCutoff = "filter.cutoff";
inline constexpr const char* filterResonance = "filter.resonance";
inline constexpr const char* filterDrive = "filter.drive";
inline constexpr const char* filterEnvAmount = "filter.envAmount";
inline constexpr const char* filterKeyTrack = "filter.keyTrack";
inline constexpr const char* filterVelocity = "filter.velocity";

inline constexpr const char* filterEnvAttack = "filterEnv.attack";
inline constexpr const char* filterEnvDecay = "filterEnv.decay";
inline constexpr const char* filterEnvSustain = "filterEnv.sustain";
inline constexpr const char* filterEnvRelease = "filterEnv.release";

inline constexpr const char* bendUp = "performance.bendUp";
inline constexpr const char* bendDown = "performance.bendDown";
inline constexpr const char* bendMode = "performance.bendMode";
inline constexpr const char* vibratoDepth = "performance.vibratoDepth";
inline constexpr const char* vibratoRate = "performance.vibratoRate";
inline constexpr const char* vibratoDelay = "performance.vibratoDelay";

inline constexpr const char* aftertouchVibrato = "performance.aftertouchVibrato";
inline constexpr const char* aftertouchCutoff = "performance.aftertouchCutoff";

// --- Voice / Sub / Noise / LFO (D-041) ---
inline constexpr const char* voiceMode = "voice.mode";
inline constexpr const char* voiceGlide = "voice.glide";

inline constexpr const char* subLevel = "sub.level";
inline constexpr const char* subOctave = "sub.octave";
inline constexpr const char* subWave = "sub.wave";
inline constexpr const char* noiseLevel = "noise.level";
inline constexpr const char* noiseColor = "noise.color";

inline constexpr const char* lfoWave = "lfo.wave";
inline constexpr const char* lfoSync = "lfo.sync";
inline constexpr const char* lfoRate = "lfo.rate";
inline constexpr const char* lfoDivision = "lfo.division";
inline constexpr const char* lfoFade = "lfo.fade";
inline constexpr const char* lfoRetrigger = "lfo.retrigger";
inline constexpr const char* lfoCutoff = "lfo.cutoff";
inline constexpr const char* lfoPulseWidth = "lfo.pulseWidth";
inline constexpr const char* lfoPitch = "lfo.pitch";
inline constexpr const char* lfoAmp = "lfo.amp";

inline constexpr const char* macros[4] = { "macro.1", "macro.2", "macro.3", "macro.4" };

// --- FX (SPEC §8) ---
inline constexpr const char* fxBypass = "fx.bypass";

inline constexpr const char* driveEnabled = "fx.drive.enabled";
inline constexpr const char* driveType = "fx.drive.type";
inline constexpr const char* driveDrive = "fx.drive.drive";
inline constexpr const char* driveTone = "fx.drive.tone";
inline constexpr const char* driveMix = "fx.drive.mix";
inline constexpr const char* driveOutput = "fx.drive.output";
inline constexpr const char* driveAutoGain = "fx.drive.autoGain";

inline constexpr const char* chorusEnabled = "fx.chorus.enabled";
inline constexpr const char* chorusType = "fx.chorus.type";
inline constexpr const char* chorusRate = "fx.chorus.rate";
inline constexpr const char* chorusDepth = "fx.chorus.depth";
inline constexpr const char* chorusWidth = "fx.chorus.width";
inline constexpr const char* chorusMix = "fx.chorus.mix";

inline constexpr const char* eqEnabled = "fx.eq.enabled";
inline constexpr const char* eqLowGain = "fx.eq.lowGain";
inline constexpr const char* eqMidFreq = "fx.eq.midFreq";
inline constexpr const char* eqMidGain = "fx.eq.midGain";
inline constexpr const char* eqHighGain = "fx.eq.highGain";

inline constexpr const char* imagerEnabled = "fx.imager.enabled";
inline constexpr const char* imagerWidth = "fx.imager.width";
inline constexpr const char* imagerMonoBass = "fx.imager.monoBass";

inline constexpr const char* delayEnabled = "fx.delay.enabled";
inline constexpr const char* delayMode = "fx.delay.mode";
inline constexpr const char* delaySync = "fx.delay.sync";
inline constexpr const char* delayDivision = "fx.delay.division";
inline constexpr const char* delayTime = "fx.delay.time";
inline constexpr const char* delayFeedback = "fx.delay.feedback";
inline constexpr const char* delayTone = "fx.delay.tone";
inline constexpr const char* delayMix = "fx.delay.mix";
inline constexpr const char* delayDucking = "fx.delay.ducking";

inline constexpr const char* reverbEnabled = "fx.reverb.enabled";
inline constexpr const char* reverbType = "fx.reverb.type";
inline constexpr const char* reverbSize = "fx.reverb.size";
inline constexpr const char* reverbDecay = "fx.reverb.decay";
inline constexpr const char* reverbPreDelay = "fx.reverb.preDelay";
inline constexpr const char* reverbTone = "fx.reverb.tone";
inline constexpr const char* reverbMix = "fx.reverb.mix";

inline constexpr const char* phaserEnabled = "fx.phaser.enabled";
inline constexpr const char* phaserType = "fx.phaser.type";
inline constexpr const char* phaserRate = "fx.phaser.rate";
inline constexpr const char* phaserDepth = "fx.phaser.depth";
inline constexpr const char* phaserFeedback = "fx.phaser.feedback";
inline constexpr const char* phaserMix = "fx.phaser.mix";

// --- RHYTHM: Trance Gate / Arpeggiator (docs/ADDON_TRANCE_GATE_ARPEGGIATOR.md) ---
inline constexpr const char* gateEnabled = "gate.enabled";
inline constexpr const char* gateSteps = "gate.steps";
inline constexpr const char* gateRate = "gate.rate";
inline constexpr const char* gateDepth = "gate.depth";
inline constexpr const char* gateSmooth = "gate.smooth";
inline constexpr const char* gateSwing = "gate.swing";
inline constexpr const char* gateRestart = "gate.restart";
inline constexpr const char* gateStereo = "gate.stereo";
inline constexpr const char* gateWidth = "gate.width";
inline constexpr const char* arpEnabled = "arp.enabled";
inline constexpr const char* arpMode = "arp.mode";
inline constexpr const char* arpRate = "arp.rate";
inline constexpr const char* arpOctaves = "arp.octaves";
inline constexpr const char* arpGate = "arp.gate";
inline constexpr const char* arpSwing = "arp.swing";
inline constexpr const char* arpVelocity = "arp.velocity";
inline constexpr const char* arpLatch = "arp.latch";
inline constexpr const char* arpRestart = "arp.restart";

inline constexpr const char* outputGain = "output.gain";     // 全体の音量。プリセットには保存しない (D-055)
inline constexpr const char* presetLevel = "preset.level";   // 音色ごとの音量の補正。プリセットに保存する (D-055)
} // namespace id

enum class Kind
{
    continuous, // float。マクロ割当の対象になり得る
    integer,    // 整数ステップ
    choice,     // 離散選択
    toggle      // ON / OFF
};

struct Definition
{
    const char* id;
    const char* name;
    Kind kind;
    float min;
    float max;
    float defaultValue;       // choice は選択肢インデックス、toggle は 0 / 1
    float skewCentre;         // 0 なら線形。それ以外はノブ中央に来る値
    const char* unit;
    std::span<const char* const> choices;
    bool macroAssignable;
};

/** 実装済みの全パラメーター定義。 */
std::span<const Definition> all() noexcept;

/** ID から定義を引く。見つからなければ nullptr。 */
const Definition* find (juce::StringRef parameterId) noexcept;

/** Rate の選択肢 (インデックス) を 1 ステップの長さ (拍) に変換する。 */
double gateRateBeats (int choiceIndex) noexcept;
double arpRateBeats (int choiceIndex) noexcept;

/** lfo.division の選択肢 (インデックス) を LFO 1 周期の長さ (拍) に変換する。 */
double lfoDivisionBeats (int choiceIndex) noexcept;

/** gate.steps の選択肢 (インデックス) をステップ数 (8 / 16 / 32) に変換する。 */
int gateStepCount (int choiceIndex) noexcept;

/** レジストリ内の位置 (all() の添字)。見つからなければ -1。 */
int indexOf (juce::StringRef parameterId) noexcept;

/** 実値 ⇄ 正規化値 (0..1)。APVTS のパラメーターと同じスキューを使う。確保なし・リアルタイム安全。 */
float toNormalised (const Definition& definition, float realValue) noexcept;
float fromNormalised (const Definition& definition, float normalised) noexcept;

/** レジストリから APVTS レイアウトを生成する。 */
juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

} // namespace polybrisk::params
