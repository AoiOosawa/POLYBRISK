#include "state/Parameters.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace polybrisk::params
{

namespace
{
// 順序は polybrisk::Waveform と一致させる (SPEC §4.1)
constexpr std::array<const char*, 7> waveChoices { "Saw", "SuperSAW", "Pulse", "Square", "Triangle", "Sine", "Noise" };
constexpr std::array<const char*, 2> phaseModeChoices { "Free", "Retrigger" };

// 順序は polybrisk::TransientMode / FilterCharacter / FilterMode と一致させる (SPEC §5 / §6.1)
constexpr std::array<const char*, 3> transientModeChoices { "Noise Burst", "Pitch Snap", "Both" };
constexpr std::array<const char*, 3> filterCharacterChoices { "Analog", "Clean", "Punch" };
constexpr std::array<const char*, 5> filterModeChoices { "LP12", "LP24", "HP12", "HP24", "BP12" };
constexpr std::array<const char*, 2> bendModeChoices { "Symmetric", "Independent" };

// 順序は fx/FxParams.h の enum と一致させる (SPEC §8)
constexpr std::array<const char*, 6> driveTypeChoices { "Warm Saturation", "Tape Saturation", "Tube Saturation", "Soft Clip", "Hard Clip", "Wavefold" };
constexpr std::array<const char*, 2> chorusTypeChoices { "Classic", "Wide" };
constexpr std::array<const char*, 2> delayModeChoices { "Stereo", "Ping Pong" };
constexpr std::array<const char*, 9> delayDivisionChoices { "1/4", "1/4 D", "1/4 T", "1/8", "1/8 D", "1/8 T", "1/16", "1/16 D", "1/16 T" };
constexpr std::array<const char*, 3> reverbTypeChoices { "Room", "Plate", "Hall" };

// RHYTHM (追加仕様 §3.1 / §4.1)。順序は gateRateBeats / arpRateBeats / ArpMode と一致させる
constexpr std::array<const char*, 3> gateStepsChoices { "8", "16", "32" };
constexpr std::array<const char*, 6> gateRateChoices { "1/8", "1/16", "1/32", "1/8T", "1/16T", "1/64" };   // 1/64 は後から追加 (末尾)
constexpr std::array<const char*, 2> restartChoices { "Transport", "Note On" };
constexpr std::array<const char*, 5> arpModeChoices { "Up", "Down", "Up-Down", "Random", "As Played" };
constexpr std::array<const char*, 6> arpRateChoices { "1/4", "1/8", "1/16", "1/32", "1/8T", "1/16T" };

// D-041。順序は polybrisk::VoiceMode / SubWave / LfoWave / PhaserType、lfoDivisionBeats と一致させる
constexpr std::array<const char*, 3> voiceModeChoices { "Poly", "Mono", "Legato" };
constexpr std::array<const char*, 2> subOctaveChoices { "-1 Oct", "-2 Oct" };
constexpr std::array<const char*, 2> subWaveChoices { "Sine", "Square" };
constexpr std::array<const char*, 5> lfoWaveChoices { "Sine", "Triangle", "Saw", "Square", "S&H" };
constexpr std::array<const char*, 12> lfoDivisionChoices { "4 Bars", "2 Bars", "1 Bar", "1/2", "1/4", "1/8", "1/16", "1/4 T", "1/8 T", "1/16 T", "1/4 D", "1/8 D" };
constexpr std::array<const char*, 2> phaserTypeChoices { "Phaser", "Flanger" };

constexpr Definition toggle (const char* parameterId, const char* name, bool defaultOn)
{
    return { parameterId, name, Kind::toggle, 0.0f, 1.0f, defaultOn ? 1.0f : 0.0f, 0.0f, "", {}, false };
}

constexpr Definition integer (const char* parameterId, const char* name, int min, int max, int defaultValue, const char* unit = "")
{
    return { parameterId, name, Kind::integer, static_cast<float> (min), static_cast<float> (max),
             static_cast<float> (defaultValue), 0.0f, unit, {}, false };
}

constexpr Definition continuous (const char* parameterId, const char* name, float min, float max, float defaultValue,
                                 const char* unit, float skewCentre = 0.0f)
{
    return { parameterId, name, Kind::continuous, min, max, defaultValue, skewCentre, unit, {}, true };
}

// 連続値だがマクロの割当先にはしないもの。Output Gain はユーザーが決める全体の音量で、プリセット (とそのマクロ) からは
// 動かせないようにする (D-055 / D-068)。割当可にすると、取り込んだプリセットがマクロ経由で音量を最大 +12 dB まで上げられる
constexpr Definition userOnly (const char* parameterId, const char* name, float min, float max, float defaultValue, const char* unit)
{
    return { parameterId, name, Kind::continuous, min, max, defaultValue, 0.0f, unit, {}, false };
}

// 連続値だがマクロの割当先にはしないもの (マクロ自身)
constexpr Definition macroValue (const char* parameterId, const char* name)
{
    return { parameterId, name, Kind::continuous, 0.0f, 100.0f, 50.0f, 0.0f, "%", {}, false };
}

template <std::size_t N>
constexpr Definition choice (const char* parameterId, const char* name, const std::array<const char*, N>& items, int defaultIndex)
{
    return { parameterId, name, Kind::choice, 0.0f, static_cast<float> (N - 1), static_cast<float> (defaultIndex), 0.0f, "", items, false };
}

// 範囲と既定値は SPEC §4.2 / §5 / §6.1 / §7.1。[暫定] の採用は docs/DECISIONS.md を参照。
// 要素数は to_array に数えさせる (手で数えると、足りない分が空の定義として紛れ込む)
constexpr auto definitions = std::to_array<Definition> ({
    // --- OSC A ---
    toggle     (id::oscA.enabled,      "OSC A Enabled",       true),
    choice     (id::oscA.wave,         "OSC A Wave",          waveChoices, 0),
    integer    (id::oscA.octave,       "OSC A Octave",        -2, 2, 0),
    integer    (id::oscA.semitone,     "OSC A Semitone",      -12, 12, 0, "st"),
    continuous (id::oscA.fine,         "OSC A Fine",          -100.0f, 100.0f, 0.0f, "cent"),
    continuous (id::oscA.level,        "OSC A Level",         0.0f, 100.0f, 100.0f, "%"),
    integer    (id::oscA.unisonVoices, "OSC A Unison Voices", 1, 9, 1),
    continuous (id::oscA.detune,       "OSC A Detune",        0.0f, 100.0f, 35.0f, "%"),
    continuous (id::oscA.spread,       "OSC A Stereo Spread", 0.0f, 100.0f, 50.0f, "%"),
    choice     (id::oscA.phaseMode,    "OSC A Phase Mode",    phaseModeChoices, 1),
    continuous (id::oscA.pulseWidth,   "OSC A Pulse Width",   5.0f, 95.0f, 50.0f, "%"),
    continuous (id::oscA.unisonMix,    "OSC A Unison Mix",    0.0f, 100.0f, 50.0f, "%"),

    // --- OSC B ---
    toggle     (id::oscB.enabled,      "OSC B Enabled",       false),
    choice     (id::oscB.wave,         "OSC B Wave",          waveChoices, 0),
    integer    (id::oscB.octave,       "OSC B Octave",        -2, 2, 0),
    integer    (id::oscB.semitone,     "OSC B Semitone",      -12, 12, 0, "st"),
    continuous (id::oscB.fine,         "OSC B Fine",          -100.0f, 100.0f, 0.0f, "cent"),
    continuous (id::oscB.level,        "OSC B Level",         0.0f, 100.0f, 70.0f, "%"),
    integer    (id::oscB.unisonVoices, "OSC B Unison Voices", 1, 9, 1),
    continuous (id::oscB.detune,       "OSC B Detune",        0.0f, 100.0f, 35.0f, "%"),
    continuous (id::oscB.spread,       "OSC B Stereo Spread", 0.0f, 100.0f, 50.0f, "%"),
    choice     (id::oscB.phaseMode,    "OSC B Phase Mode",    phaseModeChoices, 1),
    continuous (id::oscB.pulseWidth,   "OSC B Pulse Width",   5.0f, 95.0f, 50.0f, "%"),
    continuous (id::oscB.unisonMix,    "OSC B Unison Mix",    0.0f, 100.0f, 50.0f, "%"),

    // --- Amp Envelope (時間は ms、対数的な操作感のためスキューを付ける) ---
    continuous (id::ampAttack,  "Amp Attack",  0.1f, 10000.0f, 2.0f,   "ms", 100.0f),
    continuous (id::ampDecay,   "Amp Decay",   1.0f, 10000.0f, 300.0f, "ms", 300.0f),
    continuous (id::ampSustain, "Amp Sustain", 0.0f, 100.0f,   80.0f,  "%"),
    continuous (id::ampRelease, "Amp Release", 1.0f, 20000.0f, 200.0f, "ms", 400.0f),
    continuous (id::ampVelocity, "Amp Velocity", 0.0f, 100.0f, 100.0f, "%"),

    // --- Transient ---
    toggle     (id::transientEnabled,    "Transient Enabled",     false),
    choice     (id::transientMode,       "Transient Mode",        transientModeChoices, 0),
    continuous (id::transientAmount,     "Transient Amount",      0.0f, 100.0f, 50.0f, "%"),
    continuous (id::transientDuration,   "Transient Duration",    1.0f, 150.0f, 20.0f, "ms", 30.0f),
    continuous (id::transientNoiseColor, "Transient Noise Color", 0.0f, 100.0f, 50.0f, "%"),
    continuous (id::transientPitchSnap,  "Transient Pitch Snap",  -24.0f, 24.0f, 12.0f, "st"),

    // --- Filter (Cutoff はスキューで対数に近い操作感にする) ---
    choice     (id::filterCharacter, "Filter Character",  filterCharacterChoices, 0),
    choice     (id::filterMode,      "Filter Mode",       filterModeChoices, 1),
    continuous (id::filterCutoff,    "Filter Cutoff",     20.0f, 20000.0f, 20000.0f, "Hz", 632.0f),
    continuous (id::filterResonance, "Filter Resonance",  0.0f, 100.0f, 0.0f, "%"),
    continuous (id::filterDrive,     "Filter Drive",      0.0f, 100.0f, 0.0f, "%"),
    continuous (id::filterEnvAmount, "Filter Env Amount", -100.0f, 100.0f, 0.0f, "%"),
    continuous (id::filterKeyTrack,  "Filter Key Track",  0.0f, 100.0f, 0.0f, "%"),
    continuous (id::filterVelocity,  "Filter Velocity",   0.0f, 100.0f, 0.0f, "%"),

    // --- Filter Envelope ---
    continuous (id::filterEnvAttack,  "Filter Env Attack",  0.1f, 10000.0f, 2.0f,   "ms", 100.0f),
    continuous (id::filterEnvDecay,   "Filter Env Decay",   1.0f, 10000.0f, 300.0f, "ms", 300.0f),
    continuous (id::filterEnvSustain, "Filter Env Sustain", 0.0f, 100.0f,   30.0f,  "%"),
    continuous (id::filterEnvRelease, "Filter Env Release", 1.0f, 20000.0f, 200.0f, "ms", 400.0f),

    // --- Performance (SPEC §7.4 / §7.5) ---
    integer    (id::bendUp,       "Bend Up",       0, 48, 2, "st"),
    integer    (id::bendDown,     "Bend Down",     0, 48, 2, "st"),
    choice     (id::bendMode,     "Bend Mode",     bendModeChoices, 0),
    continuous (id::vibratoDepth, "Vibrato Depth", 0.0f, 200.0f, 35.0f, "cent"),
    continuous (id::vibratoRate,  "Vibrato Rate",  0.1f, 12.0f, 5.5f, "Hz"),
    continuous (id::vibratoDelay, "Vibrato Delay", 0.0f, 2000.0f, 0.0f, "ms", 400.0f),
    continuous (id::aftertouchVibrato, "Aftertouch Vibrato", 0.0f, 200.0f, 0.0f, "cent"),
    continuous (id::aftertouchCutoff,  "Aftertouch Cutoff",  -100.0f, 100.0f, 0.0f, "%"),

    // --- Voice: 発音モードとグライド (D-041) ---
    choice     (id::voiceMode,  "Voice Mode", voiceModeChoices, 0),
    continuous (id::voiceGlide, "Glide Time", 0.0f, 2000.0f, 0.0f, "ms", 200.0f),

    // --- Sub Oscillator / Noise (既定はレベル 0 = 鳴らない) ---
    continuous (id::subLevel,   "Sub Level",   0.0f, 100.0f, 0.0f, "%"),
    choice     (id::subOctave,  "Sub Octave",  subOctaveChoices, 0),
    choice     (id::subWave,    "Sub Wave",    subWaveChoices, 0),
    continuous (id::noiseLevel, "Noise Level", 0.0f, 100.0f, 0.0f, "%"),
    continuous (id::noiseColor, "Noise Color", 0.0f, 100.0f, 50.0f, "%"),

    // --- LFO (1 基、行き先は固定の 4 つ。既定は深さ 0 = 効かない) ---
    choice     (id::lfoWave,       "LFO Wave",        lfoWaveChoices, 0),
    toggle     (id::lfoSync,       "LFO Sync",        false),
    continuous (id::lfoRate,       "LFO Rate",        0.05f, 20.0f, 2.0f, "Hz", 2.0f),
    choice     (id::lfoDivision,   "LFO Division",    lfoDivisionChoices, 2),
    continuous (id::lfoFade,       "LFO Fade In",     0.0f, 5000.0f, 0.0f, "ms", 800.0f),
    toggle     (id::lfoRetrigger,  "LFO Key Sync",    false),
    continuous (id::lfoCutoff,     "LFO to Cutoff",   -100.0f, 100.0f, 0.0f, "%"),
    continuous (id::lfoPulseWidth, "LFO to Pulse Width", 0.0f, 100.0f, 0.0f, "%"),
    continuous (id::lfoPitch,      "LFO to Pitch",    0.0f, 1200.0f, 0.0f, "cent", 100.0f),
    continuous (id::lfoAmp,        "LFO to Amp",      0.0f, 100.0f, 0.0f, "%"),

    // --- Macro (50% がニュートラル。SPEC §10) ---
    macroValue (id::macros[0], "Macro 1"),
    macroValue (id::macros[1], "Macro 2"),
    macroValue (id::macros[2], "Macro 3"),
    macroValue (id::macros[3], "Macro 4"),

    // --- FX (SPEC §8)。既定ではすべて OFF ---
    toggle     (id::fxBypass,       "FX Bypass",        false),

    toggle     (id::driveEnabled,   "Drive Enabled",    false),
    choice     (id::driveType,      "Drive Type",       driveTypeChoices, 0),
    continuous (id::driveDrive,     "Drive Amount",     0.0f, 100.0f, 30.0f, "%"),
    continuous (id::driveTone,      "Drive Tone",       0.0f, 100.0f, 100.0f, "%"),
    continuous (id::driveMix,       "Drive Mix",        0.0f, 100.0f, 100.0f, "%"),
    continuous (id::driveOutput,    "Drive Output",     -24.0f, 12.0f, 0.0f, "dB"),
    toggle     (id::driveAutoGain,  "Drive Auto Gain",  true),

    toggle     (id::chorusEnabled,  "Chorus Enabled",   false),
    choice     (id::chorusType,     "Chorus Type",      chorusTypeChoices, 0),
    continuous (id::chorusRate,     "Chorus Rate",      0.05f, 8.0f, 0.8f, "Hz", 1.0f),
    continuous (id::chorusDepth,    "Chorus Depth",     0.0f, 100.0f, 50.0f, "%"),
    continuous (id::chorusWidth,    "Chorus Width",     0.0f, 100.0f, 100.0f, "%"),
    continuous (id::chorusMix,      "Chorus Mix",       0.0f, 100.0f, 50.0f, "%"),

    toggle     (id::eqEnabled,      "EQ Enabled",       false),
    continuous (id::eqLowGain,      "EQ Low Gain",      -18.0f, 18.0f, 0.0f, "dB"),
    continuous (id::eqMidFreq,      "EQ Mid Freq",      200.0f, 8000.0f, 1000.0f, "Hz", 1265.0f),
    continuous (id::eqMidGain,      "EQ Mid Gain",      -18.0f, 18.0f, 0.0f, "dB"),
    continuous (id::eqHighGain,     "EQ High Gain",     -18.0f, 18.0f, 0.0f, "dB"),

    toggle     (id::imagerEnabled,  "Imager Enabled",   false),
    continuous (id::imagerWidth,    "Imager Width",     0.0f, 200.0f, 100.0f, "%"),
    toggle     (id::imagerMonoBass, "Imager Mono Bass", false),

    toggle     (id::delayEnabled,   "Delay Enabled",    false),
    choice     (id::delayMode,      "Delay Mode",       delayModeChoices, 0),
    toggle     (id::delaySync,      "Delay Sync",       true),
    choice     (id::delayDivision,  "Delay Division",   delayDivisionChoices, 4),
    continuous (id::delayTime,      "Delay Time",       1.0f, 2000.0f, 350.0f, "ms", 400.0f),
    continuous (id::delayFeedback,  "Delay Feedback",   0.0f, 95.0f, 35.0f, "%"),
    continuous (id::delayTone,      "Delay Tone",       0.0f, 100.0f, 60.0f, "%"),
    continuous (id::delayMix,       "Delay Mix",        0.0f, 100.0f, 25.0f, "%"),
    continuous (id::delayDucking,   "Delay Ducking",    0.0f, 100.0f, 0.0f, "%"),

    toggle     (id::reverbEnabled,  "Reverb Enabled",   false),
    choice     (id::reverbType,     "Reverb Type",      reverbTypeChoices, 1),
    continuous (id::reverbSize,     "Reverb Size",      0.0f, 100.0f, 50.0f, "%"),
    continuous (id::reverbDecay,    "Reverb Decay",     0.2f, 15.0f, 1.8f, "s", 2.0f),
    continuous (id::reverbPreDelay, "Reverb Pre-Delay", 0.0f, 200.0f, 10.0f, "ms"),
    continuous (id::reverbTone,     "Reverb Tone",      0.0f, 100.0f, 60.0f, "%"),
    continuous (id::reverbMix,      "Reverb Mix",       0.0f, 100.0f, 20.0f, "%"),

    toggle     (id::phaserEnabled,  "Phaser Enabled",   false),
    choice     (id::phaserType,     "Phaser Type",      phaserTypeChoices, 0),
    continuous (id::phaserRate,     "Phaser Rate",      0.02f, 8.0f, 0.4f, "Hz", 0.8f),
    continuous (id::phaserDepth,    "Phaser Depth",     0.0f, 100.0f, 60.0f, "%"),
    continuous (id::phaserFeedback, "Phaser Feedback",  0.0f, 90.0f, 40.0f, "%"),
    continuous (id::phaserMix,      "Phaser Mix",       0.0f, 100.0f, 50.0f, "%"),

    // --- RHYTHM: Trance Gate (既定 OFF)。パターンはホストパラメーターではなく META に保存する ---
    toggle     (id::gateEnabled, "Gate Enabled", false),
    choice     (id::gateSteps,   "Gate Steps",   gateStepsChoices, 1),
    choice     (id::gateRate,    "Gate Rate",    gateRateChoices, 2),   // 既定は 1/32 (既定パターン Shifted Long と対。D-044)
    continuous (id::gateDepth,   "Gate Depth",   0.0f, 100.0f, 100.0f, "%"),
    continuous (id::gateSmooth,  "Gate Smooth",  0.5f, 30.0f, 3.0f, "ms"),
    continuous (id::gateSwing,   "Gate Swing",   0.0f, 50.0f, 0.0f, "%"),
    choice     (id::gateRestart, "Gate Restart", restartChoices, 0),
    toggle     (id::gateStereo,  "Gate Stereo",  false),
    continuous (id::gateWidth,   "Gate Width",   0.0f, 100.0f, 100.0f, "%"),

    // --- RHYTHM: Arpeggiator (既定 OFF) ---
    toggle     (id::arpEnabled,  "Arp Enabled",  false),
    choice     (id::arpMode,     "Arp Mode",     arpModeChoices, 0),
    choice     (id::arpRate,     "Arp Rate",     arpRateChoices, 2),
    integer    (id::arpOctaves,  "Arp Octaves",  1, 4, 1),
    continuous (id::arpGate,     "Arp Gate Length", 10.0f, 100.0f, 65.0f, "%"),
    continuous (id::arpSwing,    "Arp Swing",    0.0f, 50.0f, 0.0f, "%"),
    continuous (id::arpVelocity, "Arp Velocity", 25.0f, 200.0f, 100.0f, "%"),
    toggle     (id::arpLatch,    "Arp Latch",    false),
    choice     (id::arpRestart,  "Arp Restart",  restartChoices, 1),

    // --- Output ---
    userOnly   (id::outputGain, "Output Gain", -60.0f, 12.0f, 0.0f, "dB"),

    // --- 音色ごとの音量の補正 (D-055)。Output Gain はユーザーのもので、プリセットでは変えない ---
    continuous (id::presetLevel, "Preset Level", -24.0f, 24.0f, 0.0f, "dB"),
});
} // namespace

std::span<const Definition> all() noexcept
{
    return definitions;
}

const Definition* find (juce::StringRef parameterId) noexcept
{
    for (const auto& d : definitions)
        if (parameterId == juce::StringRef (d.id))
            return &d;

    return nullptr;
}

double gateRateBeats (int choiceIndex) noexcept
{
    constexpr double beats[] = { 0.5, 0.25, 0.125, 1.0 / 3.0, 1.0 / 6.0, 0.0625 };
    return beats[std::clamp (choiceIndex, 0, 5)];
}

double lfoDivisionBeats (int choiceIndex) noexcept
{
    constexpr double beats[] = { 16.0, 8.0, 4.0, 2.0, 1.0, 0.5, 0.25, 2.0 / 3.0, 1.0 / 3.0, 1.0 / 6.0, 1.5, 0.75 };
    return beats[std::clamp (choiceIndex, 0, 11)];
}

double arpRateBeats (int choiceIndex) noexcept
{
    constexpr double beats[] = { 1.0, 0.5, 0.25, 0.125, 1.0 / 3.0, 1.0 / 6.0 };
    return beats[std::clamp (choiceIndex, 0, 5)];
}

int gateStepCount (int choiceIndex) noexcept
{
    constexpr int counts[] = { 8, 16, 32 };
    return counts[std::clamp (choiceIndex, 0, 2)];
}

int indexOf (juce::StringRef parameterId) noexcept
{
    for (std::size_t i = 0; i < definitions.size(); ++i)
        if (parameterId == juce::StringRef (definitions[i].id))
            return static_cast<int> (i);

    return -1;
}

namespace
{
// juce::NormalisableRange::setSkewForCentre と同じ式
float skewFor (const Definition& d) noexcept
{
    if (d.skewCentre > d.min && d.skewCentre < d.max)
        return std::log (0.5f) / std::log ((d.skewCentre - d.min) / (d.max - d.min));

    return 1.0f;
}
} // namespace

float toNormalised (const Definition& d, float realValue) noexcept
{
    const float proportion = std::clamp ((realValue - d.min) / (d.max - d.min), 0.0f, 1.0f);
    const float skew = skewFor (d);
    return juce::exactlyEqual (skew, 1.0f) ? proportion : std::pow (proportion, skew);
}

float fromNormalised (const Definition& d, float normalised) noexcept
{
    float proportion = std::clamp (normalised, 0.0f, 1.0f);
    const float skew = skewFor (d);

    if (! juce::exactlyEqual (skew, 1.0f) && proportion > 0.0f)
        proportion = std::exp (std::log (proportion) / skew);

    return d.min + (d.max - d.min) * proportion;
}

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    for (const auto& d : definitions)
    {
        const juce::ParameterID pid { d.id, versionHint };

        switch (d.kind)
        {
            case Kind::choice:
            {
                juce::StringArray items;
                for (const auto* c : d.choices)
                    items.add (c);

                layout.add (std::make_unique<juce::AudioParameterChoice> (pid, d.name, items, static_cast<int> (d.defaultValue)));
                break;
            }

            case Kind::toggle:
                layout.add (std::make_unique<juce::AudioParameterBool> (pid, d.name, d.defaultValue >= 0.5f));
                break;

            case Kind::integer:
                layout.add (std::make_unique<juce::AudioParameterInt> (
                    pid, d.name, static_cast<int> (d.min), static_cast<int> (d.max), static_cast<int> (d.defaultValue),
                    juce::AudioParameterIntAttributes().withLabel (d.unit)));
                break;

            case Kind::continuous:
            {
                juce::NormalisableRange<float> range { d.min, d.max };
                if (d.skewCentre > d.min && d.skewCentre < d.max)
                    range.setSkewForCentre (d.skewCentre);

                layout.add (std::make_unique<juce::AudioParameterFloat> (
                    pid, d.name, range, d.defaultValue,
                    juce::AudioParameterFloatAttributes().withLabel (d.unit)));
                break;
            }
        }
    }

    return layout;
}

} // namespace polybrisk::params
