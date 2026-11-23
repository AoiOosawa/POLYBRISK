#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "state/Parameters.h"
#include "ui/Theme.h"

#include <array>
#include <functional>
#include <memory>
#include <vector>

/** パラメーターに結び付いた基本コントロール (ノブ・選択・トグル) と、それらを並べるセクション。
    UI は APVTS と state 層だけを使い、DSP には触れない (CLAUDE.md ルール 5)。 */
namespace polybrisk::ui
{

/** ドラッグ・Shift で微調整・ダブルクリックで既定値・数値入力ができるスライダー (SPEC §13)。 */
class FineSlider final : public juce::Slider
{
public:
    using juce::Slider::Slider;

    void mouseDown (const juce::MouseEvent& e) override
    {
        setMouseDragSensitivity (e.mods.isShiftDown() ? 1400 : 220);
        juce::Slider::mouseDown (e);
    }
};

/** 連続値・整数パラメーター用のノブ。名前を上、値を下に表示する。 */
class ParamKnob final : public juce::Component
{
public:
    ParamKnob (juce::AudioProcessorValueTreeState& apvts, const char* parameterId, const juce::String& labelText, int size = 60);

    void setLabelText (const juce::String& text) { label.setText (text, juce::dontSendNotification); }
    void resized() override;

    int preferredWidth() const noexcept { return juce::jmax (knobSize + 14, 70); }
    int preferredHeight() const noexcept { return knobSize + 38; }

    FineSlider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };

private:
    juce::Label label;
    int knobSize;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

/** 1 行に並ぶコントロールの共通の寸法。ラベルは上端、操作部の中心はノブの中心、値の表示は下端にそろえる。 */
namespace row
{
inline constexpr int height = 98;        // ノブ 1 個分 (ラベル 16 + ノブ 64 + 値 18)
inline constexpr int labelHeight = 16;
inline constexpr int centreY = 48;       // ノブの中心
inline constexpr int valueTop = 80;      // 値の表示の上端
inline constexpr int valueHeight = 18;
} // namespace row

/** 選択肢パラメーター用のコンボボックス。 */
class ParamChoice final : public juce::Component
{
public:
    ParamChoice (juce::AudioProcessorValueTreeState& apvts, const char* parameterId, const juce::String& labelText, int width = 118);

    void resized() override;
    int preferredWidth() const noexcept { return boxWidth; }
    int preferredHeight() const noexcept { return row::height; }

    /** ユーザーが選び直したときだけ呼ばれる (ホストやプリセットによる変更では呼ばれない)。 */
    std::function<void (int)> onUserChange;

    juce::ComboBox box;

private:
    juce::Label label;
    int boxWidth;
    juce::RangedAudioParameter& parameter;
    juce::ParameterAttachment attachment;
};

/** 縦型のスイッチ (上が ON)。ON の色は TextButton::buttonOnColourId を使う。 */
class SwitchButton final : public juce::Button
{
public:
    SwitchButton() : juce::Button ({}) { setClickingTogglesState (true); }

    /** ON の色。muted のあいだは彩度と明るさを落とす (ON だが、いまは効いていないことを示す)。 */
    juce::Colour onColour() const
    {
        const auto colour = findColour (juce::TextButton::buttonOnColourId);
        return muted ? colour.withMultipliedSaturation (0.22f).withMultipliedBrightness (0.62f) : colour;
    }

    void setMuted (bool shouldBeMuted)
    {
        if (muted != shouldBeMuted)
        {
            muted = shouldBeMuted;
            repaint();
        }
    }

    bool isMuted() const noexcept { return muted; }
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    bool muted = false;
};

/** ON/OFF パラメーター用のスイッチ。名前を上、スイッチを中央、ON / OFF の表示を下に置く。 */
class ParamToggle final : public juce::Component
{
public:
    ParamToggle (juce::AudioProcessorValueTreeState& apvts, const char* parameterId, const juce::String& text, int width = 74);

    void paint (juce::Graphics&) override;
    void resized() override;
    int preferredWidth() const noexcept { return buttonWidth; }
    int preferredHeight() const noexcept { return row::height; }

    /** ON でも効いていない状態 (FX の一括バイパス中など) として、色を落として表示する。 */
    void setMuted (bool shouldBeMuted)
    {
        if (button.isMuted() != shouldBeMuted)
        {
            button.setMuted (shouldBeMuted);
            repaint();
        }
    }

    SwitchButton button;

private:
    juce::Label label;
    int buttonWidth;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
};

/** パラメーターに結び付かない部品 (パターン選択など) に、ほかのコントロールと同じ位置のラベルを付ける。 */
class LabeledControl final : public juce::Component
{
public:
    LabeledControl (const juce::String& labelText, juce::Component& childToShow, int childHeight = 26);

    void resized() override;

private:
    juce::Label label;
    juce::Component& child;
    int childHeight;
};

/** ADSR の形を描く表示 (操作はノブで行う)。時間はノブの位置に比例した幅で描く。 */
class EnvelopeDisplay final : public juce::Component
{
public:
    EnvelopeDisplay (juce::AudioProcessorValueTreeState& apvts, const char* attackId, const char* decayId,
                     const char* sustainId, const char* releaseId);

    void paint (juce::Graphics&) override;

    /** 表示を更新する (タイマーから呼ぶ)。変化がなければ再描画しない。 */
    void refresh();

private:
    std::array<juce::RangedAudioParameter*, 4> parameters {};
    std::array<float, 4> shown { -1.0f, -1.0f, -1.0f, -1.0f };
};

/** タイトル付きのパネル。追加したコントロールを左から順に並べ、幅に収まらなければ折り返す。 */
class Section final : public juce::Component
{
public:
    explicit Section (const juce::String& titleText) : title (titleText) {}

    ParamKnob& knob (juce::AudioProcessorValueTreeState& apvts, const char* parameterId, const juce::String& labelText);
    ParamChoice& choice (juce::AudioProcessorValueTreeState& apvts, const char* parameterId, const juce::String& labelText, int width = 118);
    ParamToggle& toggle (juce::AudioProcessorValueTreeState& apvts, const char* parameterId, const juce::String& text, int width = 74);

    /** 任意のコンポーネントを並びに加える (所有はしない)。width が 0 以下なら行の幅いっぱいに広げる。 */
    void addCustom (juce::Component& component, int width, int height);

    /** 幅 width で並べたときに必要な高さ。 */
    int heightFor (int width) const;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int titleHeight = 26;
    static constexpr int padding = 10;
    static constexpr int rowUnit = row::height;   // 1 行の高さ。背の低い部品はノブの中心にそろえる

private:
    struct Item
    {
        juce::Component* component;
        int width, height;
    };

    template <typename Layout>
    int flow (int width, Layout&& place) const;

    juce::String title;
    std::vector<Item> items;
    std::vector<std::unique_ptr<juce::Component>> owned;
};

// --- ホスト (DAW) のキー操作を横取りしないための仕組み (D-076) ---------------------------------------------
// JUCE のボタンと選択欄は、クリックするとキーボードフォーカスを受け取り、そのあとの Return や矢印キーを自分で
// 処理する (ボタンが押し直される、選択が切り替わる)。プラグインでは、それがホストのキー操作の横取りになる。
// 方針: キーを受け取ってよいのは文字の入力欄だけ。ほかの部品がフォーカスを受け取ったら、エディターがすぐに
// 「受け皿」へ移す (PolyBriskAudioProcessorEditor::globalFocusChanged)。受け皿はキーを処理しないので、ホストへ渡る。
//
// 部品ごとに setWantsKeyboardFocus (false) を設定する方法は採らない: JUCE は、有効 / 無効の切り替えや見た目の
// 更新のたびにその設定を元に戻し (Label、ComboBox)、表示していないタブの部品は階層にいないので設定も届かない。

/** キーボードフォーカスの受け皿にする。受け皿はフォーカスを受け取るが、キーは処理しない。 */
void markAsFocusSink (juce::Component& component);
bool isFocusSink (const juce::Component& component);

/** この部品がキーボードフォーカスを持ち続けてよいか。文字の入力欄 (juce::TextEditor) とその内側、受け皿なら true。 */
bool mayHoldKeyboardFocus (const juce::Component& component);

/** root 以下の文字の入力欄が、入力を終えたらフォーカスを手放すようにする: Esc、および 1 行の欄の Return。
    既にある Return / Esc の処理は残し、そのあとに手放す。入力欄を作り直したら、もう一度呼ぶ。何度呼んでもよい。 */
void releaseKeysAfterTextEntry (juce::Component& root);

/** ヘッダーの OUT の下に置く、細いステレオの出力メーター (D-071)。
    左右 1 本ずつの横棒で、目盛りは -48〜0 dB。ピークを表示し、時間とともに下がる。
    安全用リミッターが効く大きさ (約 -2 dBFS 以上) になると、OUT の下に LIMIT と出す。操作はできない (クリックは下の部品へ通す)。 */
class OutputMeter final : public juce::Component
{
public:
    OutputMeter() { setInterceptsMouseClicks (false, false); }

    /** 新しいピーク (0..1) を渡す。タイマーの周期ごとに呼ぶ。表示が変わったときだけ描き直す。 */
    void push (float left, float right);

    /** 棒を描く範囲の左端 (自分の座標)。これより左は LIMIT の表示に使う。 */
    void setBarLeft (int x) { barLeft = x; }

    void paint (juce::Graphics&) override;

private:
    float level[2] {};
    int barLeft = 0;
};

/** 画面内鍵盤の横に置くホイール (ピッチベンド / モジュレーション)。Standalone 用 (D-067)。
    ピッチベンドは中央が 0 で、マウスを離すと中央へ戻る。モジュレーションは下端が 0 で、離した位置に残る。
    値は onChange で通知する (ベンド: -1..1、モジュレーション: 0..1)。パラメーターではない。 */
class PerformanceWheel final : public juce::Component
{
public:
    enum class Kind { pitchBend, modulation };

    PerformanceWheel (Kind kind, const juce::String& labelText);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    float getValue() const noexcept { return value; }

    /** 外部の MIDI で動いた値を表示に反映する (onChange は呼ばない)。ドラッグ中は無視する。 */
    void setDisplayValue (float newValue);
    std::function<void (float)> onChange;

    static constexpr int labelHeight = 14;

private:
    juce::Rectangle<float> track() const;
    void setValue (float newValue);

    Kind kind;
    juce::String label;
    float value = 0.0f;
    float valueAtDown = 0.0f;
};

} // namespace polybrisk::ui
