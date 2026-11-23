#include "synth/SynthEngine.h"

#include <algorithm>
#include <cmath>

namespace polybrisk
{

namespace
{
constexpr double controlSmoothingSeconds = 0.005; // ピッチ・Detune・Spread・Pulse Width
constexpr double levelSmoothingSeconds = 0.010;   // OSC レベル・出力ゲイン
constexpr int renderChunkSize = FxChain::maxBlock; // ホスト側レートでの処理単位 (128)
constexpr double analogCutoffLagSeconds = 0.0015;  // Analog: カットオフの追従にわずかな遅れを持たせる
constexpr int silenceHoldSamples = 256;           // 全ボイス停止後、デシメーターの余韻を出し切るまでの長さ
} // namespace

double SynthEngine::midiNoteToHz (double midiNote) noexcept
{
    return 440.0 * std::exp2 ((midiNote - 69.0) / 12.0);
}

float SynthEngine::softLimit (float x) noexcept
{
    const float magnitude = std::abs (x);

    if (magnitude <= limiterThreshold)
        return x;

    const float range = 1.0f - limiterThreshold;
    const float limited = limiterThreshold + range * std::tanh ((magnitude - limiterThreshold) / range);
    return x < 0.0f ? -limited : limited;
}

void SynthEngine::prepare (double newSampleRate) noexcept
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;

    // 44.1 / 48 kHz ではボイス処理を 2 倍のレートで行い、発振器の折り返し成分を可聴帯域の外へ追い出す。
    // 88.2 / 96 kHz 以上は元のレートがすでに十分に高いので等倍。
    oversampling = sampleRate < oversamplingRateLimit ? 2 : 1;
    internalSampleRate = sampleRate * oversampling;
    control.sampleRate = internalSampleRate;

    controlCoeff = 1.0 - std::exp (-static_cast<double> (controlBlockSize) / (controlSmoothingSeconds * internalSampleRate));
    analogLagCoeff = 1.0 - std::exp (-static_cast<double> (controlBlockSize) / (analogCutoffLagSeconds * internalSampleRate));
    levelCoeff = static_cast<float> (1.0 - std::exp (-1.0 / (levelSmoothingSeconds * internalSampleRate)));
    gainCoeff = static_cast<float> (1.0 - std::exp (-1.0 / (levelSmoothingSeconds * sampleRate)));

    for (std::size_t i = 0; i < voices.size(); ++i)
        voices[i].prepare (internalSampleRate, static_cast<std::uint32_t> (i + 1));

    fx.prepare (sampleRate);   // FX はホスト側のレートで動く (バッファの確保はここだけ)
    clock.prepare (sampleRate);
    gate.prepare (sampleRate);

    reset();
}

void SynthEngine::reset() noexcept
{
    for (auto& v : voices)
        v.reset();

    decimatorL.reset();
    decimatorR.reset();
    fx.reset();
    arp.reset (*this);
    gate.reset();
    clock.reset();
    gateOrigin = 0.0;
    heldKeys = 0;
    silentSamples = silenceHoldSamples;

    pitchBend = 0.0f;
    modWheel = 0.0f;
    smoothedBendSemitones = 0.0;
    smoothedModWheel = 0.0;
    vibratoPhase = 0.0;
    aftertouch = 0.0f;
    smoothedAftertouch = 0.0;
    numHeldNotes = 0;
    lastNote = -1;
    lfoPhase = 0.0;
    chunkStartBeats = 0.0;

    noteCounter = 0;
    pedalDown = false;
    controlInitialised = false;
    samplesUntilControl = 0;
}

int SynthEngine::getActiveVoiceCount() const noexcept
{
    int count = 0;

    for (const auto& v : voices)
        if (v.isActive())
            ++count;

    return count;
}

Voice& SynthEngine::chooseVoice() noexcept
{
    // 1. 空きボイス
    for (auto& v : voices)
        if (! v.isActive())
            return v;

    // 2. Release 中で最も音量の小さいボイス (SPEC §3)
    Voice* quietest = nullptr;

    for (auto& v : voices)
        if (v.isReleasing() && (quietest == nullptr || v.getEnvelopeLevel() < quietest->getEnvelopeLevel()))
            quietest = &v;

    if (quietest != nullptr)
        return *quietest;

    // 3. 最古のボイス
    Voice* oldest = &voices[0];

    for (auto& v : voices)
        if (v.getAge() < oldest->getAge())
            oldest = &v;

    return *oldest;
}

void SynthEngine::setParams (const EngineParams& p) noexcept
{
    // 発音モードを切り替えたら、押鍵の履歴は捨てる (古いモードでの対応が取れなくなるため)
    if (p.voiceMode != params.voiceMode)
        numHeldNotes = 0;

    params = p;
    arp.setParams (params.arp, *this);   // OFF にした・Latch を外した時点で、生成ノートを終了する
}

void SynthEngine::setTransport (const TransportInfo& info) noexcept
{
    clock.setTransport (info);

    if (clock.consumeStopped())
        arp.handleHostStop (clock.getPosition(), *this);

    if (clock.consumeDiscontinuity())
    {
        // ループ・シーク・再生開始: 生成ノートを終了して新しい位置に同期する。Gate はスムージングで追従する
        arp.handleDiscontinuity (clock.getPosition(), *this);
        gateOrigin = clock.getPosition();
    }
}

void SynthEngine::startVoice (int midiNote, float velocity, std::uint32_t noteId) noexcept
{
    // ノートオン直後のボイスが最新のパラメーターで始まるよう、初回はここでコントロール値を確定する
    if (! controlInitialised)
        updateControl();

    // グライド: 直前のノートから滑らせる。Legato では、鍵を押さえたまま次を弾いたとき (changeNote) だけ滑らせる
    const double glideFrom = (params.glideSeconds > 0.0f && params.voiceMode != VoiceMode::legato) ? static_cast<double> (lastNote) : -1.0;
    lastNote = midiNote;

    // 同音の重複 Note On も独立したボイスとして発音する
    chooseVoice().start (midiNote, velocity, ++noteCounter, control, noteId, glideFrom);
}

Voice* SynthEngine::findHeldDirectVoice() noexcept
{
    // 鍵盤から直接鳴らしている (Arp の生成ノートではない)、まだ保持されている最新のボイス
    Voice* found = nullptr;

    for (auto& v : voices)
        if (v.isActive() && ! v.isFadingOut() && v.getNoteId() == 0 && (v.isKeyDown() || v.isSustained())
            && (found == nullptr || v.getAge() > found->getAge()))
            found = &v;

    return found;
}

void SynthEngine::pushHeldNote (int midiNote, float velocity) noexcept
{
    removeHeldNote (midiNote);

    if (numHeldNotes == static_cast<int> (heldNotes.size()))
    {
        std::copy (heldNotes.begin() + 1, heldNotes.end(), heldNotes.begin());
        --numHeldNotes;
    }

    heldNotes[static_cast<std::size_t> (numHeldNotes++)] = { midiNote, velocity };
}

void SynthEngine::removeHeldNote (int midiNote) noexcept
{
    for (int i = 0; i < numHeldNotes; ++i)
        if (heldNotes[static_cast<std::size_t> (i)].note == midiNote)
        {
            std::copy (heldNotes.begin() + i + 1, heldNotes.begin() + numHeldNotes, heldNotes.begin() + i);
            --numHeldNotes;
            return;
        }
}

void SynthEngine::arpNoteOn (int midiNote, float velocity, std::uint32_t noteId) noexcept
{
    startVoice (midiNote, velocity, noteId);
}

void SynthEngine::arpNoteOff (std::uint32_t noteId) noexcept
{
    // 生成ノートはペダルで伸ばさない (ペダルは Arp の和音保持に使う)
    for (auto& v : voices)
        if (v.isActive() && v.isKeyDown() && v.getNoteId() == noteId)
            v.keyReleased (false);
}

void SynthEngine::noteOn (int midiNote, float velocity) noexcept
{
    // 何も押していない状態からの最初のノートが「フレーズの開始」(Gate の Restart = Note On の起点)
    if (heldKeys == 0)
        gateOrigin = clock.getPosition();

    ++heldKeys;

    if (params.arp.enabled)
    {
        arp.keyDown (midiNote, velocity, clock.getPosition(), *this);   // 元のノートは直接発音させない
        return;
    }

    if (params.voiceMode != VoiceMode::poly)
    {
        // Mono / Legato: 鳴っているボイスがあれば、新しく発音せずにそのボイスの音程を移す
        pushHeldNote (midiNote, velocity);

        if (auto* held = findHeldDirectVoice())
        {
            if (! controlInitialised)
                updateControl();

            held->changeNote (midiNote, velocity, params.voiceMode == VoiceMode::mono, ++noteCounter, control);
            lastNote = midiNote;
            return;
        }
    }

    startVoice (midiNote, velocity, 0);
}

void SynthEngine::noteOff (int midiNote) noexcept
{
    heldKeys = std::max (0, heldKeys - 1);
    arp.keyUp (midiNote, *this);

    if (params.voiceMode != VoiceMode::poly)
    {
        removeHeldNote (midiNote);
        auto* held = findHeldDirectVoice();

        if (held != nullptr && held->isKeyDown() && held->getNote() == midiNote)
        {
            if (numHeldNotes > 0)
            {
                // まだ押さえている鍵があれば、最後に押した鍵の音程へ戻る
                const auto& previous = heldNotes[static_cast<std::size_t> (numHeldNotes - 1)];
                held->changeNote (previous.note, previous.velocity, params.voiceMode == VoiceMode::mono, ++noteCounter, control);
                lastNote = previous.note;
            }
            else
            {
                held->keyReleased (pedalDown);
            }

            return;
        }

        // 鳴っていない鍵を離しただけなら、下の検索は何も見つけない。Poly で押さえた和音のまま Mono / Legato に
        // 切り替えた場合は、その和音のボイスがまだ鍵を押さえた状態で残っているので、下で同じノートのボイスを離す
        // (ここで return すると、そのボイスが鳴りっぱなしになる)
    }

    // 直接発音したボイス (Arp を ON にする前から鳴っていたものを含む) のうち、同じノートの最古の 1 つを離す
    Voice* target = nullptr;

    for (auto& v : voices)
        if (v.isActive() && v.isKeyDown() && v.getNoteId() == 0 && v.getNote() == midiNote
            && (target == nullptr || v.getAge() < target->getAge()))
            target = &v;

    if (target != nullptr)
        target->keyReleased (pedalDown);
}

void SynthEngine::setSustainPedal (bool down) noexcept
{
    arp.setPedal (down, *this);

    if (pedalDown && ! down)
        for (auto& v : voices)
            v.pedalReleased();

    pedalDown = down;
}

void SynthEngine::allNotesOff() noexcept
{
    numHeldNotes = 0;
    arp.reset (*this);   // 生成ノートを終了し、押鍵・Latch の保持も捨てる
    heldKeys = 0;

    for (auto& v : voices)
        if (v.isActive())
            v.forceRelease();
}

void SynthEngine::allSoundOff() noexcept
{
    numHeldNotes = 0;
    arp.reset (*this);
    heldKeys = 0;

    for (auto& v : voices)
        v.kill();
}

void SynthEngine::smoothOsc (OscControl& oscControl, const OscParams& target, OscSmoothingState& state) noexcept
{
    const double targetPitch = 12.0 * std::clamp (target.octave, -2, 2)
                             + std::clamp (target.semitone, -12, 12)
                             + static_cast<double> (std::clamp (target.fineCents, -100.0f, 100.0f)) / 100.0;
    const double targetWidth = std::clamp (static_cast<double> (target.pulseWidth), 0.05, 0.95);
    const float targetDetune = std::clamp (target.detune, 0.0f, 1.0f);
    const float targetSpread = std::clamp (target.spread, 0.0f, 1.0f);
    const float targetMix = std::clamp (target.unisonMix, 0.0f, 1.0f);

    if (! controlInitialised)
    {
        state.pitchOffset = targetPitch;
        state.pulseWidth = targetWidth;
        state.detune = targetDetune;
        state.spread = targetSpread;
        state.unisonMix = targetMix;
        state.level = target.enabled ? std::clamp (target.level, 0.0f, 1.0f) : 0.0f;
    }
    else
    {
        const auto k = static_cast<float> (controlCoeff);
        state.pitchOffset += (targetPitch - state.pitchOffset) * controlCoeff;
        state.pulseWidth += (targetWidth - state.pulseWidth) * controlCoeff;
        state.detune += (targetDetune - state.detune) * k;
        state.spread += (targetSpread - state.spread) * k;
        state.unisonMix += (targetMix - state.unisonMix) * k;
    }

    // OFF にした直後はレベルが十分に下がるまで発振を続け、クリックを防ぐ
    oscControl.enabled = target.enabled || state.level > 1.0e-4f;
    oscControl.waveform = target.waveform;
    oscControl.retrigger = target.retrigger;
    oscControl.pitchOffset = state.pitchOffset;
    oscControl.pulseWidth = state.pulseWidth;

    // SuperSAW だけ上下のデチューンを非対称にする (Saw などのユニゾンは対称のまま。D-045)
    computeUnisonLayout (oscControl.layout, target.unisonVoices, state.detune, state.spread,
                         state.unisonMix, target.waveform == Waveform::superSaw);
}

void SynthEngine::smoothFilterControl() noexcept
{
    const auto& f = params.filter;
    const double targetCutoff = std::log2 (std::clamp (static_cast<double> (f.cutoffHz), filter::minCutoffHz, filter::maxCutoffHz));
    const double targetEnv = envRangeOctaves * std::clamp (static_cast<double> (f.envAmount), -1.0, 1.0);
    const double targetKeyTrack = std::clamp (static_cast<double> (f.keyTrack), 0.0, 1.0);
    const double targetVelocity = velocityRangeOctaves * std::clamp (static_cast<double> (f.velocity), 0.0, 1.0);
    const float targetResonance = std::clamp (f.resonance, 0.0f, 1.0f);
    const float targetDrive = std::clamp (f.drive, 0.0f, 1.0f);
    const float targetVelocityToAmp = std::clamp (params.velocityToAmp, 0.0f, 1.0f);

    auto& s = smoothFilter;

    if (! controlInitialised)
    {
        s.cutoffOctaves = targetCutoff;
        s.envOctaves = targetEnv;
        s.keyTrack = targetKeyTrack;
        s.velocityOctaves = targetVelocity;
        s.resonance = targetResonance;
        s.drive = targetDrive;
        smoothedVelocityToAmp = targetVelocityToAmp;
    }
    else
    {
        const auto k = static_cast<float> (controlCoeff);
        s.cutoffOctaves += (targetCutoff - s.cutoffOctaves) * controlCoeff;
        s.envOctaves += (targetEnv - s.envOctaves) * controlCoeff;
        s.keyTrack += (targetKeyTrack - s.keyTrack) * controlCoeff;
        s.velocityOctaves += (targetVelocity - s.velocityOctaves) * controlCoeff;
        s.resonance += (targetResonance - s.resonance) * k;
        s.drive += (targetDrive - s.drive) * k;
        smoothedVelocityToAmp += (targetVelocityToAmp - smoothedVelocityToAmp) * k;
    }

    // Drive 0 の Clean は入力段を完全に線形にするので、スムージングの残りかすは 0 に丸める
    const float drive = (targetDrive <= 0.0f && s.drive < 1.0e-4f) ? 0.0f : s.drive;

    auto& c = control.filter;
    c.settings = FilterSettings::make (f.character, f.mode, s.resonance, drive);
    c.env = AdsrCoefficients::make (params.filterEnv, internalSampleRate);
    c.cutoffOctaves = s.cutoffOctaves;
    c.envOctaves = s.envOctaves;
    c.keyTrack = s.keyTrack;
    c.velocityOctaves = s.velocityOctaves;
    c.lagCoeff = f.character == FilterCharacter::analog ? analogLagCoeff : 1.0;
    control.velocityToAmp = smoothedVelocityToAmp;
}

void SynthEngine::updateControl (int offsetInChunk) noexcept
{
    smoothOsc (control.oscA, params.oscA, smoothA);
    smoothOsc (control.oscB, params.oscB, smoothB);
    smoothFilterControl();

    // --- Glide / Sub / Noise ---
    control.glideSamples = std::clamp (static_cast<double> (params.glideSeconds), 0.0, 2.0) * internalSampleRate;

    if (! controlInitialised)
    {
        smoothedSubLevel = std::clamp (params.sub.level, 0.0f, 1.0f);
        smoothedNoiseLevel = std::clamp (params.noise.level, 0.0f, 1.0f);
    }

    control.sub.enabled = params.sub.level > 0.0f || smoothedSubLevel > 1.0e-4f;
    control.sub.wave = params.sub.wave;
    control.sub.pitchOffset = params.sub.octave <= -2 ? -24.0 : -12.0;

    control.noise.enabled = params.noise.level > 0.0f || smoothedNoiseLevel > 1.0e-4f;

    if (control.noise.enabled)
    {
        // カラーは Transient の Noise Burst と同じ規則 (Dark 側はローパス、Bright 側はハイパス)
        TransientParams colour;
        colour.noiseColor = params.noise.color;
        colour.amount = 1.0f;
        const auto shaped = TransientControl::make (colour, internalSampleRate);
        control.noise.colourCoeff = shaped.colourCoeff;
        control.noise.colourHighPass = shaped.colourHighPass;
        control.noise.gain = shaped.noiseLevel;
    }

    // --- LFO ---
    {
        const auto& l = params.lfo;
        const double targetCutoff = modRangeOctaves * std::clamp (static_cast<double> (l.toCutoff), -1.0, 1.0);
        const double targetWidth = 0.45 * std::clamp (static_cast<double> (l.toPulseWidth), 0.0, 1.0);
        const double targetPitch = std::clamp (static_cast<double> (l.toPitchCents), 0.0, 1200.0) / 100.0;
        const float targetAmp = std::clamp (l.toAmp, 0.0f, 1.0f);
        auto& s = smoothLfo;

        if (! controlInitialised)
        {
            s = { targetCutoff, targetWidth, targetPitch, targetAmp };
        }
        else
        {
            s.cutoff += (targetCutoff - s.cutoff) * controlCoeff;
            s.pulseWidth += (targetWidth - s.pulseWidth) * controlCoeff;
            s.pitch += (targetPitch - s.pitch) * controlCoeff;
            s.amp += (targetAmp - s.amp) * static_cast<float> (controlCoeff);
        }

        const bool wanted = targetCutoff != 0.0 || targetWidth != 0.0 || targetPitch != 0.0 || targetAmp != 0.0f;
        const bool settling = std::abs (s.cutoff) > 1.0e-5 || s.pulseWidth > 1.0e-6 || s.pitch > 1.0e-5 || s.amp > 1.0e-5f;

        const double hz = l.sync ? clock.getBpm() / (60.0 * std::clamp (l.divisionBeats, 1.0 / 16.0, 64.0))
                                 : std::clamp (static_cast<double> (l.rateHz), 0.05, 20.0);

        // テンポ同期でホストが再生中なら、位相を音楽位置に固定する (ループやシークの後も同じ位置で同じ形になる)
        if (l.sync && clock.isHostDriven())
            lfoPhase = (chunkStartBeats + offsetInChunk * beatsPerInternalSample) / std::clamp (l.divisionBeats, 1.0 / 16.0, 64.0);

        auto& c = control.lfo;
        c.active = wanted || settling;
        c.wave = l.wave;
        c.retrigger = l.retrigger;
        c.globalPhase = lfoPhase;
        c.cyclesPerSample = hz / internalSampleRate;
        c.fadeSamples = std::clamp (static_cast<double> (l.fadeSeconds), 0.0, 5.0) * internalSampleRate;
        c.cutoffOctaves = c.active ? s.cutoff : 0.0;
        c.pulseWidth = c.active ? s.pulseWidth : 0.0;
        c.pitchSemitones = c.active ? s.pitch : 0.0;
        c.amp = c.active ? s.amp : 0.0f;

        lfoPhase += c.cyclesPerSample * controlBlockSize;

        if (lfoPhase > 1.0e9)
            lfoPhase -= 1.0e9;
    }

    control.amp = AdsrCoefficients::make (params.amp, internalSampleRate);
    control.transient = TransientControl::make (params.transient, internalSampleRate);

    // --- Pitch Bend / Vibrato (SPEC §7.4 / §7.5)。cents の加算で独立に合成する ---
    const auto& perf = params.performance;
    const double bendRange = pitchBend >= 0.0f ? std::clamp (static_cast<double> (perf.bendUp), 0.0, 48.0)
                                               : std::clamp (static_cast<double> (perf.bendDown), 0.0, 48.0);
    const double targetBend = static_cast<double> (pitchBend) * bendRange;

    if (! controlInitialised)
    {
        smoothedBendSemitones = targetBend;
        smoothedModWheel = modWheel;
        smoothedAftertouch = aftertouch;
    }
    else
    {
        smoothedBendSemitones += (targetBend - smoothedBendSemitones) * controlCoeff;
        smoothedModWheel += (static_cast<double> (modWheel) - smoothedModWheel) * controlCoeff;
        smoothedAftertouch += (static_cast<double> (aftertouch) - smoothedAftertouch) * controlCoeff;
    }

    const double rate = std::clamp (static_cast<double> (perf.vibratoRateHz), 0.1, 12.0);
    const double depth = std::clamp (static_cast<double> (perf.vibratoDepthCents), 0.0, 200.0);
    const double delay = std::clamp (static_cast<double> (perf.vibratoDelay), 0.0, 2.0);

    control.pitchBendSemitones = smoothedBendSemitones;
    // アフタータッチ: ビブラートの深さとカットオフに加算する (どちらも量が 0 なら何も変わらない)
    const double pressureVibrato = std::clamp (static_cast<double> (perf.aftertouchVibratoCents), 0.0, 200.0) * smoothedAftertouch;
    control.filter.cutoffOctaves += modRangeOctaves * std::clamp (static_cast<double> (perf.aftertouchCutoff), -1.0, 1.0) * smoothedAftertouch;

    control.vibratoCents = (depth * smoothedModWheel + pressureVibrato) * std::sin (2.0 * 3.141592653589793 * vibratoPhase);
    control.vibratoDelaySamples = delay * internalSampleRate;
    control.vibratoFadeSamples = std::max (1.0, 0.5 * delay * internalSampleRate);

    vibratoPhase += rate * static_cast<double> (controlBlockSize) / internalSampleRate;
    vibratoPhase -= std::floor (vibratoPhase);

    if (! controlInitialised)
    {
        smoothedOutputGain = params.outputGain;
        controlInitialised = true;
    }
}

void SynthEngine::renderVoices (float* left, float* right, int numSamples) noexcept
{
    int offset = 0;

    while (offset < numSamples)
    {
        // コントロールレートの更新位置は絶対サンプル位置にそろえる (ブロック分割に依存しない)
        if (samplesUntilControl <= 0)
        {
            updateControl (offset);
            samplesUntilControl = controlBlockSize;

            for (auto& v : voices)
                if (v.isActive())
                    v.controlTick (control);
        }

        const int chunk = std::min (numSamples - offset, samplesUntilControl);
        float* l = left + offset;
        float* r = right + offset;

        float levelA[controlBlockSize];
        float levelB[controlBlockSize];
        float levelSub[controlBlockSize];
        float levelNoise[controlBlockSize];
        const float targetSub = std::clamp (params.sub.level, 0.0f, 1.0f);
        const float targetNoise = std::clamp (params.noise.level, 0.0f, 1.0f);

        const float targetA = params.oscA.enabled ? std::clamp (params.oscA.level, 0.0f, 1.0f) : 0.0f;
        const float targetB = params.oscB.enabled ? std::clamp (params.oscB.level, 0.0f, 1.0f) : 0.0f;

        for (int i = 0; i < chunk; ++i)
        {
            smoothA.level += (targetA - smoothA.level) * levelCoeff;
            smoothB.level += (targetB - smoothB.level) * levelCoeff;

            smoothedSubLevel += (targetSub - smoothedSubLevel) * levelCoeff;
            smoothedNoiseLevel += (targetNoise - smoothedNoiseLevel) * levelCoeff;

            levelA[i] = smoothA.level;
            levelB[i] = smoothB.level;
            levelSub[i] = smoothedSubLevel;
            levelNoise[i] = smoothedNoiseLevel;

            l[i] = 0.0f;
            r[i] = 0.0f;
        }

        for (auto& v : voices)
            if (v.isActive())
                v.render (l, r, chunk, control, { levelA, levelB, levelSub, levelNoise });

        offset += chunk;
        samplesUntilControl -= chunk;
    }
}

void SynthEngine::render (float* left, float* right, int numSamples) noexcept
{
    int offset = 0;

    while (offset < numSamples)
    {
        int chunk = std::min (numSamples - offset, renderChunkSize);
        float* l = left + offset;
        float* r = right + offset;

        // RHYTHM: この区間の各サンプルの音楽位置。Arp のイベントがあれば、その位置で区間を切る
        double beats[renderChunkSize];
        clock.peek (beats, chunk);
        chunkStartBeats = beats[0];
        beatsPerInternalSample = clock.getBeatsPerSample() / oversampling;

        if (params.arp.enabled)
            chunk = arp.advance (beats, chunk, clock.isHostDriven(), clock.getBeatsPerSample(), *this);

        float gateLeft[renderChunkSize];
        float gateRight[renderChunkSize];
        bool gateActive = false;

        if (params.gate.enabled || gate.getCurrentStep() >= 0)
        {
            // Restart = Note On、またはホストが止まっているときは、フレーズの開始位置を起点にする
            if (params.gate.restartOnNote || ! clock.isHostDriven())
                for (int i = 0; i < chunk; ++i)
                    beats[i] -= gateOrigin;

            gateActive = gate.process (gateLeft, gateRight, beats, chunk, params.gate);
        }
        else
        {
            gate.idle();
        }

        clock.advance (chunk);

        if (oversampling == 2)
        {
            float highL[renderChunkSize * 2];
            float highR[renderChunkSize * 2];
            renderVoices (highL, highR, chunk * 2);

            for (int i = 0; i < chunk; ++i)
            {
                l[i] = decimatorL.process (highL[2 * i], highL[2 * i + 1]);
                r[i] = decimatorR.process (highR[2 * i], highR[2 * i + 1]);
            }
        }
        else
        {
            renderVoices (l, r, chunk);
        }

        const bool voicesActive = getActiveVoiceCount() > 0;

        // 全ボイスが止まってデシメーターの余韻も出し切ったら、ボイス側の出力を完全な無音にする
        for (int i = 0; i < chunk; ++i)
        {
            if (voicesActive)
                silentSamples = 0;
            else if (silentSamples < silenceHoldSamples)
                ++silentSamples;

            if (silentSamples >= silenceHoldSamples)
            {
                l[i] = 0.0f;
                r[i] = 0.0f;
            }
        }

        if (silentSamples >= silenceHoldSamples)
        {
            decimatorL.reset();
            decimatorR.reset();
        }

        // グローバル FX (SPEC §2)。Delay / Reverb の尾はボイスが止まった後も続く
        fx.process (l, r, chunk, params.fx, clock.getBpm(), gateActive ? gateLeft : nullptr, gateActive ? gateRight : nullptr);

        // 出力ゲインと安全用リミッターは最終段。softLimit (0) は 0 なので、無音は無音のまま
        for (int i = 0; i < chunk; ++i)
        {
            smoothedOutputGain += (params.outputGain - smoothedOutputGain) * gainCoeff;
            l[i] = softLimit (l[i] * smoothedOutputGain);
            r[i] = softLimit (r[i] * smoothedOutputGain);
        }

        offset += chunk;
    }
}

} // namespace polybrisk
