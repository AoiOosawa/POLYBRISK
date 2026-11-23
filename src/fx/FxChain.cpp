#include "fx/FxChain.h"

namespace polybrisk
{

void FxChain::prepare (double newSampleRate)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
    drive.prepare (sampleRate);
    phaser.prepare (sampleRate);
    chorus.prepare (sampleRate);
    eq.prepare (sampleRate);
    imager.prepare (sampleRate);
    delay.prepare (sampleRate);
    reverb.prepare (sampleRate);

    switchStep = static_cast<float> (1.0 / (0.01 * sampleRate));
    mixSmoothing = static_cast<float> (1.0 - std::exp (-1.0 / (0.02 * sampleRate)));
    idleThreshold = static_cast<int> (0.25 * sampleRate);
    reset();
}

void FxChain::reset() noexcept
{
    drive.reset();
    phaser.reset();
    chorus.reset();
    eq.reset();
    imager.reset();
    delay.reset();
    reverb.reset();

    // ON/OFF の状態は次の process() で即座に反映する (リセット直後にフェードインさせない)
    for (auto* sw : { &driveSwitch, &phaserSwitch, &chorusSwitch, &eqSwitch, &imagerSwitch, &delaySwitch, &reverbSwitch })
        *sw = {};

    mixInitialised = false;
    quietSamples = 0;
    idle = true;
}

template <typename Fx, typename Params>
void FxChain::insert (Fx& fx, Switch& sw, bool enabled, const Params& params, float* left, float* right, int numSamples) noexcept
{
    const float target = enabled ? 1.0f : 0.0f;

    if (sw.gain <= 0.0f && ! enabled)
    {
        if (sw.wasActive)
        {
            fx.reset();     // 次に ON にしたとき、古い内部状態が鳴らないようにする
            sw.wasActive = false;
        }

        return;
    }

    sw.wasActive = true;

    float dryL[maxBlock], dryR[maxBlock];
    std::copy (left, left + numSamples, dryL);
    std::copy (right, right + numSamples, dryR);

    fx.process (left, right, numSamples, params);

    if (sw.gain >= 1.0f && enabled)
        return;

    for (int i = 0; i < numSamples; ++i)
    {
        sw.gain = std::clamp (sw.gain + (target > sw.gain ? switchStep : -switchStep), 0.0f, 1.0f);
        left[i] = dryL[i] + (left[i] - dryL[i]) * sw.gain;
        right[i] = dryR[i] + (right[i] - dryR[i]) * sw.gain;
    }
}

void FxChain::process (float* left, float* right, int numSamples, const FxParams& p, double bpm, const float* gateLeft, const float* gateRight) noexcept
{
    numSamples = std::min (numSamples, maxBlock);
    const bool on = ! p.bypassAll;

    const bool anyEnabled = on && (p.drive.enabled || p.phaser.enabled || p.chorus.enabled || p.eq.enabled || p.imager.enabled || p.delay.enabled || p.reverb.enabled);
    const bool anyFading = driveSwitch.gain > 0.0f || phaserSwitch.gain > 0.0f || chorusSwitch.gain > 0.0f || eqSwitch.gain > 0.0f
                        || imagerSwitch.gain > 0.0f || delaySwitch.gain > 0.0f || reverbSwitch.gain > 0.0f;

    bool inputSilent = true;
    for (int i = 0; i < numSamples; ++i)
        inputSilent = inputSilent && left[i] == 0.0f && right[i] == 0.0f;

    // 初回 (リセット直後) は、設定どおりの状態からフェードなしで始める
    if (idle && ! anyFading && ! mixInitialised)
    {
        driveSwitch.gain = on && p.drive.enabled ? 1.0f : 0.0f;
        phaserSwitch.gain = on && p.phaser.enabled ? 1.0f : 0.0f;
        chorusSwitch.gain = on && p.chorus.enabled ? 1.0f : 0.0f;
        eqSwitch.gain = on && p.eq.enabled ? 1.0f : 0.0f;
        imagerSwitch.gain = on && p.imager.enabled ? 1.0f : 0.0f;
        delaySwitch.gain = on && p.delay.enabled ? 1.0f : 0.0f;
        reverbSwitch.gain = on && p.reverb.enabled ? 1.0f : 0.0f;
        delayMix = std::clamp (p.delay.mix, 0.0f, 1.0f);
        reverbMix = std::clamp (p.reverb.mix, 0.0f, 1.0f);
        mixInitialised = true;
    }

    // すべて OFF なら完全な素通し。無音が続いて尾も消えていれば何もしない。
    if (! anyEnabled && ! anyFading)
    {
        // フェードアウトし終えた FX の内部状態を捨てる (次に ON にしたとき、古い音が鳴らないように)
        const auto discard = [] (auto& fx, Switch& sw)
        {
            if (sw.wasActive)
            {
                fx.reset();
                sw.wasActive = false;
            }
        };

        discard (drive, driveSwitch);
        discard (phaser, phaserSwitch);
        discard (chorus, chorusSwitch);
        discard (eq, eqSwitch);
        discard (imager, imagerSwitch);
        discard (delay, delaySwitch);
        discard (reverb, reverbSwitch);

        if (gateLeft != nullptr && gateRight != nullptr)
            for (int i = 0; i < numSamples; ++i)
            {
                left[i] *= gateLeft[i];
                right[i] *= gateRight[i];
            }

        idle = inputSilent;
        return;
    }

    if (idle && inputSilent)
        return;

    idle = false;

    insert (drive, driveSwitch, on && p.drive.enabled, p.drive, left, right, numSamples);
    insert (phaser, phaserSwitch, on && p.phaser.enabled, p.phaser, left, right, numSamples);
    insert (chorus, chorusSwitch, on && p.chorus.enabled, p.chorus, left, right, numSamples);
    insert (eq, eqSwitch, on && p.eq.enabled, p.eq, left, right, numSamples);
    insert (imager, imagerSwitch, on && p.imager.enabled, p.imager, left, right, numSamples);

    // --- Trance Gate: 全ボイス合計に掛かる左右のゲイン (Stereo が OFF なら左右同じ)。刻んだ音の反復・残響は後段で自然に残る ---
    if (gateLeft != nullptr && gateRight != nullptr)
        for (int i = 0; i < numSamples; ++i)
        {
            left[i] *= gateLeft[i];
            right[i] *= gateRight[i];
        }

    // --- Delay / Reverb: 並列の wet パス ---
    const bool delayOn = on && p.delay.enabled;
    const bool reverbOn = on && p.reverb.enabled;
    const bool runDelay = delayOn || delaySwitch.gain > 0.0f;
    const bool runReverb = reverbOn || reverbSwitch.gain > 0.0f;

    if (! runDelay && delaySwitch.wasActive)
    {
        delay.reset();
        delaySwitch.wasActive = false;
    }

    if (! runReverb && reverbSwitch.wasActive)
    {
        reverb.reset();
        reverbSwitch.wasActive = false;
    }

    if (runDelay || runReverb)
    {
        // センドのゲイン (ON/OFF のフェード) を先に求め、入力と出力の両方に掛ける。
        // 入力側にも掛けることで、ON にした瞬間の波形がそのまま反復・残響に入るのを防ぐ。
        float delayGain[maxBlock], reverbGain[maxBlock];
        float sendL[maxBlock], sendR[maxBlock];
        float delayL[maxBlock] = {}, delayR[maxBlock] = {};
        float reverbL[maxBlock] = {}, reverbR[maxBlock] = {};

        for (int i = 0; i < numSamples; ++i)
        {
            delaySwitch.gain = std::clamp (delaySwitch.gain + (delayOn ? switchStep : -switchStep), 0.0f, 1.0f);
            reverbSwitch.gain = std::clamp (reverbSwitch.gain + (reverbOn ? switchStep : -switchStep), 0.0f, 1.0f);
            delayGain[i] = delaySwitch.gain;
            reverbGain[i] = reverbSwitch.gain;
        }

        if (runDelay)
        {
            for (int i = 0; i < numSamples; ++i)
            {
                sendL[i] = left[i] * delayGain[i];
                sendR[i] = right[i] * delayGain[i];
            }

            delay.process (sendL, sendR, delayL, delayR, numSamples, p.delay, bpm);
            delaySwitch.wasActive = true;
        }

        if (runReverb)
        {
            for (int i = 0; i < numSamples; ++i)
            {
                sendL[i] = left[i] * reverbGain[i];
                sendR[i] = right[i] * reverbGain[i];
            }

            reverb.process (sendL, sendR, reverbL, reverbR, numSamples, p.reverb);
            reverbSwitch.wasActive = true;
        }

        const float targetDelayMix = std::clamp (p.delay.mix, 0.0f, 1.0f);
        const float targetReverbMix = std::clamp (p.reverb.mix, 0.0f, 1.0f);

        for (int i = 0; i < numSamples; ++i)
        {
            delayMix += (targetDelayMix - delayMix) * mixSmoothing;
            reverbMix += (targetReverbMix - reverbMix) * mixSmoothing;

            const float dg = delayGain[i];
            const float rg = reverbGain[i];
            const float dry = (1.0f + (dryGainFor (delayMix) - 1.0f) * dg) * (1.0f + (dryGainFor (reverbMix) - 1.0f) * rg);
            const float dw = wetGainFor (delayMix) * dg;
            const float rw = wetGainFor (reverbMix) * rg;

            left[i] = left[i] * dry + delayL[i] * dw + reverbL[i] * rw;
            right[i] = right[i] * dry + delayR[i] * dw + reverbR[i] * rw;
        }
    }

    // 入力が無音で出力も十分に小さい状態が続いたら、内部状態を捨てて完全な無音にする
    float peak = 0.0f;
    for (int i = 0; i < numSamples; ++i)
        peak = std::max ({ peak, std::abs (left[i]), std::abs (right[i]) });

    if (inputSilent && peak < 1.0e-6f)
        quietSamples += numSamples;
    else
        quietSamples = 0;

    // ディレイは次の反復まで、リバーブは Pre-delay の分だけ無音が続くことがあるので、長めに待つ
    const int threshold = runDelay ? static_cast<int> (3.0 * sampleRate)
                                   : (runReverb ? static_cast<int> (0.6 * sampleRate) : idleThreshold);

    if (quietSamples >= threshold)
    {
        drive.reset();
        phaser.reset();
        chorus.reset();
        eq.reset();
        imager.reset();
        delay.reset();
        reverb.reset();
        quietSamples = 0;
        idle = true;

        for (int i = 0; i < numSamples; ++i)
            left[i] = right[i] = 0.0f;
    }
}

} // namespace polybrisk
