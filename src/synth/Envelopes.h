#pragma once

namespace polybrisk
{

/** ADSR の設定値 (時間は秒、sustain は 0..1)。 */
struct AdsrParams
{
    float attack = 0.002f;
    float decay = 0.3f;
    float sustain = 0.8f;
    float release = 0.2f;
};

/** サンプルレートに依存する係数。全ボイス共通なのでエンジンが 1 回だけ計算する。 */
struct AdsrCoefficients
{
    // 長い Decay では係数が 1 に極めて近くなるため、float では sustain の手前で止まってしまう。double で保持する。
    double attackStep = 1.0;    // 1 サンプルあたりの線形増分
    double decayCoeff = 0.0;    // sustain への指数収束係数
    double sustain = 1.0;
    double releaseCoeff = 0.0;  // 0 への指数収束係数

    static AdsrCoefficients make (const AdsrParams& params, double sampleRate) noexcept;
};

/** ADSR エンベロープ。
    - Attack: 指定時間で 0→1 の線形。
    - Decay: 指定時間で sustain との差が -60 dB になる指数カーブ (Sustain 段を兼ねる)。
      Sustain が 0 のときは -100 dB を下回ると停止する。
    - Release: 指定時間で -60 dB になる指数カーブ。-100 dB を下回ると停止する。 */
class Adsr
{
public:
    enum class Stage { idle, attack, decay, release };

    void reset() noexcept { stage = Stage::idle; level = 0.0; }
    void noteOn() noexcept { stage = Stage::attack; }
    void noteOff() noexcept { if (stage != Stage::idle) stage = Stage::release; }

    float next (const AdsrCoefficients& c) noexcept
    {
        switch (stage)
        {
            case Stage::attack:
                level += c.attackStep;
                if (level >= 1.0)
                {
                    level = 1.0;
                    stage = Stage::decay;
                }
                break;

            case Stage::decay:
                level = c.sustain + (level - c.sustain) * c.decayCoeff;

                // Sustain 0 の音 (Stab など) は、鍵盤を押さえたままでも減衰しきったら止めてボイスを空ける
                if (c.sustain <= 0.0 && level < silenceThreshold)
                {
                    level = 0.0;
                    stage = Stage::idle;
                }
                break;

            case Stage::release:
                level *= c.releaseCoeff;
                if (level < silenceThreshold)
                {
                    level = 0.0;
                    stage = Stage::idle;
                }
                break;

            case Stage::idle:
            default:
                break;
        }

        return static_cast<float> (level);
    }

    bool isActive() const noexcept { return stage != Stage::idle; }
    bool isReleasing() const noexcept { return stage == Stage::release; }
    float getLevel() const noexcept { return static_cast<float> (level); }
    Stage getStage() const noexcept { return stage; }

    static constexpr double silenceThreshold = 1.0e-5; // -100 dB

private:
    Stage stage = Stage::idle;
    double level = 0.0;
};

} // namespace polybrisk
