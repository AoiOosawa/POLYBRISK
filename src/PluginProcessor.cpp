#include "PluginProcessor.h"

#include "PluginEditor.h"
#include "state/Parameters.h"
#include "state/StateSerializer.h"

namespace
{
constexpr double macroSmoothingSeconds = 0.010;
} // namespace

PolyBriskAudioProcessor::PolyBriskAudioProcessor (const juce::File& presetRoot)
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "APVTS", polybrisk::params::createLayout()),
      presets (apvts, meta, presetRoot)
{
    namespace ids = polybrisk::params::id;

    const auto definitions = polybrisk::params::all();
    jassert (static_cast<int> (definitions.size()) <= maxParameters);

    for (size_t i = 0; i < definitions.size(); ++i)
    {
        auto* value = apvts.getRawParameterValue (definitions[i].id);
        jassert (value != nullptr);
        rawValues.push_back (value);
        effective[i] = definitions[i].defaultValue;
        effectiveForUi[i].store (definitions[i].defaultValue);
    }

    const auto at = [] (const char* parameterId)
    {
        const int i = polybrisk::params::indexOf (parameterId);
        jassert (i >= 0);
        return i;
    };

    const auto osc = [&at] (const polybrisk::params::OscIds& o)
    {
        return OscIndex { at (o.enabled), at (o.wave), at (o.octave), at (o.semitone), at (o.fine), at (o.level),
                          at (o.unisonVoices), at (o.detune), at (o.spread), at (o.phaseMode), at (o.pulseWidth), at (o.unisonMix) };
    };

    index.oscA = osc (ids::oscA);
    index.oscB = osc (ids::oscB);
    index.ampAttack = at (ids::ampAttack);
    index.ampDecay = at (ids::ampDecay);
    index.ampSustain = at (ids::ampSustain);
    index.ampRelease = at (ids::ampRelease);
    index.ampVelocity = at (ids::ampVelocity);
    index.transientEnabled = at (ids::transientEnabled);
    index.transientMode = at (ids::transientMode);
    index.transientAmount = at (ids::transientAmount);
    index.transientDuration = at (ids::transientDuration);
    index.transientNoiseColor = at (ids::transientNoiseColor);
    index.transientPitchSnap = at (ids::transientPitchSnap);
    index.filterCharacter = at (ids::filterCharacter);
    index.filterMode = at (ids::filterMode);
    index.filterCutoff = at (ids::filterCutoff);
    index.filterResonance = at (ids::filterResonance);
    index.filterDrive = at (ids::filterDrive);
    index.filterEnvAmount = at (ids::filterEnvAmount);
    index.filterKeyTrack = at (ids::filterKeyTrack);
    index.filterVelocity = at (ids::filterVelocity);
    index.filterEnvAttack = at (ids::filterEnvAttack);
    index.filterEnvDecay = at (ids::filterEnvDecay);
    index.filterEnvSustain = at (ids::filterEnvSustain);
    index.filterEnvRelease = at (ids::filterEnvRelease);
    index.bendUp = at (ids::bendUp);
    index.bendDown = at (ids::bendDown);
    index.bendMode = at (ids::bendMode);
    index.vibratoDepth = at (ids::vibratoDepth);
    index.vibratoRate = at (ids::vibratoRate);
    index.vibratoDelay = at (ids::vibratoDelay);
    index.outputGain = at (ids::outputGain);
    index.presetLevel = at (ids::presetLevel);
    index.aftertouchVibrato = at (ids::aftertouchVibrato);
    index.aftertouchCutoff = at (ids::aftertouchCutoff);
    index.voiceMode = at (ids::voiceMode);
    index.voiceGlide = at (ids::voiceGlide);
    index.subLevel = at (ids::subLevel);
    index.subOctave = at (ids::subOctave);
    index.subWave = at (ids::subWave);
    index.noiseLevel = at (ids::noiseLevel);
    index.noiseColor = at (ids::noiseColor);
    index.lfoWave = at (ids::lfoWave);
    index.lfoSync = at (ids::lfoSync);
    index.lfoRate = at (ids::lfoRate);
    index.lfoDivision = at (ids::lfoDivision);
    index.lfoFade = at (ids::lfoFade);
    index.lfoRetrigger = at (ids::lfoRetrigger);
    index.lfoCutoff = at (ids::lfoCutoff);
    index.lfoPulseWidth = at (ids::lfoPulseWidth);
    index.lfoPitch = at (ids::lfoPitch);
    index.lfoAmp = at (ids::lfoAmp);
    index.phaserEnabled = at (ids::phaserEnabled);
    index.phaserType = at (ids::phaserType);
    index.phaserRate = at (ids::phaserRate);
    index.phaserDepth = at (ids::phaserDepth);
    index.phaserFeedback = at (ids::phaserFeedback);
    index.phaserMix = at (ids::phaserMix);
    index.gateEnabled = at (ids::gateEnabled);
    index.gateSteps = at (ids::gateSteps);
    index.gateRate = at (ids::gateRate);
    index.gateDepth = at (ids::gateDepth);
    index.gateSmooth = at (ids::gateSmooth);
    index.gateSwing = at (ids::gateSwing);
    index.gateRestart = at (ids::gateRestart);
    index.gateStereo = at (ids::gateStereo);
    index.gateWidth = at (ids::gateWidth);
    index.arpEnabled = at (ids::arpEnabled);
    index.arpMode = at (ids::arpMode);
    index.arpRate = at (ids::arpRate);
    index.arpOctaves = at (ids::arpOctaves);
    index.arpGate = at (ids::arpGate);
    index.arpSwing = at (ids::arpSwing);
    index.arpVelocity = at (ids::arpVelocity);
    index.arpLatch = at (ids::arpLatch);
    index.arpRestart = at (ids::arpRestart);
    index.fxBypass = at (ids::fxBypass);
    index.driveEnabled = at (ids::driveEnabled);
    index.driveType = at (ids::driveType);
    index.driveDrive = at (ids::driveDrive);
    index.driveTone = at (ids::driveTone);
    index.driveMix = at (ids::driveMix);
    index.driveOutput = at (ids::driveOutput);
    index.driveAutoGain = at (ids::driveAutoGain);
    index.chorusEnabled = at (ids::chorusEnabled);
    index.chorusType = at (ids::chorusType);
    index.chorusRate = at (ids::chorusRate);
    index.chorusDepth = at (ids::chorusDepth);
    index.chorusWidth = at (ids::chorusWidth);
    index.chorusMix = at (ids::chorusMix);
    index.eqEnabled = at (ids::eqEnabled);
    index.eqLowGain = at (ids::eqLowGain);
    index.eqMidFreq = at (ids::eqMidFreq);
    index.eqMidGain = at (ids::eqMidGain);
    index.eqHighGain = at (ids::eqHighGain);
    index.imagerEnabled = at (ids::imagerEnabled);
    index.imagerWidth = at (ids::imagerWidth);
    index.imagerMonoBass = at (ids::imagerMonoBass);
    index.delayEnabled = at (ids::delayEnabled);
    index.delayMode = at (ids::delayMode);
    index.delaySync = at (ids::delaySync);
    index.delayDivision = at (ids::delayDivision);
    index.delayTime = at (ids::delayTime);
    index.delayFeedback = at (ids::delayFeedback);
    index.delayTone = at (ids::delayTone);
    index.delayMix = at (ids::delayMix);
    index.delayDucking = at (ids::delayDucking);
    index.reverbEnabled = at (ids::reverbEnabled);
    index.reverbType = at (ids::reverbType);
    index.reverbSize = at (ids::reverbSize);
    index.reverbDecay = at (ids::reverbDecay);
    index.reverbPreDelay = at (ids::reverbPreDelay);
    index.reverbTone = at (ids::reverbTone);
    index.reverbMix = at (ids::reverbMix);

    for (size_t m = 0; m < index.macros.size(); ++m)
    {
        index.macros[m] = at (ids::macros[m]);
        pendingMacroValue[m].store (-1.0f);
    }

    // Output Gain は音色の一部ではない (プリセットに保存しない) ので、動かしても Dirty にしない (D-055)
    for (const auto& d : definitions)
        if (juce::String (d.id) != ids::outputGain)
            apvts.addParameterListener (d.id, this);

    // マクロの割当・Gate パターンの編集は「音色の変更」。スナップショットを作り直し、Dirty にする
    meta.onChange = [this]
    {
        pushMacroSnapshot();
        meta.setDirty (true);
    };

    // プリセットを読んだら、Arp の生成ノートを終了させる (SPEC §12)。読込による変更は Dirty にしない
    presets.onPresetLoaded = [this]
    {
        arpResetRequested.store (true);
        parametersTouched.store (false);
    };
    pushMacroSnapshot();
    startTimerHz (30);
}

PolyBriskAudioProcessor::~PolyBriskAudioProcessor()
{
    stopTimer();
    presets.onPresetLoaded = nullptr;
    meta.onChange = nullptr;

    for (const auto& d : polybrisk::params::all())
        apvts.removeParameterListener (d.id, this);
}

void PolyBriskAudioProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    engine.prepare (currentSampleRate);
    macrosInitialised = false;
}

void PolyBriskAudioProcessor::releaseResources()
{
    engine.reset();
}

bool PolyBriskAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // 音声入力なし・ステレオ出力のみ (SPEC §1)
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
        && layouts.getMainInputChannelSet().isDisabled();
}

// ---------------------------------------------------------------------------
// マクロ設定の受け渡し
// ---------------------------------------------------------------------------
void PolyBriskAudioProcessor::pushMacroSnapshot()
{
    const auto snapshot = meta.buildSnapshot();   // ここで組み立て、ロック中はコピーだけ
    const auto pattern = meta.getGatePattern();

    {
        const juce::SpinLock::ScopedLockType sl (snapshotLock);
        pendingSnapshot = snapshot;
        pendingGatePattern = pattern;
    }

    snapshotDirty.store (true);
}

void PolyBriskAudioProcessor::beginMidiLearn (int macroIndex) noexcept
{
    learnTarget.store (macroIndex >= 0 && macroIndex < polybrisk::macro::numMacros ? macroIndex : -1);
}

void PolyBriskAudioProcessor::syncFromAudioThread()
{
    for (size_t m = 0; m < pendingMacroValue.size(); ++m)
    {
        const float value = pendingMacroValue[m].exchange (-1.0f);

        if (value >= 0.0f)
            if (auto* parameter = apvts.getParameter (polybrisk::params::id::macros[m]))
                parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
    }

    // パラメーターが動いていたら Dirty にする (プリセット名の横の * 表示)
    if (parametersTouched.exchange (false))
        meta.setDirty (true);

    const int learned = learnResult.exchange (-1);

    if (learned >= 0)
        meta.setMacroCc (learned >> 8, learned & 0xff);
}

float PolyBriskAudioProcessor::getEffectiveValue (int parameterIndex) const noexcept
{
    if (parameterIndex < 0 || parameterIndex >= static_cast<int> (rawValues.size()))
        return 0.0f;

    return effectiveForUi[static_cast<size_t> (parameterIndex)].load (std::memory_order_relaxed);
}

// ---------------------------------------------------------------------------
// オーディオスレッド
// ---------------------------------------------------------------------------
void PolyBriskAudioProcessor::computeEffectiveValues (int numSamples) noexcept
{
    namespace macro = polybrisk::macro;
    const auto definitions = polybrisk::params::all();

    // 新しいマクロ設定があれば取り込む。取れなければ次のブロックに回す (待たない)。
    if (snapshotDirty.load (std::memory_order_acquire))
    {
        const juce::SpinLock::ScopedTryLockType tryLock (snapshotLock);

        if (tryLock.isLocked())
        {
            activeSnapshot = pendingSnapshot;
            activeGatePattern = pendingGatePattern;
            snapshotDirty.store (false, std::memory_order_release);
        }
    }

    // Base 値
    for (size_t i = 0; i < rawValues.size(); ++i)
        effective[i] = rawValues[i]->load (std::memory_order_relaxed);

    // マクロの現在値 (CC で動かした直後は、パラメーターへ反映されるまで CC の値を使う)
    const float smoothing = static_cast<float> (1.0 - std::exp (-static_cast<double> (std::max (1, numSamples))
                                                                / (macroSmoothingSeconds * currentSampleRate)));
    std::array<float, macro::numMacros> macroValues {};

    for (size_t m = 0; m < macroValues.size(); ++m)
    {
        auto& runtime = macroRuntime[m];
        const float raw = effective[static_cast<size_t> (index.macros[m])];

        if (runtime.ccActive && ! juce::exactlyEqual (raw, runtime.rawAtCc))
            runtime.ccActive = false;   // ホスト / UI / 同期によってパラメーターが動いた

        const float target = runtime.ccActive ? runtime.ccValue : raw * 0.01f;

        if (! macrosInitialised)
            runtime.smoothed = target;
        else
            runtime.smoothed += (target - runtime.smoothed) * smoothing;

        // 目標にほぼ達したら目標値そのものにする (0.5 のニュートラルを厳密に保つ)
        if (std::abs (target - runtime.smoothed) < 1.0e-5f)
            runtime.smoothed = target;

        macroValues[m] = runtime.smoothed;
    }

    macrosInitialised = true;

    // Effective = clamp (Base + Σ delta)。正規化空間で合成してから実値へ戻す (SPEC §10.2)
    for (int e = 0; e < activeSnapshot.numEntries; ++e)
    {
        const auto& entry = activeSnapshot.entries[static_cast<size_t> (e)];
        const int target = entry.parameterIndex;

        if (target < 0 || target >= static_cast<int> (rawValues.size()))
            continue;

        // 同じパラメーターへの最初の割当のところで、全割当の合計をまとめて計算する
        bool first = true;
        for (int previous = 0; previous < e; ++previous)
            first = first && activeSnapshot.entries[static_cast<size_t> (previous)].parameterIndex != target;

        if (! first)
            continue;

        float sum = 0.0f;
        for (int other = e; other < activeSnapshot.numEntries; ++other)
        {
            const auto& o = activeSnapshot.entries[static_cast<size_t> (other)];
            if (o.parameterIndex == target)
                sum += macro::delta (o, macroValues[static_cast<size_t> (o.macroIndex)]);
        }

        if (juce::exactlyEqual (sum, 0.0f))
            continue;   // ニュートラルでは Base 値をそのまま使う (変換の丸めも入れない)

        const auto& definition = definitions[static_cast<size_t> (target)];
        const float base = polybrisk::params::toNormalised (definition, effective[static_cast<size_t> (target)]);
        effective[static_cast<size_t> (target)] = polybrisk::params::fromNormalised (definition, juce::jlimit (0.0f, 1.0f, base + sum));
    }

    for (size_t i = 0; i < rawValues.size(); ++i)
        effectiveForUi[i].store (effective[i], std::memory_order_relaxed);
}

polybrisk::EngineParams PolyBriskAudioProcessor::makeEngineParams() const noexcept
{
    const auto v = [this] (int i) { return effective[static_cast<size_t> (i)]; };
    const auto asInt = [&v] (int i) { return juce::roundToInt (v (i)); };

    const auto readOsc = [&] (const OscIndex& o)
    {
        polybrisk::OscParams p;
        p.enabled = v (o.enabled) >= 0.5f;
        p.waveform = static_cast<polybrisk::Waveform> (asInt (o.wave));
        p.octave = asInt (o.octave);
        p.semitone = asInt (o.semitone);
        p.fineCents = v (o.fine);
        p.level = v (o.level) * 0.01f;
        p.unisonVoices = asInt (o.unisonVoices);
        p.detune = v (o.detune) * 0.01f;
        p.spread = v (o.spread) * 0.01f;
        p.unisonMix = v (o.unisonMix) * 0.01f;
        p.retrigger = asInt (o.phaseMode) == 1;
        p.pulseWidth = v (o.pulseWidth) * 0.01f;
        return p;
    };

    polybrisk::EngineParams p;
    p.oscA = readOsc (index.oscA);
    p.oscB = readOsc (index.oscB);
    p.amp.attack = v (index.ampAttack) * 0.001f;
    p.amp.decay = v (index.ampDecay) * 0.001f;
    p.amp.sustain = v (index.ampSustain) * 0.01f;
    p.amp.release = v (index.ampRelease) * 0.001f;
    p.velocityToAmp = v (index.ampVelocity) * 0.01f;

    p.transient.enabled = v (index.transientEnabled) >= 0.5f;
    p.transient.mode = static_cast<polybrisk::TransientMode> (asInt (index.transientMode));
    p.transient.amount = v (index.transientAmount) * 0.01f;
    p.transient.duration = v (index.transientDuration) * 0.001f;
    p.transient.noiseColor = v (index.transientNoiseColor) * 0.01f;
    p.transient.pitchSnap = v (index.transientPitchSnap);

    p.filter.character = static_cast<polybrisk::FilterCharacter> (asInt (index.filterCharacter));
    p.filter.mode = static_cast<polybrisk::FilterMode> (asInt (index.filterMode));
    p.filter.cutoffHz = v (index.filterCutoff);
    p.filter.resonance = v (index.filterResonance) * 0.01f;
    p.filter.drive = v (index.filterDrive) * 0.01f;
    p.filter.envAmount = v (index.filterEnvAmount) * 0.01f;
    p.filter.keyTrack = v (index.filterKeyTrack) * 0.01f;
    p.filter.velocity = v (index.filterVelocity) * 0.01f;
    p.filterEnv.attack = v (index.filterEnvAttack) * 0.001f;
    p.filterEnv.decay = v (index.filterEnvDecay) * 0.001f;
    p.filterEnv.sustain = v (index.filterEnvSustain) * 0.01f;
    p.filterEnv.release = v (index.filterEnvRelease) * 0.001f;

    // Symmetric では Bend Up の値を上下両方に使う (SPEC §7.4)
    const bool independent = asInt (index.bendMode) == 1;
    p.performance.bendUp = v (index.bendUp);
    p.performance.bendDown = independent ? v (index.bendDown) : v (index.bendUp);
    p.performance.vibratoDepthCents = v (index.vibratoDepth);
    p.performance.vibratoRateHz = v (index.vibratoRate);
    p.performance.vibratoDelay = v (index.vibratoDelay) * 0.001f;

    p.performance.aftertouchVibratoCents = v (index.aftertouchVibrato);
    p.performance.aftertouchCutoff = v (index.aftertouchCutoff) * 0.01f;

    const auto on = [&v] (int i) { return v (i) >= 0.5f; };

    p.voiceMode = static_cast<polybrisk::VoiceMode> (asInt (index.voiceMode));
    p.glideSeconds = v (index.voiceGlide) * 0.001f;
    p.sub.level = v (index.subLevel) * 0.01f;
    p.sub.octave = asInt (index.subOctave) == 1 ? -2 : -1;
    p.sub.wave = static_cast<polybrisk::SubWave> (asInt (index.subWave));
    p.noise.level = v (index.noiseLevel) * 0.01f;
    p.noise.color = v (index.noiseColor) * 0.01f;

    p.lfo.wave = static_cast<polybrisk::LfoWave> (asInt (index.lfoWave));
    p.lfo.sync = on (index.lfoSync);
    p.lfo.rateHz = v (index.lfoRate);
    p.lfo.divisionBeats = polybrisk::params::lfoDivisionBeats (asInt (index.lfoDivision));
    p.lfo.fadeSeconds = v (index.lfoFade) * 0.001f;
    p.lfo.retrigger = on (index.lfoRetrigger);
    p.lfo.toCutoff = v (index.lfoCutoff) * 0.01f;
    p.lfo.toPulseWidth = v (index.lfoPulseWidth) * 0.01f;
    p.lfo.toPitchCents = v (index.lfoPitch);
    p.lfo.toAmp = v (index.lfoAmp) * 0.01f;

    auto& fx = p.fx;
    fx.bypassAll = on (index.fxBypass);

    fx.drive.enabled = on (index.driveEnabled);
    fx.drive.type = static_cast<polybrisk::DriveType> (asInt (index.driveType));
    fx.drive.drive = v (index.driveDrive) * 0.01f;
    fx.drive.tone = v (index.driveTone) * 0.01f;
    fx.drive.mix = v (index.driveMix) * 0.01f;
    fx.drive.outputDb = v (index.driveOutput);
    fx.drive.autoGain = on (index.driveAutoGain);

    fx.phaser.enabled = on (index.phaserEnabled);
    fx.phaser.type = static_cast<polybrisk::PhaserType> (asInt (index.phaserType));
    fx.phaser.rateHz = v (index.phaserRate);
    fx.phaser.depth = v (index.phaserDepth) * 0.01f;
    fx.phaser.feedback = v (index.phaserFeedback) * 0.01f;
    fx.phaser.mix = v (index.phaserMix) * 0.01f;

    fx.chorus.enabled = on (index.chorusEnabled);
    fx.chorus.type = static_cast<polybrisk::ChorusType> (asInt (index.chorusType));
    fx.chorus.rateHz = v (index.chorusRate);
    fx.chorus.depth = v (index.chorusDepth) * 0.01f;
    fx.chorus.width = v (index.chorusWidth) * 0.01f;
    fx.chorus.mix = v (index.chorusMix) * 0.01f;

    fx.eq.enabled = on (index.eqEnabled);
    fx.eq.lowGainDb = v (index.eqLowGain);
    fx.eq.midFreqHz = v (index.eqMidFreq);
    fx.eq.midGainDb = v (index.eqMidGain);
    fx.eq.highGainDb = v (index.eqHighGain);

    fx.imager.enabled = on (index.imagerEnabled);
    fx.imager.width = v (index.imagerWidth) * 0.01f;
    fx.imager.monoBass = on (index.imagerMonoBass);

    fx.delay.enabled = on (index.delayEnabled);
    fx.delay.mode = static_cast<polybrisk::DelayMode> (asInt (index.delayMode));
    fx.delay.sync = on (index.delaySync);
    fx.delay.division = static_cast<polybrisk::DelayDivision> (asInt (index.delayDivision));
    fx.delay.timeSeconds = v (index.delayTime) * 0.001f;
    fx.delay.feedback = v (index.delayFeedback) * 0.01f;
    fx.delay.tone = v (index.delayTone) * 0.01f;
    fx.delay.mix = v (index.delayMix) * 0.01f;
    fx.delay.ducking = v (index.delayDucking) * 0.01f;

    fx.reverb.enabled = on (index.reverbEnabled);
    fx.reverb.type = static_cast<polybrisk::ReverbType> (asInt (index.reverbType));
    fx.reverb.size = v (index.reverbSize) * 0.01f;
    fx.reverb.decaySeconds = v (index.reverbDecay);
    fx.reverb.preDelaySeconds = v (index.reverbPreDelay) * 0.001f;
    fx.reverb.tone = v (index.reverbTone) * 0.01f;
    fx.reverb.mix = v (index.reverbMix) * 0.01f;

    p.gate.enabled = on (index.gateEnabled);
    p.gate.steps = polybrisk::params::gateStepCount (asInt (index.gateSteps));
    p.gate.rateBeats = polybrisk::params::gateRateBeats (asInt (index.gateRate));
    p.gate.depth = v (index.gateDepth) * 0.01f;
    p.gate.smoothSeconds = v (index.gateSmooth) * 0.001f;
    p.gate.swing = v (index.gateSwing) * 0.01f;
    p.gate.restartOnNote = asInt (index.gateRestart) == 1;
    p.gate.stereo = on (index.gateStereo);
    p.gate.width = v (index.gateWidth) * 0.01f;
    p.gate.pattern = activeGatePattern;

    p.arp.enabled = on (index.arpEnabled);
    p.arp.mode = static_cast<polybrisk::ArpMode> (asInt (index.arpMode));
    p.arp.rateBeats = polybrisk::params::arpRateBeats (asInt (index.arpRate));
    p.arp.octaves = asInt (index.arpOctaves);
    p.arp.gate = v (index.arpGate) * 0.01f;
    p.arp.swing = v (index.arpSwing) * 0.01f;
    p.arp.velocity = v (index.arpVelocity) * 0.01f;
    p.arp.latch = on (index.arpLatch);
    p.arp.restartOnNote = asInt (index.arpRestart) == 1;

    // 全体の音量 (Output Gain) × 音色ごとの補正 (Preset Level)。Output Gain の下限 -60 dB は無音として扱う
    p.outputGain = juce::Decibels::decibelsToGain (v (index.outputGain), -60.0f) * juce::Decibels::decibelsToGain (v (index.presetLevel));
    return p;
}

void PolyBriskAudioProcessor::handleMidiMessage (const juce::MidiMessage& message) noexcept
{
    if (message.isNoteOn())
    {
        engine.noteOn (message.getNoteNumber(), message.getFloatVelocity());
    }
    else if (message.isNoteOff()) // Velocity 0 の Note On も Note Off として扱われる
    {
        engine.noteOff (message.getNoteNumber());
    }
    else if (message.isChannelPressure())
    {
        engine.setAftertouch (static_cast<float> (message.getChannelPressureValue()) / 127.0f);
    }
    else if (message.isAftertouch())
    {
        // ポリフォニック・アフタータッチも、全体に効く 1 つの値として扱う (D-041)
        engine.setAftertouch (static_cast<float> (message.getAfterTouchValue()) / 127.0f);
    }
    else if (message.isPitchWheel())
    {
        const float bend = polybrisk::SynthEngine::pitchWheelToNormalised (message.getPitchWheelValue());
        engine.setPitchBend (bend);
        midiPitchBend.store (bend, std::memory_order_relaxed);        // 画面のホイールの表示用
        midiPitchBendDirty.store (true, std::memory_order_relaxed);
    }
    else if (message.isController())
    {
        const int controller = message.getControllerNumber();
        const int value = message.getControllerValue();

        if (controller == 1)
        {
            engine.setModWheel (static_cast<float> (value) / 127.0f);
            midiModWheel.store (static_cast<float> (value) / 127.0f, std::memory_order_relaxed);
            midiModWheelDirty.store (true, std::memory_order_relaxed);
        }
        else if (controller == 64)
            engine.setSustainPedal (value >= 64);
        else if (controller == 120)
            engine.allSoundOff();
        else if (controller == 123)
            engine.allNotesOff();
        else if (polybrisk::macro::isAssignableCc (controller))
        {
            // MIDI Learn 待ちなら、この CC を対象のマクロへ割り当てる (反映はメッセージスレッド)
            const int target = learnTarget.load (std::memory_order_relaxed);

            if (target >= 0)
            {
                learnResult.store ((target << 8) | controller);
                learnTarget.store (-1);
                activeSnapshot.cc[static_cast<size_t> (target)] = controller;   // すぐ効くよう手元の設定にも入れる
            }

            for (size_t m = 0; m < activeSnapshot.cc.size(); ++m)
            {
                if (activeSnapshot.cc[m] != controller)
                    continue;

                auto& runtime = macroRuntime[m];
                runtime.ccValue = static_cast<float> (value) / 127.0f;
                runtime.rawAtCc = rawValues[static_cast<size_t> (index.macros[m])]->load (std::memory_order_relaxed);
                runtime.ccActive = true;
                pendingMacroValue[m].store (runtime.ccValue * 100.0f);
            }
        }
    }
}

void PolyBriskAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    if (numChannels < 2)
    {
        buffer.clear();
        return;
    }

    // 画面内鍵盤 (Standalone) のノートを MIDI に混ぜる
    keyboardState.processNextMidiBuffer (midiMessages, 0, numSamples, true);

    // 画面内のホイール (Standalone)。動かしたときだけ渡す (外部 MIDI の値を毎ブロック上書きしない)
    if (uiPitchBendDirty.exchange (false))
        engine.setPitchBend (uiPitchBend.load());

    if (uiModWheelDirty.exchange (false))
        engine.setModWheel (uiModWheel.load());

    if (arpResetRequested.exchange (false))
        engine.resetArp();

    computeEffectiveValues (numSamples);
    engine.setParams (makeEngineParams());

    // ホストのトランスポート (JUCE に触れるのはここまで。DSP には POD で渡す)。
    // テンポや位置が得られないホストでは、内部クロック (120 BPM) にフォールバックする。
    polybrisk::TransportInfo transport;

    if (auto* playHead = getPlayHead())
    {
        if (const auto position = playHead->getPosition())
        {
            transport.playing = position->getIsPlaying();

            if (const auto hostBpm = position->getBpm())
                transport.bpm = *hostBpm;

            if (const auto ppq = position->getPpqPosition())
            {
                transport.ppq = *ppq;
                transport.positionValid = true;
            }
        }
    }

    engine.setTransport (transport);

    auto* left = buffer.getWritePointer (0);
    auto* right = buffer.getWritePointer (1);

    // MIDI イベントのサンプル位置でブロックを分割して処理する (SPEC §3)
    int position = 0;

    for (const auto metadata : midiMessages)
    {
        const int eventPosition = juce::jlimit (position, numSamples, metadata.samplePosition);

        if (eventPosition > position)
        {
            engine.render (left + position, right + position, eventPosition - position);
            position = eventPosition;
        }

        // 3 バイトまでのチャンネルメッセージだけを見る。SysEx などの長いメッセージは使わないうえ、
        // juce::MidiMessage にするとヒープ確保が起きるので、作らずに捨てる
        if (metadata.numBytes <= 3)
            handleMidiMessage (metadata.getMessage());
    }

    if (position < numSamples)
        engine.render (left + position, right + position, numSamples - position);

    for (int ch = 2; ch < numChannels; ++ch)
        buffer.clear (ch, 0, numSamples);

    gateStepForUi.store (engine.getGateStep(), std::memory_order_relaxed);

    // 出力メーター: このブロックのピークを、画面が次に読むまでの最大値として残す (確保・ロックなし)
    for (size_t ch = 0; ch < 2; ++ch)
    {
        const float blockPeak = buffer.getMagnitude (static_cast<int> (ch), 0, numSamples);

        if (blockPeak > outputPeak[ch].load (std::memory_order_relaxed))
            outputPeak[ch].store (blockPeak, std::memory_order_relaxed);
    }
}

juce::AudioProcessorEditor* PolyBriskAudioProcessor::createEditor()
{
    return new PolyBriskAudioProcessorEditor (*this);
}

void PolyBriskAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    polybrisk::state::toBinary (apvts, meta, destData);
}

void PolyBriskAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // 不正なデータは無視して現在の状態を維持する
    if (polybrisk::state::fromBinary (data, sizeInBytes, apvts, meta))
    {
        // 復元による変更は Dirty にしない (Dirty の状態そのものは保存されている)。Arp の生成ノートは終了させる
        parametersTouched.store (false);
        arpResetRequested.store (true);
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PolyBriskAudioProcessor();
}
