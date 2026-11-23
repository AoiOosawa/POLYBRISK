#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "PluginProcessor.h"
#include "ui/Theme.h"
#include "ui/Views.h"

/** POLYBRISK のエディター (SPEC §9 / §13)。
    Simple と Advanced は同じ APVTS / MetaState を見ているだけなので、切り替えても音は変わらない。
    ウインドウは縦横比を保って拡大縮小でき、中身は基準サイズからのスケールで描く。 */
class PolyBriskAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                            private juce::Timer,
                                            private juce::FocusChangeListener
{
public:
    static constexpr int numAdvancedTabs = polybrisk::ui::AdvancedView::numTabs;

    explicit PolyBriskAudioProcessorEditor (PolyBriskAudioProcessor&);
    ~PolyBriskAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // スクリーンショット・テスト用
    void showAdvancedTab (int index);
    void showBrowser (bool show);

    /** focused がキーボードフォーカスを受け取ったとき、フォーカスの移し先を返す (移さなくてよければ nullptr)。
        この画面の中の、文字の入力欄でも受け皿でもない部品なら、受け皿を返す (D-076)。 */
    juce::Component* focusTargetFor (juce::Component* focused);

private:
    /** 基準サイズで配置する中身。エディターはこれを拡大縮小して表示する。 */
    class Content final : public juce::Component
    {
    public:
        explicit Content (PolyBriskAudioProcessorEditor& e) : owner (e) {}
        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        PolyBriskAudioProcessorEditor& owner;
    };

    void timerCallback() override;
    void globalFocusChanged (juce::Component* focused) override;
    void setAdvanced (bool advanced);
    void layoutContent();
    int baseHeight() const noexcept { return 700 + (showKeyboard ? keyboardHeight : 0); }

    static constexpr int baseWidth = 1040;
    static constexpr int headerHeight = 54;
    static constexpr int keyboardHeight = 84;
    static constexpr int wheelWidth = 34;           // ホイール 1 本の幅
    static constexpr int wheelAreaWidth = 2 * wheelWidth + 16;
    static constexpr int lowestKey = 21, highestKey = 108;   // A0..C8 の 88 鍵
    static constexpr int whiteKeys = 52;

    PolyBriskAudioProcessor& polyBrisk;
    polybrisk::ui::LookAndFeel lookAndFeel;
    bool showKeyboard = false;

    Content content { *this };
    juce::TextButton previous { "<" }, next { ">" }, presetName, save { "Save" }, simpleTab { "SIMPLE" }, advancedTab { "ADVANCED" };
    juce::Slider outputGain { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Label outputLabel;
    polybrisk::ui::OutputMeter outputMeter;     // OUT のスライダーの下の細いメーター
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> outputAttachment;
    polybrisk::ui::SimpleView simpleView;
    polybrisk::ui::AdvancedView advancedView;
    polybrisk::ui::PresetBrowser browser;
    juce::MidiKeyboardComponent keyboard;
    polybrisk::ui::PerformanceWheel bendWheel { polybrisk::ui::PerformanceWheel::Kind::pitchBend, "BEND" };
    polybrisk::ui::PerformanceWheel modWheel { polybrisk::ui::PerformanceWheel::Kind::modulation, "MOD" };
    juce::String shownPresetName;
    bool shownAdvanced = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PolyBriskAudioProcessorEditor)
};
