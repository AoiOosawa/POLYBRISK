#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "state/MacroEngine.h"
#include "state/MetaState.h"
#include "state/PresetManager.h"
#include "synth/SynthEngine.h"

#include <array>
#include <vector>

class PolyBriskAudioProcessor final : public juce::AudioProcessor,
                                      private juce::AudioProcessorValueTreeState::Listener,
                                      private juce::Timer
{
public:
    /** presetRoot を省略すると既定の場所 (Application Support/POLYBRISK) を使う。テストでは一時フォルダーを渡す。 */
    explicit PolyBriskAudioProcessor (const juce::File& presetRoot = {});
    ~PolyBriskAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "POLYBRISK"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 20.0; }   // Reverb Decay 最大 15 s + Delay

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getValueTreeState() noexcept { return apvts; }

    /** マクロ名・割当・CC (ホストパラメーターにしない状態)。変更は自動的にオーディオスレッドへ渡る。 */
    polybrisk::MetaState& getMetaState() noexcept { return meta; }

    polybrisk::PresetManager& getPresetManager() noexcept { return presets; }

    /** Standalone の画面内鍵盤・UI からのノート入力。 */
    juce::MidiKeyboardState& getKeyboardState() noexcept { return keyboardState; }

    /** 画面内のホイール (Standalone) からのピッチベンド (-1..1) とモジュレーション (0..1)。
        次のブロックの先頭でエンジンへ渡す。外部の MIDI と同じ値を上書きする (後から来た方が有効)。 */
    void setUiPitchBend (float normalised) noexcept { uiPitchBend.store (normalised); uiPitchBendDirty.store (true); }
    void setUiModWheel (float normalised) noexcept { uiModWheel.store (normalised); uiModWheelDirty.store (true); }

    /** 外部の MIDI で届いたピッチベンド / モジュレーションを、画面のホイールの表示用に取り出す。
        前回の呼び出しのあとに届いていれば true を返し、値を書く。メッセージスレッド用。 */
    bool takeMidiPitchBend (float& normalised) noexcept
    {
        if (! midiPitchBendDirty.exchange (false)) return false;
        normalised = midiPitchBend.load();
        return true;
    }

    bool takeMidiModWheel (float& normalised) noexcept
    {
        if (! midiModWheelDirty.exchange (false)) return false;
        normalised = midiModWheel.load();
        return true;
    }

    /** MIDI Learn: 次に届いた割当可能な CC を、指定したマクロ (0..3) に割り当てる。 */
    void beginMidiLearn (int macroIndex) noexcept;
    void cancelMidiLearn() noexcept { learnTarget.store (-1); }
    int getMidiLearnTarget() const noexcept { return learnTarget.load(); }

    /** オーディオスレッドで受けた CC の結果 (マクロ値・MIDI Learn) をパラメーターと MetaState に反映する。
        タイマーから定期的に呼ばれる。メッセージスレッド専用。 */
    void syncFromAudioThread();

    /** 出力メーター用: 前回の呼び出しからあとの出力のピーク (絶対値の最大、0..1)。読むと 0 に戻る。メッセージスレッド用。 */
    float takeOutputPeak (int channel) noexcept { return outputPeak[static_cast<size_t> (channel & 1)].exchange (0.0f, std::memory_order_relaxed); }

    /** Trance Gate がいま再生しているステップ (UI のインジケーター用。動作していなければ -1)。 */
    int getGateStep() const noexcept { return gateStepForUi.load (std::memory_order_relaxed); }

    /** マクロ適用後の実効値 (実値)。UI の表示用。任意のスレッドから読める。 */
    float getEffectiveValue (int parameterIndex) const noexcept;

private:
    void timerCallback() override { syncFromAudioThread(); }
    void parameterChanged (const juce::String&, float) override { parametersTouched.store (true, std::memory_order_relaxed); }

    void pushMacroSnapshot();
    void handleMidiMessage (const juce::MidiMessage& message) noexcept;
    void computeEffectiveValues (int numSamples) noexcept;
    polybrisk::EngineParams makeEngineParams() const noexcept;

    juce::AudioProcessorValueTreeState apvts;
    polybrisk::MetaState meta;
    polybrisk::PresetManager presets;
    juce::MidiKeyboardState keyboardState;

    std::atomic<bool> parametersTouched { false };  // 任意のスレッドから立つ。Dirty 表示への反映はタイマーで行う
    std::atomic<float> uiPitchBend { 0.0f }, uiModWheel { 0.0f };             // 画面内のホイール (D-067)
    std::atomic<bool> uiPitchBendDirty { false }, uiModWheelDirty { false };
    std::atomic<float> midiPitchBend { 0.0f }, midiModWheel { 0.0f };         // 外部 MIDI の値 (表示用)
    std::atomic<bool> midiPitchBendDirty { false }, midiModWheelDirty { false };
    std::atomic<bool> arpResetRequested { false };  // プリセット切替・状態復元の後、オーディオスレッドで処理する

    // --- パラメーター: レジストリ順の atomic と、その添字 (構築時に一度だけ解決) ---
    static constexpr int maxParameters = 256;
    std::vector<std::atomic<float>*> rawValues;
    std::array<float, maxParameters> effective {};                  // オーディオスレッド: マクロ適用後の実値
    std::array<std::atomic<float>, maxParameters> effectiveForUi {}; // UI 表示用のコピー

    struct OscIndex
    {
        int enabled, wave, octave, semitone, fine, level, unisonVoices, detune, spread, phaseMode, pulseWidth, unisonMix;
    };

    struct Index
    {
        OscIndex oscA, oscB;
        int ampAttack, ampDecay, ampSustain, ampRelease, ampVelocity;
        int transientEnabled, transientMode, transientAmount, transientDuration, transientNoiseColor, transientPitchSnap;
        int filterCharacter, filterMode, filterCutoff, filterResonance, filterDrive, filterEnvAmount, filterKeyTrack, filterVelocity;
        int filterEnvAttack, filterEnvDecay, filterEnvSustain, filterEnvRelease;
        int bendUp, bendDown, bendMode, vibratoDepth, vibratoRate, vibratoDelay, aftertouchVibrato, aftertouchCutoff;
        int voiceMode, voiceGlide, subLevel, subOctave, subWave, noiseLevel, noiseColor;
        int lfoWave, lfoSync, lfoRate, lfoDivision, lfoFade, lfoRetrigger, lfoCutoff, lfoPulseWidth, lfoPitch, lfoAmp;
        int phaserEnabled, phaserType, phaserRate, phaserDepth, phaserFeedback, phaserMix;
        std::array<int, polybrisk::macro::numMacros> macros;
        int fxBypass;
        int driveEnabled, driveType, driveDrive, driveTone, driveMix, driveOutput, driveAutoGain;
        int chorusEnabled, chorusType, chorusRate, chorusDepth, chorusWidth, chorusMix;
        int eqEnabled, eqLowGain, eqMidFreq, eqMidGain, eqHighGain;
        int imagerEnabled, imagerWidth, imagerMonoBass;
        int delayEnabled, delayMode, delaySync, delayDivision, delayTime, delayFeedback, delayTone, delayMix, delayDucking;
        int reverbEnabled, reverbType, reverbSize, reverbDecay, reverbPreDelay, reverbTone, reverbMix;
        int gateEnabled, gateSteps, gateRate, gateDepth, gateSmooth, gateSwing, gateRestart, gateStereo, gateWidth;
        int arpEnabled, arpMode, arpRate, arpOctaves, arpGate, arpSwing, arpVelocity, arpLatch, arpRestart;
        int outputGain, presetLevel;
    };

    Index index {};

    // --- マクロ: メッセージスレッド → オーディオスレッドの受け渡し (オーディオ側は try-lock のみ) ---
    juce::SpinLock snapshotLock;
    polybrisk::macro::Snapshot pendingSnapshot;     // snapshotLock で保護
    polybrisk::GatePattern pendingGatePattern;      // snapshotLock で保護
    std::atomic<bool> snapshotDirty { false };
    polybrisk::macro::Snapshot activeSnapshot;      // オーディオスレッド専用
    polybrisk::GatePattern activeGatePattern;       // オーディオスレッド専用

    struct MacroRuntime
    {
        float smoothed = 0.5f;
        bool ccActive = false;      // CC の値がパラメーターより新しい
        float ccValue = 0.5f;       // 0..1
        float rawAtCc = 50.0f;      // CC を受けた時点のパラメーター値 (%)
    };

    std::array<MacroRuntime, polybrisk::macro::numMacros> macroRuntime;   // オーディオスレッド専用
    bool macrosInitialised = false;

    // --- オーディオスレッド → メッセージスレッド ---
    std::array<std::atomic<float>, polybrisk::macro::numMacros> pendingMacroValue;  // % (負 = なし)
    std::atomic<int> learnTarget { -1 };
    std::atomic<int> learnResult { -1 };    // (macro << 8) | cc

    std::atomic<int> gateStepForUi { -1 };
    std::array<std::atomic<float>, 2> outputPeak {};    // 出力メーター用 (D-071)。画面が読むまでの最大値を持つ
    double currentSampleRate = 44100.0;
    polybrisk::SynthEngine engine;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PolyBriskAudioProcessor)
};
