#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/** POLYBRISK の配色と LookAndFeel (SPEC §13: ダーク基調。配色は assets/branding と共通、D-013)。 */
namespace polybrisk::ui
{

namespace colours
{
inline const juce::Colour background { 0xff12131c };
inline const juce::Colour panel { 0xff1b1d2b };
inline const juce::Colour panelLight { 0xff262940 };
inline const juce::Colour outline { 0xff34385a };
inline const juce::Colour text { 0xffe8e9f3 };
inline const juce::Colour textDim { 0xff9a9db8 };
inline const juce::Colour accent { 0xffff6a3c };      // オレンジ
inline const juce::Colour accentWarm { 0xffffd23c };  // イエロー
inline const juce::Colour accentHot { 0xffff3c8e };   // ピンク
inline const juce::Colour cyan { 0xff3cd6ff };
inline const juce::Colour violet { 0xff8a6bff };
} // namespace colours

inline juce::Font font (float height, bool bold = false)
{
    return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
}

/** ロゴマーク (デチューンした鋸歯状波が重なるモチーフ) を area いっぱいに描く。 */
void drawLogoMark (juce::Graphics& g, juce::Rectangle<float> area);

class LookAndFeel final : public juce::LookAndFeel_V4
{
public:
    LookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                           float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos, float minSliderPos,
                           float maxSliderPos, juce::Slider::SliderStyle, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool highlighted, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getLabelFont (juce::Label&) override;
    juce::Font getPopupMenuFont() override;
    void drawTabButton (juce::TabBarButton&, juce::Graphics&, bool isMouseOver, bool isMouseDown) override;
    int getTabButtonBestWidth (juce::TabBarButton&, int tabDepth) override;
};

} // namespace polybrisk::ui
