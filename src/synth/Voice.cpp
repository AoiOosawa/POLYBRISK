#include "synth/Voice.h"

#include <algorithm>
#include <cmath>

namespace polybrisk
{

namespace
{
constexpr double declickSeconds = 0.002;

inline double noteToHz (double midiNote) noexcept
{
    return 440.0 * std::exp2 ((midiNote - 69.0) / 12.0);
}
} // namespace

void Voice::prepare (double sampleRate, std::uint32_t seedValue) noexcept
{
    const double sr = sampleRate > 0.0 ? sampleRate : 44100.0;
    fadeLength = std::max (8, static_cast<int> (declickSeconds * sr));

    oscA.seed (seedValue * 2654435761u + 0x1234567u);
    oscB.seed (seedValue * 40503u + 0x7654321u);
    noiseBurst.seed (seedValue * 747796405u + 0x2545f491u);
    noiseState = seedValue * 1664525u + 0x3c6ef35fu;
    lfoSeed = seedValue * 0x85ebca6bu + 0x27d4eb2fu;
    reset();
}

void Voice::reset() noexcept
{
    amp.reset();
    filterEnv.reset();
    filter.reset();
    noiseBurst.stop();
    filterG = 0.0f;
    filterGStep = 0.0f;
    filterRampLeft = 0;
    pitchSnapOffset = 0.0;
    glidePitch = 0.0;
    glideStep = 0.0;
    glideTicksLeft = 0;
    pendingGlideFrom = -1.0;
    subPhase = 0.0;
    noiseLowPass = 0.0f;
    lfoPulseWidth = 0.0;
    ampMod = 1.0f;
    ampModTarget = 1.0f;
    samplesSinceStart = 0;
    active = false;
    keyDown = false;
    sustained = false;
    pendingStart = false;
    note = -1;
    noteId = 0;
    velocity = 0.0f;
    gainVelocity = 0.0f;
    age = 0;
    fadeSamplesLeft = 0;
}

void Voice::start (int midiNote, float newVelocity, std::uint64_t noteAge, const VoiceControl& control, std::uint32_t id,
                   double glideFromNote) noexcept
{
    noteId = id;
    pendingGlideFrom = glideFromNote;
    note = std::clamp (midiNote, 0, 127);
    velocity = std::clamp (newVelocity, 0.0f, 1.0f);
    age = noteAge;
    keyDown = true;
    sustained = false;

    if (active)
    {
        // スティール: 現在の音を短くフェードアウトしてから新しいノートを開始する
        pendingStart = true;

        if (fadeSamplesLeft <= 0)
            fadeSamplesLeft = fadeLength;
    }
    else
    {
        beginNote (control);
    }
}

void Voice::beginNote (const VoiceControl& control) noexcept
{
    pendingStart = false;
    fadeSamplesLeft = 0;
    active = true;
    gainVelocity = velocity;    // 新しいノートの音量は、ここで初めて反映する (スティールのフェード中は前のノートの音量のまま)

    if (control.oscA.retrigger)
        oscA.retrigger();

    if (control.oscB.retrigger)
        oscB.retrigger();

    amp.reset();
    amp.noteOn();
    filterEnv.reset();
    filterEnv.noteOn();
    filter.reset();
    samplesSinceStart = 0;
    subPhase = 0.0;
    noiseLowPass = 0.0f;
    beginGlide (pendingGlideFrom, control);
    pendingGlideFrom = -1.0;

    if (control.transient.noiseActive)
        noiseBurst.trigger();
    else
        noiseBurst.stop();

    updateModulation (control, true);

    // フェード中に鍵盤が離されていた場合は、すぐに Release へ進める
    if (! keyDown && ! sustained)
        releaseEnvelopes();
}

void Voice::beginGlide (double fromPitch, const VoiceControl& control) noexcept
{
    const auto target = static_cast<double> (note);

    if (fromPitch < 0.0 || control.glideSamples <= 0.0 || fromPitch == target)
    {
        glidePitch = target;
        glideStep = 0.0;
        glideTicksLeft = 0;
        return;
    }

    // 指定時間で直線的に (semitone 単位で) 目標へ進む
    glideTicksLeft = std::max (1, static_cast<int> (control.glideSamples / controlBlockSize));
    glidePitch = fromPitch;
    glideStep = (target - fromPitch) / glideTicksLeft;
}

void Voice::changeNote (int midiNote, float newVelocity, bool retrigger, std::uint64_t noteAge, const VoiceControl& control) noexcept
{
    const double from = glidePitch;
    note = std::clamp (midiNote, 0, 127);
    age = noteAge;
    keyDown = true;
    sustained = false;

    if (pendingStart)
    {
        // スティールのフェード中: 始まる前のノートを差し替えるだけ
        velocity = std::clamp (newVelocity, 0.0f, 1.0f);
        return;
    }

    beginGlide (from, control);

    if (retrigger)
    {
        // いまのレベルから Attack をやり直す (レベルを 0 に戻さないのでクリックしない)
        velocity = std::clamp (newVelocity, 0.0f, 1.0f);
        amp.noteOn();
        filterEnv.noteOn();
        samplesSinceStart = 0;

        if (control.transient.noiseActive)
            noiseBurst.trigger();
    }
}

void Voice::releaseEnvelopes() noexcept
{
    amp.noteOff();
    filterEnv.noteOff();
}

void Voice::updateModulation (const VoiceControl& control, bool jump) noexcept
{
    // グライド: コントロール周期ごとに 1 歩進む
    if (! jump && glideTicksLeft > 0)
    {
        glidePitch += glideStep;

        if (--glideTicksLeft == 0)
            glidePitch = static_cast<double> (note);
    }

    // LFO: Key Sync ならノートオンからの経過時間、そうでなければ全ボイス共通の位相
    double lfoValue = 0.0;

    if (control.lfo.active)
    {
        const auto& l = control.lfo;
        const double phase = l.retrigger ? static_cast<double> (samplesSinceStart) * l.cyclesPerSample : l.globalPhase;
        const std::uint32_t seed = l.retrigger ? lfoSeed + static_cast<std::uint32_t> (age) * 2654435761u : 0x51ed270bu;
        lfoValue = lfo::value (l.wave, phase, seed);

        if (l.fadeSamples > 0.0)
        {
            const double x = std::clamp (static_cast<double> (samplesSinceStart) / l.fadeSamples, 0.0, 1.0);
            lfoValue *= x * x * (3.0 - 2.0 * x);
        }

        lfoPulseWidth = l.pulseWidth * lfoValue;
        ampModTarget = 1.0f - l.amp * 0.5f * (1.0f - static_cast<float> (lfoValue));
    }
    else
    {
        lfoPulseWidth = 0.0;
        ampModTarget = 1.0f;
    }

    if (jump)
        ampMod = ampModTarget;

    const auto& f = control.filter;
    const double target = f.cutoffOctaves
                        + control.lfo.cutoffOctaves * lfoValue
                        + f.envOctaves * static_cast<double> (filterEnv.getLevel())
                        + f.keyTrack * (note - 60) / 12.0
                        + f.velocityOctaves * (static_cast<double> (velocity) - 1.0);

    if (jump)
        cutoffLagOctaves = target;
    else
        cutoffLagOctaves += (target - cutoffLagOctaves) * f.lagCoeff;

    const float targetG = filter::cutoffToG (std::exp2 (std::clamp (cutoffLagOctaves, 0.0, 16.0)), control.sampleRate);

    if (jump)
    {
        filterG = targetG;
        filterGStep = 0.0f;
        filterRampLeft = 0;
    }
    else
    {
        filterGStep = (targetG - filterG) / static_cast<float> (controlBlockSize);
        filterRampLeft = controlBlockSize;
    }

    // Vibrato の Delay はボイスごと (ノートオンからの経過時間)。LFO の位相は全ボイス共通。
    double vibratoGate = 1.0;

    if (control.vibratoDelaySamples > 0.0)
    {
        const double x = std::clamp ((static_cast<double> (samplesSinceStart) - control.vibratoDelaySamples) / control.vibratoFadeSamples, 0.0, 1.0);
        vibratoGate = x * x * (3.0 - 2.0 * x);
    }

    pitchSnapOffset = control.transient.pitchOffsetAt (static_cast<double> (samplesSinceStart))
                    + control.pitchBendSemitones
                    + control.vibratoCents * vibratoGate / 100.0
                    + control.lfo.pitchSemitones * lfoValue;
}

void Voice::keyReleased (bool pedalDown) noexcept
{
    keyDown = false;

    if (pedalDown)
    {
        sustained = true;
        return;
    }

    if (! pendingStart)
        releaseEnvelopes();
}

void Voice::pedalReleased() noexcept
{
    if (! sustained)
        return;

    sustained = false;

    if (! keyDown && ! pendingStart)
        releaseEnvelopes();
}

void Voice::forceRelease() noexcept
{
    keyDown = false;
    sustained = false;

    if (! pendingStart)
        releaseEnvelopes();
}

void Voice::kill() noexcept
{
    keyDown = false;
    sustained = false;
    pendingStart = false;

    if (active && fadeSamplesLeft <= 0)
        fadeSamplesLeft = fadeLength;
}

void Voice::render (float* left, float* right, int numSamples, const VoiceControl& control, const VoiceLevels& levels) noexcept
{
    int offset = 0;

    while (active && offset < numSamples)
    {
        int chunk = numSamples - offset;

        if (fadeSamplesLeft > 0)
            chunk = std::min (chunk, fadeSamplesLeft);

        const bool wasFading = fadeSamplesLeft > 0;
        renderChunk (left + offset, right + offset, chunk, control, levels, offset);
        offset += chunk;

        const bool fadeFinished = wasFading && fadeSamplesLeft == 0;

        if (pendingStart && (fadeFinished || ! amp.isActive()))
        {
            // 古い音が消えたので、待たせていた新しいノートを開始する
            beginNote (control);
        }
        else if (fadeFinished || ! amp.isActive())
        {
            amp.reset();
            filterEnv.reset();
            noiseBurst.stop();
            fadeSamplesLeft = 0;
            active = false;
        }
    }
}

void Voice::renderChunk (float* left, float* right, int numSamples, const VoiceControl& control, const VoiceLevels& levels, int offset) noexcept
{
    const float* levelA = levels.a + offset;
    const float* levelB = levels.b + offset;

    float aL[controlBlockSize] = {};
    float aR[controlBlockSize] = {};
    float bL[controlBlockSize] = {};
    float bR[controlBlockSize] = {};

    const double inverseSampleRate = 1.0 / control.sampleRate;
    const double basePitch = glidePitch + pitchSnapOffset; // Pitch Snap は OSC A/B 共通 (各 OSC の Tune は保持)

    // LFO → Pulse Width (深さ 0 のときは元の値をそのまま使う)
    const auto widthFor = [this, &control] (double base)
    {
        return control.lfo.active ? std::clamp (base + lfoPulseWidth, 0.05, 0.95) : base;
    };

    if (control.oscA.enabled)
        oscA.render (aL, aR, numSamples, control.oscA.waveform,
                     noteToHz (basePitch + control.oscA.pitchOffset) * inverseSampleRate,
                     control.oscA.layout, widthFor (control.oscA.pulseWidth));

    if (control.oscB.enabled)
        oscB.render (bL, bR, numSamples, control.oscB.waveform,
                     noteToHz (basePitch + control.oscB.pitchOffset) * inverseSampleRate,
                     control.oscB.layout, widthFor (control.oscB.pulseWidth));

    // Voice Mix: OSC A + OSC B + Transient → Filter → Amp (SPEC §2)
    float mixL[controlBlockSize];
    float mixR[controlBlockSize];

    for (int i = 0; i < numSamples; ++i)
    {
        mixL[i] = aL[i] * levelA[i] + bL[i] * levelB[i];
        mixR[i] = aR[i] * levelA[i] + bR[i] * levelB[i];
    }

    if (control.sub.enabled)
    {
        // Sub: ユニゾンなしの 1 声をセンターに足す
        const float* levelSub = levels.sub + offset;
        const double inc = std::min (0.45, noteToHz (basePitch + control.sub.pitchOffset) * inverseSampleRate);

        for (int i = 0; i < numSamples; ++i)
        {
            const double v = control.sub.wave == SubWave::square ? osc::pulse (subPhase, inc, 0.5) : osc::sine (subPhase);
            const float s = static_cast<float> (v) * levelSub[i];
            mixL[i] += s;
            mixR[i] += s;
            subPhase = osc::wrap01 (subPhase + inc);
        }
    }

    if (control.noise.enabled)
    {
        const float* levelNoise = levels.noise + offset;

        for (int i = 0; i < numSamples; ++i)
        {
            noiseState ^= noiseState << 13;
            noiseState ^= noiseState >> 17;
            noiseState ^= noiseState << 5;
            const float white = static_cast<float> (noiseState >> 8) * (2.0f / 16777216.0f) - 1.0f;

            noiseLowPass += (white - noiseLowPass) * control.noise.colourCoeff;
            const float s = (control.noise.colourHighPass ? white - noiseLowPass : noiseLowPass) * control.noise.gain * levelNoise[i];
            mixL[i] += s;
            mixR[i] += s;
        }
    }

    if (noiseBurst.isActive())
        noiseBurst.render (mixL, mixR, numSamples, control.transient);

    const int rampSamples = std::min (numSamples, filterRampLeft);
    const float gStep = filterRampLeft > 0 ? filterGStep : 0.0f;

    if (rampSamples > 0)
        filter.process (mixL, mixR, rampSamples, control.filter.settings, filterG, gStep);

    filterG += gStep * static_cast<float> (rampSamples);
    filterRampLeft -= rampSamples;

    if (rampSamples < numSamples)
        filter.process (mixL + rampSamples, mixR + rampSamples, numSamples - rampSamples, control.filter.settings, filterG, 0.0f);

    // ベロシティ → 音量。Mono の Retrigger や Legato で鳴っている途中にベロシティが変わると、そのまま掛けると
    // 1 サンプルで音量が跳んでクリックになる。約 5 ms (48 kHz) でなめらかに追わせる
    constexpr float velocityFollow = 0.004f;
    const float fadeStep = 1.0f / static_cast<float> (fadeLength);

    for (int i = 0; i < numSamples; ++i)
    {
        filterEnv.next (control.filter.env);
        ampMod += (ampModTarget - ampMod) * 0.06f;
        gainVelocity += ((pendingStart ? gainVelocity : velocity) - gainVelocity) * velocityFollow;
        const float baseGain = (1.0f - control.velocityToAmp + control.velocityToAmp * gainVelocity) * voiceGain;
        float gain = amp.next (control.amp) * baseGain * ampMod;

        if (fadeSamplesLeft > 0)
        {
            gain *= static_cast<float> (fadeSamplesLeft) * fadeStep;
            --fadeSamplesLeft;
        }

        left[i] += mixL[i] * gain;
        right[i] += mixR[i] * gain;
    }

    samplesSinceStart += numSamples;
}

} // namespace polybrisk
