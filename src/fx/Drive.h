#pragma once

#include "fx/FxParams.h"
#include "synth/Halfband.h"

#include <algorithm>
#include <cmath>

namespace polybrisk
{

/** DRIVE: 6 種類の非線形カーブ (SPEC §8.1)。4x オーバーサンプリングで処理する。
    名称は音の方向を表すだけで、特定の機材のエミュレーションではない。

    Mix: 歪ませた音は、オーバーサンプリングのフィルター (IIR ハーフバンド 4 段) を通るぶん、数サンプル遅れる
    (遅れは周波数によって違う)。原音をそのまま混ぜると、Mix の途中で高域にくし形の谷ができる (Mix 50 % で
    5 kHz 付近が約 -28 dB)。そのため原音側も、カーブだけを抜いた同じフィルターに通し、位相をそろえてから混ぜる (D-069)。 */
class Drive
{
public:
    static constexpr int oversamplingFactor = 4;

    /** 各カーブの 1 サンプル。入力はプリゲイン後。出力はおおむね ±1 に収まる。 */
    static float shape (DriveType type, float x) noexcept
    {
        switch (type)
        {
            case DriveType::tape:
                // tanh より膝がゆるく、大入力でもゆっくり詰まっていく
                return 0.63661977f * std::atan (1.5707963f * x);

            case DriveType::tube:
            {
                // 上下非対称 (偶数次倍音)。DC は後段で取り除く
                const float y = x >= 0.0f ? std::tanh (x) : 1.6f * std::tanh (x / 1.6f) * 0.8f;
                return y;
            }

            case DriveType::softClip:
            {
                const float c = std::clamp (x, -1.5f, 1.5f);
                return c - (4.0f / 27.0f) * c * c * c;
            }

            case DriveType::hardClip:
                return std::clamp (x, -1.0f, 1.0f);

            case DriveType::wavefold:
                // 正弦で折り返す。|x| > 1 で波形が畳まれ、倍音が大きく増える
                return std::sin (1.5707963f * x);

            case DriveType::warm:
            default:
                return std::tanh (x);
        }
    }

    void prepare (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate;
        dcCoeff = static_cast<float> (1.0 - 2.0 * 3.141592653589793 * 10.0 / sampleRate);
        smoothing = static_cast<float> (1.0 - std::exp (-1.0 / (0.01 * sampleRate)));
        reset();
    }

    void reset() noexcept
    {
        for (auto& ch : channels)
            ch = {};

        initialised = false;
    }

    void process (float* left, float* right, int numSamples, const DriveParams& p) noexcept
    {
        const float drive = std::clamp (p.drive, 0.0f, 1.0f);
        const float targetPre = std::pow (10.0f, drive * 30.0f / 20.0f);            // 0..+30 dB
        const float output = std::pow (10.0f, std::clamp (p.outputDb, -24.0f, 12.0f) / 20.0f);
        // Auto Gain: 歪ませても聴感の音量がおおむね変わらないよう、プリゲインの一部を打ち消す
        const float makeup = p.type == DriveType::tape ? 1.25f : (p.type == DriveType::wavefold ? 1.4f : 1.0f);
        const float targetPost = output * makeup * (p.autoGain ? std::pow (targetPre, -0.65f) : 1.0f);
        const float targetMix = std::clamp (p.mix, 0.0f, 1.0f);
        const double toneHz = 800.0 * std::pow (25.0, static_cast<double> (std::clamp (p.tone, 0.0f, 1.0f)));
        const float targetTone = static_cast<float> (1.0 - std::exp (-2.0 * 3.141592653589793 * std::min (toneHz, 0.45 * sampleRate) / sampleRate));

        if (! initialised)
        {
            pre = targetPre;
            post = targetPost;
            mix = targetMix;
            toneCoeff = targetTone;
            initialised = true;
        }

        float* const io[2] = { left, right };

        for (int i = 0; i < numSamples; ++i)
        {
            pre += (targetPre - pre) * smoothing;
            post += (targetPost - post) * smoothing;
            mix += (targetMix - mix) * smoothing;
            toneCoeff += (targetTone - toneCoeff) * smoothing;

            for (int c = 0; c < 2; ++c)
            {
                auto& ch = channels[c];
                const float dry = io[c][i];

                // 1x → 2x → 4x
                float up2[2];
                ch.up1.process (dry * pre, up2[0], up2[1]);

                float down2[2];
                for (int k = 0; k < 2; ++k)
                {
                    float up4[2];
                    ch.up2.process (up2[k], up4[0], up4[1]);
                    down2[k] = ch.down2.process (shape (p.type, up4[0]), shape (p.type, up4[1]));
                }

                float wet = ch.down1.process (down2[0], down2[1]);

                // 原音側: 同じフィルターをカーブなしで通す (歪ませた側と同じだけ遅らせる)
                float dryUp2[2];
                ch.dryUp1.process (dry, dryUp2[0], dryUp2[1]);

                float dryDown2[2];
                for (int k = 0; k < 2; ++k)
                {
                    float dryUp4[2];
                    ch.dryUp2.process (dryUp2[k], dryUp4[0], dryUp4[1]);
                    dryDown2[k] = ch.dryDown2.process (dryUp4[0], dryUp4[1]);
                }

                const float aligned = ch.dryDown1.process (dryDown2[0], dryDown2[1]);

                // DC 除去 (Tube の非対称カーブ用。他のカーブでは影響しない)
                const float blocked = wet - ch.dcX + dcCoeff * ch.dcY;
                ch.dcX = wet;
                ch.dcY = blocked;

                ch.tone += (blocked - ch.tone) * toneCoeff;
                io[c][i] = aligned + (ch.tone * post - aligned) * mix;
            }
        }
    }

private:
    struct Channel
    {
        HalfbandInterpolator up1, up2;
        HalfbandDecimator down1, down2;
        HalfbandInterpolator dryUp1, dryUp2;     // 原音側の位相合わせ用 (カーブなしで同じ経路を通す)
        HalfbandDecimator dryDown1, dryDown2;
        float dcX = 0.0f, dcY = 0.0f, tone = 0.0f;
    };

    Channel channels[2];
    double sampleRate = 48000.0;
    float dcCoeff = 0.999f;
    float smoothing = 1.0f;
    float pre = 1.0f, post = 1.0f, mix = 1.0f, toneCoeff = 1.0f;
    bool initialised = false;
};

} // namespace polybrisk
