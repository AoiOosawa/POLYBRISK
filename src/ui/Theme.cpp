#include "ui/Theme.h"

namespace polybrisk::ui
{

void drawLogoMark (juce::Graphics& g, juce::Rectangle<float> area)
{
    const float size = juce::jmin (area.getWidth(), area.getHeight());
    const auto box = area.withSizeKeepingCentre (size, size);

    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2a2d4a), box.getX(), box.getY(),
                                             colours::background, box.getRight(), box.getBottom(), false));
    g.fillRoundedRectangle (box, size * 0.22f);

    // 3 本の鋸歯状波を少しずつずらして重ねる
    const juce::Colour waveColours[] = { colours::violet, colours::cyan, colours::accent };
    const float offsets[] = { -0.09f, 0.0f, 0.09f };

    for (int i = 0; i < 3; ++i)
    {
        juce::Path wave;
        const float left = box.getX() + size * 0.16f;
        const float width = size * 0.68f;
        const float middle = box.getCentreY() + size * offsets[i];
        const float height = size * 0.17f;
        const int teeth = 3;

        wave.startNewSubPath (left, middle + height);
        for (int t = 0; t < teeth; ++t)
        {
            const float x1 = left + width * static_cast<float> (t + 1) / teeth;
            wave.lineTo (x1, middle - height);
            if (t + 1 < teeth)
                wave.lineTo (x1, middle + height);
        }

        g.setColour (waveColours[i].withAlpha (i == 2 ? 1.0f : 0.75f));
        g.strokePath (wave, juce::PathStrokeType (size * 0.055f, juce::PathStrokeType::mitered, juce::PathStrokeType::rounded));
    }
}

LookAndFeel::LookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, colours::background);
    setColour (juce::Label::textColourId, colours::text);
    setColour (juce::Slider::textBoxTextColourId, colours::text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxHighlightColourId, colours::accent.withAlpha (0.4f));
    setColour (juce::TextButton::buttonColourId, colours::panelLight);
    setColour (juce::TextButton::buttonOnColourId, colours::accent);
    setColour (juce::TextButton::textColourOffId, colours::textDim);
    setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    setColour (juce::ComboBox::backgroundColourId, colours::panelLight);
    setColour (juce::ComboBox::outlineColourId, colours::outline);
    setColour (juce::ComboBox::textColourId, colours::text);
    setColour (juce::ComboBox::arrowColourId, colours::textDim);
    setColour (juce::PopupMenu::backgroundColourId, colours::panel);
    setColour (juce::PopupMenu::textColourId, colours::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, colours::accent);
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    setColour (juce::TextEditor::backgroundColourId, colours::background);
    setColour (juce::TextEditor::outlineColourId, colours::outline);
    setColour (juce::TextEditor::focusedOutlineColourId, colours::accent);
    setColour (juce::TextEditor::textColourId, colours::text);
    setColour (juce::TextEditor::highlightColourId, colours::accent.withAlpha (0.4f));
    setColour (juce::CaretComponent::caretColourId, colours::accent);
    setColour (juce::ListBox::backgroundColourId, colours::background);
    setColour (juce::ListBox::outlineColourId, colours::outline);
    setColour (juce::ScrollBar::thumbColourId, colours::outline);
    setColour (juce::TabbedComponent::backgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::TabbedComponent::outlineColourId, juce::Colours::transparentBlack);
    setColour (juce::TabbedButtonBar::tabOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::TabbedButtonBar::frontOutlineColourId, juce::Colours::transparentBlack);
}

void LookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                    float rotaryStartAngle, float rotaryEndAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (4.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
    const float thickness = juce::jmax (2.5f, radius * 0.16f);
    const float alpha = slider.isEnabled() ? 1.0f : 0.35f;

    juce::Path track;
    track.addCentredArc (centre.x, centre.y, radius - thickness * 0.5f, radius - thickness * 0.5f, 0.0f, rotaryStartAngle, rotaryEndAngle, true);
    g.setColour (colours::outline.withAlpha (alpha));
    g.strokePath (track, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // 中央が基準のノブ (バイポーラ) は、中央から値までを塗る
    const bool bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;
    const float from = bipolar ? rotaryStartAngle + static_cast<float> (slider.valueToProportionOfLength (0.0)) * (rotaryEndAngle - rotaryStartAngle)
                               : rotaryStartAngle;

    juce::Path value;
    value.addCentredArc (centre.x, centre.y, radius - thickness * 0.5f, radius - thickness * 0.5f, 0.0f,
                         juce::jmin (from, angle), juce::jmax (from, angle), true);
    g.setGradientFill (juce::ColourGradient (colours::accentWarm.withAlpha (alpha), bounds.getX(), bounds.getBottom(),
                                             colours::accentHot.withAlpha (alpha), bounds.getRight(), bounds.getY(), false));
    g.strokePath (value, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const float inner = radius - thickness * 1.6f;
    g.setColour (colours::panelLight.withAlpha (alpha));
    g.fillEllipse (centre.x - inner, centre.y - inner, inner * 2.0f, inner * 2.0f);

    juce::Path pointer;
    pointer.addRoundedRectangle (-thickness * 0.3f, -inner, thickness * 0.6f, inner * 0.55f, thickness * 0.3f);
    g.setColour (colours::text.withAlpha (alpha));
    g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (centre.x, centre.y));
}

void LookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos, float,
                                    float, juce::Slider::SliderStyle style, juce::Slider& slider)
{
    if (style != juce::Slider::LinearHorizontal && style != juce::Slider::LinearBar)
    {
        juce::LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, 0.0f, 0.0f, style, slider);
        return;
    }

    const auto track = juce::Rectangle<float> (static_cast<float> (x), static_cast<float> (y) + static_cast<float> (height) * 0.5f - 2.5f,
                                               static_cast<float> (width), 5.0f);
    g.setColour (colours::outline);
    g.fillRoundedRectangle (track, 2.5f);

    const bool bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;
    const float zero = bipolar ? static_cast<float> (x) + static_cast<float> (width) * static_cast<float> (slider.valueToProportionOfLength (0.0))
                               : static_cast<float> (x);
    g.setColour (colours::accent);
    g.fillRoundedRectangle (juce::Rectangle<float> (juce::jmin (zero, sliderPos), track.getY(), std::abs (sliderPos - zero), 5.0f), 2.5f);
    g.setColour (colours::text);
    g.fillEllipse (sliderPos - 6.0f, track.getCentreY() - 6.0f, 12.0f, 12.0f);
}

void LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour, bool highlighted, bool down)
{
    const auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    auto colour = backgroundColour;

    if (! button.isEnabled())
        colour = colour.withMultipliedAlpha (0.4f);
    else if (down)
        colour = colour.brighter (0.25f);
    else if (highlighted)
        colour = colour.brighter (0.12f);

    g.setColour (colour);
    g.fillRoundedRectangle (bounds, 5.0f);
    g.setColour (button.getToggleState() ? colour.brighter (0.3f) : colours::outline);
    g.drawRoundedRectangle (bounds, 5.0f, 1.0f);
}

juce::Font LookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight) { return font (juce::jmin (13.0f, static_cast<float> (buttonHeight) * 0.6f), true); }
juce::Font LookAndFeel::getComboBoxFont (juce::ComboBox&) { return font (13.0f); }
juce::Font LookAndFeel::getLabelFont (juce::Label& label) { return label.getFont(); }
juce::Font LookAndFeel::getPopupMenuFont() { return font (13.5f); }

void LookAndFeel::drawTabButton (juce::TabBarButton& button, juce::Graphics& g, bool isMouseOver, bool)
{
    auto area = button.getActiveArea().toFloat().reduced (2.0f, 3.0f);
    const bool front = button.isFrontTab();

    g.setColour (front ? colours::panelLight : (isMouseOver ? colours::panel.brighter (0.08f) : colours::panel));
    g.fillRoundedRectangle (area, 6.0f);

    if (front)
    {
        g.setColour (colours::accent);
        g.fillRoundedRectangle (area.removeFromBottom (3.0f).reduced (10.0f, 0.0f), 1.5f);
    }

    g.setColour (front ? colours::text : colours::textDim);
    g.setFont (font (14.0f, true));
    g.drawText (button.getButtonText(), button.getActiveArea(), juce::Justification::centred);
}

int LookAndFeel::getTabButtonBestWidth (juce::TabBarButton& button, int)
{
    return 40 + juce::GlyphArrangement::getStringWidthInt (font (14.0f, true), button.getButtonText());
}

} // namespace polybrisk::ui
