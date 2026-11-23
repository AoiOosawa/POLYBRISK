#include "ui/Controls.h"

namespace polybrisk::ui
{

namespace
{
juce::String formatValue (const params::Definition& d, double value)
{
    const juce::String unit = d.unit;

    if (d.kind == params::Kind::integer)
        return juce::String (juce::roundToInt (value)) + (unit.isNotEmpty() ? " " + unit : juce::String());

    const double magnitude = std::abs (value);
    const int decimals = unit == "Hz" ? (magnitude >= 100.0 ? 0 : (magnitude >= 10.0 ? 1 : 2))
                       : unit == "ms" ? (magnitude >= 100.0 ? 0 : 1)
                       : unit == "s"  ? 2
                       : 1;

    if (unit == "Hz" && magnitude >= 1000.0)
        return juce::String (value / 1000.0, 2) + " kHz";

    return juce::String (value, decimals) + (unit.isNotEmpty() ? " " + unit : juce::String());
}

void styleLabel (juce::Label& label, const juce::String& text)
{
    label.setText (text, juce::dontSendNotification);
    label.setFont (font (11.5f));
    label.setColour (juce::Label::textColourId, colours::textDim);
    label.setJustificationType (juce::Justification::centred);
    label.setMinimumHorizontalScale (0.7f);
    label.setInterceptsMouseClicks (false, false);
}
} // namespace

// ---------------------------------------------------------------------------
ParamKnob::ParamKnob (juce::AudioProcessorValueTreeState& apvts, const char* parameterId, const juce::String& labelText, int size)
    : knobSize (size)
{
    styleLabel (label, labelText);
    addAndMakeVisible (label);

    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, preferredWidth(), 18);
    slider.setColour (juce::Slider::textBoxTextColourId, colours::text);
    slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (slider);

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, parameterId, slider);

    if (const auto* d = params::find (parameterId))
    {
        const auto definition = *d;
        slider.textFromValueFunction = [definition] (double v) { return formatValue (definition, v); };
        slider.valueFromTextFunction = [] (const juce::String& text)
        {
            const auto cleaned = text.trim().toLowerCase();
            double value = cleaned.retainCharacters ("0123456789.-+e").getDoubleValue();
            if (cleaned.contains ("khz"))
                value *= 1000.0;
            return value;
        };
        slider.setDoubleClickReturnValue (true, static_cast<double> (definition.defaultValue));
        slider.updateText();
    }
}

void ParamKnob::resized()
{
    auto bounds = getLocalBounds();
    label.setBounds (bounds.removeFromTop (16));
    slider.setBounds (bounds);
}

// ---------------------------------------------------------------------------
ParamChoice::ParamChoice (juce::AudioProcessorValueTreeState& apvts, const char* parameterId, const juce::String& labelText, int width)
    : boxWidth (width),
      parameter (*apvts.getParameter (parameterId)),
      attachment (parameter, [this] (float value) { box.setSelectedItemIndex (juce::roundToInt (value), juce::dontSendNotification); })
{
    styleLabel (label, labelText);
    addAndMakeVisible (label);

    if (const auto* d = params::find (parameterId))
    {
        int itemId = 1;
        for (const auto* choiceName : d->choices)
            box.addItem (choiceName, itemId++);
    }

    // パラメーターからの反映は通知なしで行うので、onChange が呼ばれるのはユーザーの操作のときだけ
    box.onChange = [this]
    {
        const int index = box.getSelectedItemIndex();

        if (index >= 0)
        {
            attachment.setValueAsCompleteGesture (static_cast<float> (index));

            if (onUserChange != nullptr)
                onUserChange (index);
        }
    };

    addAndMakeVisible (box);
    attachment.sendInitialUpdate();
}

void ParamChoice::resized()
{
    label.setBounds (0, 0, getWidth(), row::labelHeight);
    box.setBounds (0, row::centreY - 13, getWidth(), 26);
}

// ---------------------------------------------------------------------------
void SwitchButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    // 縦型のスイッチ: 溝の中をキャップが上下する。上が ON で、ON のキャップは発光色になる
    const bool on = getToggleState();
    const float alpha = isEnabled() ? 1.0f : 0.4f;
    const auto slot = getLocalBounds().toFloat().reduced (1.0f);
    const float corner = 5.0f;

    g.setColour (colours::background.withMultipliedAlpha (alpha));
    g.fillRoundedRectangle (slot, corner);
    g.setColour (colours::outline.brighter (highlighted ? 0.45f : 0.2f).withMultipliedAlpha (alpha));
    g.drawRoundedRectangle (slot, corner, 1.2f);

    auto inner = slot.reduced (3.0f);
    const auto cap = on ? inner.removeFromTop (inner.getHeight() * 0.5f) : inner.removeFromBottom (inner.getHeight() * 0.5f);
    const auto colour = on ? onColour() : colours::panelLight.brighter (0.12f);

    if (on && ! muted)
    {
        // 発光のにじみ
        g.setColour (colour.withAlpha (0.22f * alpha));
        g.fillRoundedRectangle (cap.expanded (2.5f), corner);
    }

    g.setGradientFill (juce::ColourGradient (colour.brighter (down ? 0.45f : 0.28f).withMultipliedAlpha (alpha), 0.0f, cap.getY(),
                                             colour.darker (0.18f).withMultipliedAlpha (alpha), 0.0f, cap.getBottom(), false));
    g.fillRoundedRectangle (cap, corner - 2.0f);

    // キャップの印: ON は横線、OFF は小さな輪
    const auto centre = cap.getCentre();
    if (on)
    {
        g.setColour (juce::Colours::white.withAlpha ((muted ? 0.55f : 0.95f) * alpha));
        g.fillRoundedRectangle (juce::Rectangle<float> (cap.getWidth() * 0.5f, 2.4f).withCentre (centre), 1.2f);
    }
    else
    {
        g.setColour (colours::textDim.withAlpha (0.8f * alpha));
        g.drawEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre (centre), 1.3f);
    }
}

ParamToggle::ParamToggle (juce::AudioProcessorValueTreeState& apvts, const char* parameterId, const juce::String& text, int width)
    : buttonWidth (width)
{
    styleLabel (label, text);
    addAndMakeVisible (label);

    button.setTitle (text);
    button.onStateChange = [this] { repaint(); };
    addAndMakeVisible (button);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, parameterId, button);
}

void ParamToggle::resized()
{
    label.setBounds (0, 0, getWidth(), row::labelHeight);
    button.setBounds (juce::Rectangle<int> (28, 46).withCentre ({ getWidth() / 2, row::centreY }));
}

void ParamToggle::paint (juce::Graphics& g)
{
    const bool on = button.getToggleState();
    g.setColour ((on ? button.onColour().brighter (0.15f) : colours::textDim).withMultipliedAlpha (isEnabled() ? 1.0f : 0.4f));
    g.setFont (font (11.5f, true));
    g.drawText (on ? (button.isMuted() ? "BYPASS" : "ON") : "OFF", 0, row::valueTop, getWidth(), row::valueHeight, juce::Justification::centred);
}

// ---------------------------------------------------------------------------
LabeledControl::LabeledControl (const juce::String& labelText, juce::Component& childToShow, int heightOfChild)
    : child (childToShow), childHeight (heightOfChild)
{
    styleLabel (label, labelText);
    addAndMakeVisible (label);
    addAndMakeVisible (child);
}

void LabeledControl::resized()
{
    label.setBounds (0, 0, getWidth(), row::labelHeight);
    child.setBounds (0, row::centreY - childHeight / 2, getWidth(), childHeight);
}

// ---------------------------------------------------------------------------
EnvelopeDisplay::EnvelopeDisplay (juce::AudioProcessorValueTreeState& apvts, const char* attackId, const char* decayId,
                                  const char* sustainId, const char* releaseId)
    : parameters { apvts.getParameter (attackId), apvts.getParameter (decayId), apvts.getParameter (sustainId), apvts.getParameter (releaseId) }
{
    setInterceptsMouseClicks (false, false);
    refresh();
}

void EnvelopeDisplay::refresh()
{
    std::array<float, 4> now {};
    for (size_t i = 0; i < now.size(); ++i)
        now[i] = parameters[i] != nullptr ? parameters[i]->getValue() : 0.0f;

    if (now != shown)
    {
        shown = now;
        repaint();
    }
}

void EnvelopeDisplay::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour (colours::background);
    g.fillRoundedRectangle (bounds, 5.0f);

    const auto area = bounds.reduced (10.0f, 9.0f);
    const float width = area.getWidth();
    const float top = area.getY(), bottom = area.getBottom();

    // 時間の区間はノブの位置 (正規化値) に比例した幅にする。短い設定でも形が見えるよう最小幅を持たせる
    const auto segment = [width] (float normalised) { return width * (0.03f + 0.25f * juce::jlimit (0.0f, 1.0f, normalised)); };
    const float x0 = area.getX();
    const float xAttack = x0 + segment (shown[0]);
    const float xDecay = xAttack + segment (shown[1]);
    const float xHold = xDecay + width * 0.16f;
    const float xRelease = xHold + segment (shown[3]);
    const float ySustain = bottom - (bottom - top) * juce::jlimit (0.0f, 1.0f, shown[2]);

    // 区切りの補助線
    g.setColour (colours::outline.withAlpha (0.45f));
    for (const float x : { xAttack, xDecay, xHold })
        g.drawVerticalLine (juce::roundToInt (x), top, bottom);
    g.drawHorizontalLine (juce::roundToInt (bottom), area.getX(), area.getRight());

    juce::Path line;
    line.startNewSubPath (x0, bottom);
    line.lineTo (xAttack, top);
    line.quadraticTo (xAttack + (xDecay - xAttack) * 0.3f, ySustain, xDecay, ySustain);
    line.lineTo (xHold, ySustain);
    line.quadraticTo (xHold + (xRelease - xHold) * 0.3f, bottom, xRelease, bottom);

    juce::Path fill (line);
    fill.closeSubPath();
    g.setGradientFill (juce::ColourGradient (colours::accent.withAlpha (0.38f), 0.0f, top, colours::accent.withAlpha (0.04f), 0.0f, bottom, false));
    g.fillPath (fill);

    g.setGradientFill (juce::ColourGradient (colours::accentWarm, x0, 0.0f, colours::accentHot, area.getRight(), 0.0f, false));
    g.strokePath (line, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    g.setColour (colours::text);
    for (const auto point : { juce::Point<float> (xAttack, top), { xDecay, ySustain }, { xHold, ySustain }, { xRelease, bottom } })
        g.fillEllipse (point.x - 2.5f, point.y - 2.5f, 5.0f, 5.0f);

    g.setColour (colours::textDim);
    g.setFont (font (10.0f, true));
    const float labelY = bottom - 12.0f;
    const std::pair<const char*, std::pair<float, float>> labels[] = { { "A", { x0, xAttack } }, { "D", { xAttack, xDecay } },
                                                                        { "S", { xDecay, xHold } }, { "R", { xHold, xRelease } } };
    for (const auto& [text, span] : labels)
        if (span.second - span.first >= 12.0f)
            g.drawText (text, juce::Rectangle<float> (span.first, labelY, span.second - span.first, 11.0f), juce::Justification::centred);
}

// ---------------------------------------------------------------------------
ParamKnob& Section::knob (juce::AudioProcessorValueTreeState& apvts, const char* parameterId, const juce::String& labelText)
{
    auto control = std::make_unique<ParamKnob> (apvts, parameterId, labelText);
    auto& ref = *control;
    addCustom (ref, ref.preferredWidth(), ref.preferredHeight());
    owned.push_back (std::move (control));
    return ref;
}

ParamChoice& Section::choice (juce::AudioProcessorValueTreeState& apvts, const char* parameterId, const juce::String& labelText, int width)
{
    auto control = std::make_unique<ParamChoice> (apvts, parameterId, labelText, width);
    auto& ref = *control;
    addCustom (ref, ref.preferredWidth(), ref.preferredHeight());
    owned.push_back (std::move (control));
    return ref;
}

ParamToggle& Section::toggle (juce::AudioProcessorValueTreeState& apvts, const char* parameterId, const juce::String& text, int width)
{
    auto control = std::make_unique<ParamToggle> (apvts, parameterId, text, width);
    auto& ref = *control;
    addCustom (ref, ref.preferredWidth(), ref.preferredHeight());
    owned.push_back (std::move (control));
    return ref;
}

void Section::addCustom (juce::Component& component, int width, int height)
{
    items.push_back ({ &component, width, height });
    addAndMakeVisible (component);
}

template <typename Layout>
int Section::flow (int width, Layout&& place) const
{
    constexpr int gap = 8;
    const int left = padding;
    const int right = width - padding;
    int x = left, y = titleHeight + 4, rowHeight = 0;

    for (const auto& item : items)
    {
        if (! item.component->isVisible())
            continue;

        const int itemWidth = item.width > 0 ? item.width : right - left;

        if (x > left && x + itemWidth > right)
        {
            x = left;
            y += rowHeight + gap;
            rowHeight = 0;
        }

        place (item, x, y, itemWidth);
        x += itemWidth + gap;
        rowHeight = juce::jmax (rowHeight, juce::jmax (item.height, rowUnit));
    }

    return y + rowHeight + padding;
}

int Section::heightFor (int width) const
{
    return flow (width, [] (const Item&, int, int, int) {});
}

void Section::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (colours::panel);
    g.fillRoundedRectangle (bounds, 8.0f);
    g.setColour (colours::outline.withAlpha (0.6f));
    g.drawRoundedRectangle (bounds, 8.0f, 1.0f);

    g.setColour (colours::accent);
    g.fillRoundedRectangle (juce::Rectangle<float> (static_cast<float> (padding), 9.0f, 3.0f, 12.0f), 1.5f);
    g.setColour (colours::text);
    g.setFont (font (12.5f, true));
    g.drawText (title.toUpperCase(), padding + 9, 4, getWidth() - 2 * padding, 22, juce::Justification::centredLeft);
}

void Section::resized()
{
    flow (getWidth(), [] (const Item& item, int x, int y, int width)
    {
        // 背の低い部品 (ボタンなど) は、中心をノブの中心にそろえる
        item.component->setBounds (x, y + (item.height < rowUnit ? row::centreY - item.height / 2 : 0), width, item.height);
    });
}

// ===========================================================================
// キーボードフォーカス (D-076)
// ===========================================================================
namespace
{
const juce::Identifier focusSinkProperty { "polybriskFocusSink" };
const juce::Identifier focusReleaseProperty { "polybriskReleasesFocus" };
}

void markAsFocusSink (juce::Component& component)
{
    component.getProperties().set (focusSinkProperty, true);
    component.setWantsKeyboardFocus (true);
}

bool isFocusSink (const juce::Component& component)
{
    return component.getProperties().contains (focusSinkProperty);
}

bool mayHoldKeyboardFocus (const juce::Component& component)
{
    if (isFocusSink (component))
        return true;

    // 入力欄そのもの、またはその内側 (ノブの数値入力や、編集できる選択欄の入力欄も TextEditor)
    for (const auto* c = &component; c != nullptr; c = c->getParentComponent())
        if (dynamic_cast<const juce::TextEditor*> (c) != nullptr)
            return true;

    return false;
}

void releaseKeysAfterTextEntry (juce::Component& root)
{
    if (auto* editor = dynamic_cast<juce::TextEditor*> (&root))
    {
        if (! editor->getProperties().contains (focusReleaseProperty))
        {
            editor->getProperties().set (focusReleaseProperty, true);
            const juce::Component::SafePointer<juce::TextEditor> safe (editor);

            const auto thenRelease = [safe] (std::function<void()> existing)
            {
                return [safe, handler = std::move (existing)]
                {
                    if (handler != nullptr)
                        handler();

                    if (safe != nullptr)
                        safe->giveAwayKeyboardFocus();
                };
            };

            editor->onEscapeKey = thenRelease (editor->onEscapeKey);

            // 複数行の欄では Return は改行に使うことがあるので、1 行の欄だけ
            if (! editor->isMultiLine())
                editor->onReturnKey = thenRelease (editor->onReturnKey);
        }

        return;     // 入力欄の内側の部品には触れない
    }

    for (auto* child : root.getChildren())
        releaseKeysAfterTextEntry (*child);
}

// ===========================================================================
// OutputMeter
// ===========================================================================
void OutputMeter::push (float left, float right)
{
    const float in[2] = { left, right };
    bool changed = false;

    for (int ch = 0; ch < 2; ++ch)
    {
        // 上がるときはすぐ、下がるときは 1 周期に約 -1.6 dB
        float v = juce::jmax (in[ch], level[ch] * 0.83f);
        v = v < 1.0e-4f ? 0.0f : juce::jmin (v, 2.0f);
        changed = changed || std::abs (v - level[ch]) > 1.0e-4f;
        level[ch] = v;
    }

    if (changed)
        repaint();
}

void OutputMeter::paint (juce::Graphics& g)
{
    auto bars = getLocalBounds().withTrimmedLeft (barLeft).removeFromBottom (6).toFloat();

    for (int ch = 0; ch < 2; ++ch)
    {
        const auto bar = bars.withHeight (2.0f).translated (0.0f, static_cast<float> (ch) * 3.0f);
        g.setColour (colours::background);
        g.fillRect (bar);

        const float db = juce::Decibels::gainToDecibels (level[ch], -60.0f);
        const float fraction = juce::jlimit (0.0f, 1.0f, (db + 48.0f) / 48.0f);
        g.setColour (db > -2.0f ? colours::accentHot : (db > -9.0f ? colours::accentWarm : colours::cyan));
        g.fillRect (bar.withWidth (bar.getWidth() * fraction));
    }

    // 安全用リミッターのしきい値 (0.8 = 約 -1.9 dBFS) を超えた: 音が丸められている
    if (juce::jmax (level[0], level[1]) >= 0.8f)
    {
        g.setColour (colours::accentHot);
        g.setFont (font (9.0f, true));
        g.drawText ("LIMIT", getLocalBounds().withWidth (barLeft).removeFromBottom (10), juce::Justification::centredRight);
    }
}

// ===========================================================================
// PerformanceWheel
// ===========================================================================
PerformanceWheel::PerformanceWheel (Kind k, const juce::String& labelText) : kind (k), label (labelText)
{
    setWantsKeyboardFocus (false);
}

juce::Rectangle<float> PerformanceWheel::track() const
{
    return getLocalBounds().toFloat().withTrimmedBottom (static_cast<float> (labelHeight)).reduced (5.0f, 4.0f);
}

void PerformanceWheel::setValue (float newValue)
{
    const float low = kind == Kind::pitchBend ? -1.0f : 0.0f;
    newValue = juce::jlimit (low, 1.0f, newValue);

    if (! juce::approximatelyEqual (newValue, value))
    {
        value = newValue;
        repaint();

        if (onChange != nullptr)
            onChange (value);
    }
}

void PerformanceWheel::setDisplayValue (float newValue)
{
    if (isMouseButtonDown())
        return;

    newValue = juce::jlimit (kind == Kind::pitchBend ? -1.0f : 0.0f, 1.0f, newValue);

    if (! juce::approximatelyEqual (newValue, value))
    {
        value = newValue;
        repaint();
    }
}

void PerformanceWheel::paint (juce::Graphics& g)
{
    const auto area = track();
    const bool bend = kind == Kind::pitchBend;
    const auto colour = bend ? colours::accent : colours::cyan;

    // 溝
    g.setColour (colours::background.darker (0.4f));
    g.fillRoundedRectangle (area, 5.0f);
    g.setColour (colours::outline);
    g.drawRoundedRectangle (area.reduced (0.5f), 5.0f, 1.0f);

    // つまみの位置 (0..1、上が大きい)
    const float thumbHeight = 14.0f;
    const float travel = area.getHeight() - thumbHeight - 4.0f;
    const float position = bend ? 0.5f * (value + 1.0f) : value;
    const float thumbY = area.getBottom() - 2.0f - thumbHeight - position * travel;

    // 0 の位置からつまみまでを色で埋める
    const float zeroY = area.getBottom() - 2.0f - thumbHeight * 0.5f - (bend ? 0.5f : 0.0f) * travel;
    const float thumbCentre = thumbY + thumbHeight * 0.5f;
    const auto fill = juce::Rectangle<float> (area.getX() + 3.0f, juce::jmin (zeroY, thumbCentre),
                                              area.getWidth() - 6.0f, std::abs (zeroY - thumbCentre));
    g.setColour (colour.withAlpha (0.35f));
    g.fillRoundedRectangle (fill, 2.0f);

    if (bend)
    {
        g.setColour (colours::textDim.withAlpha (0.5f));
        g.fillRect (juce::Rectangle<float> (area.getX() + 2.0f, zeroY - 0.5f, area.getWidth() - 4.0f, 1.0f));
    }

    // つまみ: 横の溝を 3 本入れる
    const auto thumb = juce::Rectangle<float> (area.getX() + 2.0f, thumbY, area.getWidth() - 4.0f, thumbHeight);
    g.setColour (colours::panelLight.brighter (isMouseButtonDown() ? 0.35f : 0.15f));
    g.fillRoundedRectangle (thumb, 3.0f);
    g.setColour (colour);
    g.fillRect (juce::Rectangle<float> (thumb.getX() + 3.0f, thumb.getCentreY() - 1.0f, thumb.getWidth() - 6.0f, 2.0f));
    g.setColour (colours::background.withAlpha (0.6f));
    g.fillRect (juce::Rectangle<float> (thumb.getX() + 3.0f, thumb.getY() + 3.0f, thumb.getWidth() - 6.0f, 1.0f));
    g.fillRect (juce::Rectangle<float> (thumb.getX() + 3.0f, thumb.getBottom() - 4.0f, thumb.getWidth() - 6.0f, 1.0f));

    g.setColour (colours::textDim);
    g.setFont (font (9.5f, true));
    g.drawText (label, getLocalBounds().removeFromBottom (labelHeight), juce::Justification::centred);
}

void PerformanceWheel::mouseDown (const juce::MouseEvent&)
{
    valueAtDown = value;
    repaint();
}

void PerformanceWheel::mouseDrag (const juce::MouseEvent& e)
{
    // 相対ドラッグ。溝の端から端で、値の全範囲を動く
    const float travel = juce::jmax (1.0f, track().getHeight() - 18.0f);
    const float range = kind == Kind::pitchBend ? 2.0f : 1.0f;
    setValue (valueAtDown - static_cast<float> (e.getDistanceFromDragStartY()) / travel * range);
}

void PerformanceWheel::mouseUp (const juce::MouseEvent&)
{
    if (kind == Kind::pitchBend)
        setValue (0.0f);      // ばねで中央へ戻る

    repaint();
}

void PerformanceWheel::mouseDoubleClick (const juce::MouseEvent&)
{
    setValue (0.0f);
}

} // namespace polybrisk::ui
