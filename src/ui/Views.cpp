#include "ui/Views.h"

namespace polybrisk::ui
{

namespace ids = polybrisk::params::id;

namespace
{
void fillPatternBox (juce::ComboBox& box)
{
    // 項目 ID は「パターン番号 + 1」。グループごとに見出しを入れる
    int itemId = 1;
    juce::String group;

    for (const auto& factory : polybrisk::factoryGatePatterns)
    {
        if (group != factory.group)
        {
            group = factory.group;
            box.addSectionHeading (group);
        }

        box.addItem (factory.name, itemId++);
    }
}

/** パラメーターを実値で設定する (ホストに 1 回の操作として通知する)。 */
void setParameterValue (juce::AudioProcessorValueTreeState& apvts, const char* parameterId, float realValue)
{
    if (auto* parameter = apvts.getParameter (parameterId))
    {
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (realValue));
        parameter->endChangeGesture();
    }
}

/** 収録パターンを選ぶ。ステレオのパターンなら Gate の Stereo も ON にし (モノラルのパターンでは変えない)、
    Rate はそのパターンが前提とする値にする (Fast 1/32 のものは 1/32 か 1/64、それ以外は 1/16)。 */
void chooseFactoryPattern (PolyBriskAudioProcessor& processor, int factoryIndex)
{
    if (factoryIndex < 0 || factoryIndex >= static_cast<int> (polybrisk::factoryGatePatterns.size()))
        return;

    processor.getMetaState().setFactoryGatePattern (factoryIndex);

    auto& apvts = processor.getValueTreeState();

    if (const int rate = polybrisk::factoryGatePatterns[static_cast<size_t> (factoryIndex)].rateChoice; rate >= 0)
        setParameterValue (apvts, ids::gateRate, static_cast<float> (rate));

    if (polybrisk::factoryGatePatterns[static_cast<size_t> (factoryIndex)].right16 != nullptr && apvts.getRawParameterValue (ids::gateStereo)->load() < 0.5f)
        setParameterValue (apvts, ids::gateStereo, 1.0f);
}

void dim (juce::Component& component, bool active)
{
    component.setEnabled (active);
    component.setAlpha (active ? 1.0f : 0.4f);
}

/** 現在のパターン名をコンボボックスに表示する (ユーザーが編集していれば * を付ける)。 */
juce::String patternLabel (polybrisk::MetaState& meta)
{
    return meta.getGatePatternName() + (meta.isGatePatternEdited() ? " *" : "");
}

float rawValue (juce::AudioProcessorValueTreeState& apvts, const char* parameterId)
{
    return apvts.getRawParameterValue (parameterId)->load();
}
} // namespace

// ===========================================================================
// Page
// ===========================================================================
Section& Page::add (const juce::String& title, float weight, bool newRow)
{
    cells.push_back ({ std::make_unique<Section> (title), weight, newRow });
    addAndMakeVisible (*cells.back().section);
    return *cells.back().section;
}

int Page::layout (int width, bool apply)
{
    constexpr int gap = 8;
    int y = gap;
    size_t i = 0;

    while (i < cells.size())
    {
        // 1 行分 (次の newRow の手前まで) を集める
        size_t end = i + 1;
        while (end < cells.size() && ! cells[end].newRow)
            ++end;

        float totalWeight = 0.0f;
        for (size_t c = i; c < end; ++c)
            totalWeight += cells[c].weight;

        const int available = width - gap * static_cast<int> (end - i + 1);
        int rowHeight = 0;
        std::vector<int> widths;

        for (size_t c = i; c < end; ++c)
        {
            widths.push_back (static_cast<int> (static_cast<float> (available) * cells[c].weight / totalWeight));
            rowHeight = juce::jmax (rowHeight, cells[c].section->heightFor (widths.back()));
        }

        if (apply)
        {
            int x = gap;
            for (size_t c = i; c < end; ++c)
            {
                cells[c].section->setBounds (x, y, widths[c - i], rowHeight);
                x += widths[c - i] + gap;
            }
        }

        y += rowHeight + gap;
        i = end;
    }

    return y;
}

void Page::resized()
{
    layout (getWidth(), true);
}

// ===========================================================================
// GateGrid
// ===========================================================================
int GateGrid::stepCount() const
{
    return polybrisk::params::gateStepCount (juce::roundToInt (rawValue (processor.getValueTreeState(), ids::gateSteps)));
}

int GateGrid::stepAt (int x) const
{
    const int steps = stepCount();
    return juce::jlimit (0, steps - 1, x * steps / juce::jmax (1, getWidth()));
}

bool GateGrid::isStereo() const
{
    return rawValue (processor.getValueTreeState(), ids::gateStereo) >= 0.5f;
}

juce::Rectangle<float> GateGrid::laneBounds (int lane, bool stereo) const
{
    auto bounds = getLocalBounds().toFloat().reduced (0.0f, 2.0f);

    if (! stereo)
        return bounds;

    const float height = (bounds.getHeight() - 3.0f) * 0.5f;
    return lane == 0 ? bounds.removeFromTop (height) : bounds.removeFromBottom (height);
}

void GateGrid::refresh()
{
    const auto pattern = processor.getMetaState().getGatePattern();
    const int steps = stepCount();
    const int playing = processor.getGateStep();
    const bool stereo = isStereo();

    if (! (pattern == shown) || steps != shownSteps || playing != shownPlaying || stereo != shownStereo)
    {
        shown = pattern;
        shownSteps = steps;
        shownPlaying = playing;
        shownStereo = stereo;
        repaint();
    }
}

void GateGrid::paint (juce::Graphics& g)
{
    const int steps = shownSteps;
    const float cell = static_cast<float> (getWidth()) / static_cast<float> (steps);

    g.setColour (colours::background);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 5.0f);

    // Stereo のときは上段が左、下段が右
    for (int lane = 0; lane < (shownStereo ? 2 : 1); ++lane)
    {
        const auto area = laneBounds (lane, shownStereo);
        const auto& values = lane == 0 ? shown.values : shown.right;
        const auto colour = ! shownStereo ? colours::accent : (lane == 0 ? colours::accent : colours::cyan.darker (0.15f));

        for (int i = 0; i < steps; ++i)
        {
            const float value = values[static_cast<size_t> (i)];
            const auto slot = juce::Rectangle<float> (static_cast<float> (i) * cell + 1.5f, area.getY(), cell - 3.0f, area.getHeight());

            // 4 ステップごとに背景を変えて拍を見やすくする
            g.setColour ((i / 4) % 2 == 0 ? colours::panelLight.withAlpha (0.55f) : colours::panel);
            g.fillRoundedRectangle (slot, 3.0f);

            if (value > 0.0f)
            {
                const auto bar = slot.withTop (slot.getBottom() - slot.getHeight() * value);
                g.setColour (i == shownPlaying ? colours::accentWarm : colour.withAlpha (0.55f + 0.45f * value));
                g.fillRoundedRectangle (bar, 3.0f);
            }

            if (i == shownPlaying)
            {
                g.setColour (colours::accentWarm);
                g.drawRoundedRectangle (slot, 3.0f, 1.5f);
            }
        }

        if (shownStereo)
        {
            g.setColour (juce::Colours::white.withAlpha (0.85f));
            g.setFont (font (10.5f, true));
            g.drawText (lane == 0 ? "L" : "R", juce::Rectangle<float> (5.0f, area.getY() + 2.0f, 12.0f, 12.0f), juce::Justification::centred);
        }
    }
}

void GateGrid::setFromMouse (const juce::MouseEvent& e, bool toggle)
{
    const int step = stepAt (e.x);
    auto& meta = processor.getMetaState();
    const bool stereo = isStereo();

    if (toggle)
    {
        // 押した位置の段を、ドラッグが終わるまで編集する。Stereo が OFF なら左右とも同じ値を書く
        dragLane = ! stereo ? -1 : (static_cast<float> (e.y) < static_cast<float> (getHeight()) * 0.5f ? 0 : 1);

        // クリック: 開いていれば閉じ、閉じていれば全開にする。そのままドラッグすれば高さを調整できる
        const auto pattern = meta.getGatePattern();
        const float current = dragLane == 1 ? pattern.right[static_cast<size_t> (step)] : pattern.values[static_cast<size_t> (step)];
        dragValue = current > 0.0f ? 0.0f : 1.0f;
    }
    else
    {
        const auto area = laneBounds (juce::jmax (0, dragLane), dragLane >= 0);
        const float fromHeight = 1.0f - (static_cast<float> (e.y) - area.getY()) / juce::jmax (1.0f, area.getHeight());
        dragValue = juce::jlimit (0.0f, 1.0f, std::round (fromHeight * 20.0f) / 20.0f);
    }

    meta.setGateStep (step, dragValue, dragLane);
    refresh();
}

void GateGrid::mouseDown (const juce::MouseEvent& e)
{
    dragged = false;
    setFromMouse (e, true);
}

void GateGrid::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragged && e.getDistanceFromDragStart() < 4)
        return;

    dragged = true;
    setFromMouse (e, false);
}

// ===========================================================================
// SimpleView
// ===========================================================================
SimpleView::SimpleView (PolyBriskAudioProcessor& p)
    : processor (p),
      apvts (p.getValueTreeState()),
      fxBypass (apvts, ids::fxBypass, "FX Bypass", 84),
      gateOn (apvts, ids::gateEnabled, "Gate", 64),
      arpOn (apvts, ids::arpEnabled, "Arp", 64),
      gateDepth (apvts, ids::gateDepth, "Depth"),
      gatePatternBox ("Pattern", gatePattern),
      arpMode (apvts, ids::arpMode, "Mode", 130),
      arpRate (apvts, ids::arpRate, "Rate", 90),
      toneCutoff (apvts, ids::filterCutoff, "Cutoff"),
      toneResonance (apvts, ids::filterResonance, "Resonance"),
      toneAttack (apvts, ids::ampAttack, "Attack"),
      toneRelease (apvts, ids::ampRelease, "Release")
{
    for (size_t i = 0; i < macros.size(); ++i)
    {
        macros[i] = std::make_unique<ParamKnob> (apvts, ids::macros[i], "Macro", macroKnobSize);
        addAndMakeVisible (*macros[i]);
    }

    const std::pair<const char*, const char*> fx[] = { { ids::driveEnabled, "Drive" }, { ids::phaserEnabled, "Phaser" }, { ids::chorusEnabled, "Chorus" }, { ids::eqEnabled, "EQ" },
                                                       { ids::imagerEnabled, "Imager" }, { ids::delayEnabled, "Delay" }, { ids::reverbEnabled, "Reverb" } };
    for (const auto& [parameterId, name] : fx)
    {
        fxToggles.push_back (std::make_unique<ParamToggle> (apvts, parameterId, name, 74));
        addAndMakeVisible (*fxToggles.back());
    }

    fxBypass.button.setColour (juce::TextButton::buttonOnColourId, colours::accentHot);
    gateOn.button.setColour (juce::TextButton::buttonOnColourId, colours::cyan.darker (0.3f));
    arpOn.button.setColour (juce::TextButton::buttonOnColourId, colours::violet);

    for (auto* c : std::initializer_list<juce::Component*> { &fxBypass, &gateOn, &arpOn, &gateDepth, &gatePatternBox, &arpMode, &arpRate,
                                                               &toneCutoff, &toneResonance, &toneAttack, &toneRelease, &info, &performance })
        addAndMakeVisible (*c);

    fillPatternBox (gatePattern);
    gatePattern.onChange = [this]
    {
        if (gatePattern.getSelectedId() > 0)
            chooseFactoryPattern (processor, gatePattern.getSelectedId() - 1);
    };

    info.setFont (font (13.0f));
    info.setColour (juce::Label::textColourId, colours::textDim);
    info.setJustificationType (juce::Justification::topLeft);
    info.setMinimumHorizontalScale (0.8f);
    performance.setFont (font (12.5f));
    performance.setColour (juce::Label::textColourId, colours::textDim);
    performance.setJustificationType (juce::Justification::topRight);

    refresh();
}

void SimpleView::refresh()
{
    auto& meta = processor.getMetaState();

    // マクロのラベルはプリセットごとに変わる
    for (int i = 0; i < 4; ++i)
        macros[static_cast<size_t> (i)]->setLabelText (meta.getMacro (i).name);

    const auto label = patternLabel (meta);
    if (label != shownPattern)
    {
        shownPattern = label;
        gatePattern.setText (label, juce::dontSendNotification);
    }

    // OFF のあいだも位置は変えず、薄く表示する (D-039)
    const bool gate = rawValue (apvts, ids::gateEnabled) >= 0.5f;
    const bool arp = rawValue (apvts, ids::arpEnabled) >= 0.5f;

    if (gate != shownGate || arp != shownArp || ! rhythmShown)
    {
        shownGate = gate;
        shownArp = arp;
        rhythmShown = true;
        dim (gatePatternBox, gate);
        dim (gateDepth, gate);
        dim (arpMode, arp);
        dim (arpRate, arp);
    }

    // 一括バイパス中は、個々の FX のスイッチの色を落とす (ON のままだが効いていない)
    const bool bypass = rawValue (apvts, ids::fxBypass) >= 0.5f;
    if (bypass != shownBypass)
    {
        shownBypass = bypass;
        for (auto& t : fxToggles)
            t->setMuted (bypass);
    }

    const auto preset = meta.getPresetInfo();
    juce::String text = preset.category;
    if (preset.tags.size() > 0)
        text << (text.isNotEmpty() ? "  |  " : "") << preset.tags.joinIntoString (" / ");
    if (preset.description.isNotEmpty())
        text << (text.isNotEmpty() ? "\n" : "") << preset.description;

    if (text != shownInfo)
    {
        shownInfo = text;
        info.setText (text, juce::dontSendNotification);
    }

    const bool independent = juce::roundToInt (rawValue (apvts, ids::bendMode)) == 1;
    const int up = juce::roundToInt (rawValue (apvts, ids::bendUp));
    const int down = independent ? juce::roundToInt (rawValue (apvts, ids::bendDown)) : up;
    performance.setText ("Bend +" + juce::String (up) + " / -" + juce::String (down) + " st    Vibrato "
                             + juce::String (juce::roundToInt (rawValue (apvts, ids::vibratoDepth))) + " cent (Mod Wheel)",
                         juce::dontSendNotification);
}

SimpleView::Areas SimpleView::areas() const
{
    constexpr int gap = 10;
    constexpr int titleHeight = 32;
    Areas a;
    auto bounds = getLocalBounds().reduced (12);

    // 上段: 左にマクロ、右によく直す 4 つのノブ (TONE)
    auto top = bounds.removeFromTop (titleHeight + macroKnobSize + 38 + 12);
    a.tone = top.removeFromRight (4 * toneCutoff.preferredWidth() + 3 * 12 + 32);
    top.removeFromRight (gap);
    a.macros = top;
    bounds.removeFromTop (gap);
    a.fx = bounds.removeFromTop (titleHeight + row::height + 8);
    bounds.removeFromTop (gap);

    auto rhythm = bounds.removeFromTop (titleHeight + row::height + 8);
    a.gate = rhythm.removeFromLeft ((rhythm.getWidth() - gap) / 2);
    rhythm.removeFromLeft (gap);
    a.arp = rhythm;

    bounds.removeFromTop (gap + 2);
    a.footer = bounds.reduced (8, 0);
    return a;
}

void SimpleView::paint (juce::Graphics& g)
{
    const auto panel = [&g] (juce::Rectangle<int> area, const juce::String& title)
    {
        g.setColour (colours::panel);
        g.fillRoundedRectangle (area.toFloat(), 10.0f);
        g.setColour (colours::outline.withAlpha (0.6f));
        g.drawRoundedRectangle (area.toFloat().reduced (0.5f), 10.0f, 1.0f);
        g.setColour (colours::accent);
        g.fillRoundedRectangle (juce::Rectangle<float> (static_cast<float> (area.getX() + 12), static_cast<float> (area.getY() + 11), 3.0f, 13.0f), 1.5f);
        g.setColour (colours::text);
        g.setFont (font (13.0f, true));
        g.drawText (title, area.getX() + 22, area.getY() + 6, 300, 24, juce::Justification::centredLeft);
    };

    const auto a = areas();
    panel (a.macros, "MACROS");
    panel (a.tone, "TONE");
    panel (a.fx, "FX");
    panel (a.gate, "TRANCE GATE");
    panel (a.arp, "ARPEGGIATOR");
}

void SimpleView::resized()
{
    constexpr int titleHeight = 32;
    constexpr int gap = 12;
    const auto a = areas();

    // --- マクロ: 4 つを等間隔に置く ---
    auto macroArea = a.macros.reduced (16, 0).withTrimmedTop (titleHeight);
    const int macroWidth = macroArea.getWidth() / 4;
    for (auto& m : macros)
        m->setBounds (macroArea.removeFromLeft (macroWidth).removeFromTop (m->preferredHeight()).withSizeKeepingCentre (m->preferredWidth() + 40, m->preferredHeight()));

    // --- TONE: 4 つを左から詰め、マクロのノブと中心の高さをそろえる ---
    auto toneArea = a.tone.reduced (16, 0).withTrimmedTop (titleHeight);
    toneArea = toneArea.withHeight (row::height).translated (0, (macroKnobSize + 38 - row::height) / 2);
    for (auto* k : { &toneCutoff, &toneResonance, &toneAttack, &toneRelease })
    {
        k->setBounds (toneArea.removeFromLeft (k->preferredWidth()));
        toneArea.removeFromLeft (gap);
    }

    // --- FX サマリー: 左から詰め、一括バイパスだけ右端 ---
    auto fxArea = a.fx.reduced (16, 0).withTrimmedTop (titleHeight).withHeight (row::height);
    fxBypass.setBounds (fxArea.removeFromRight (fxBypass.preferredWidth()));
    for (auto& t : fxToggles)
    {
        t->setBounds (fxArea.removeFromLeft (t->preferredWidth()));
        fxArea.removeFromLeft (gap);
    }

    // --- RHYTHM: Gate と Arp を左右のパネルに分け、それぞれ左から詰める ---
    const auto place = [] (juce::Rectangle<int> area, std::initializer_list<std::pair<juce::Component*, int>> items)
    {
        area = area.reduced (16, 0).withTrimmedTop (titleHeight).withHeight (row::height);

        for (const auto& [component, width] : items)
        {
            component->setBounds (area.removeFromLeft (width));
            area.removeFromLeft (gap);
        }
    };

    place (a.gate, { { &gateOn, gateOn.preferredWidth() }, { &gatePatternBox, 190 }, { &gateDepth, gateDepth.preferredWidth() } });
    place (a.arp, { { &arpOn, arpOn.preferredWidth() }, { &arpMode, arpMode.preferredWidth() }, { &arpRate, arpRate.preferredWidth() } });

    auto footer = a.footer;
    performance.setBounds (footer.removeFromRight (360));
    info.setBounds (footer);
}

// ===========================================================================
// MacroEditor
// ===========================================================================
MacroEditor::MacroEditor (PolyBriskAudioProcessor& p) : processor (p)
{
    // タブを開いたときのフォーカスは自分で受ける (受けないと、最初の名前欄が編集状態になってしまう)。キーは処理しない
    markAsFocusSink (*this);

    // 割当先にできるのは連続値パラメーターだけ (SPEC §10.1)
    for (const auto& d : polybrisk::params::all())
        if (d.macroAssignable)
        {
            targetIds.add (d.id);
            targetNames.add (d.name);
        }

    for (int m = 0; m < 4; ++m)
    {
        auto& column = columns[static_cast<size_t> (m)];
        column.value = std::make_unique<ParamKnob> (processor.getValueTreeState(), ids::macros[m], "Value", 54);

        column.name.setFont (font (15.0f, true));
        column.name.setJustification (juce::Justification::centred);
        column.name.setInputRestrictions (32);
        const auto commitName = [this, m]
        {
            auto& editor = columns[static_cast<size_t> (m)].name;
            if (! rebuilding && editor.getText() != processor.getMetaState().getMacro (m).name)
                processor.getMetaState().setMacroName (m, editor.getText().trim().isEmpty() ? polybrisk::MetaState::defaultMacroName (m) : editor.getText());
        };
        column.name.onReturnKey = commitName;
        column.name.onFocusLost = commitName;

        column.learn.onClick = [this, m]
        {
            if (processor.getMidiLearnTarget() == m)
                processor.cancelMidiLearn();
            else
                processor.beginMidiLearn (m);
            refresh();
        };
        column.clearCc.onClick = [this, m] { processor.getMetaState().setMacroCc (m, -1); };

        column.add.onClick = [this, m]
        {
            // まだ割り当てていない最初のパラメーターを、オフセット 0 で追加する (音は変わらない)
            const auto config = processor.getMetaState().getMacro (m);

            for (const auto& id : targetIds)
            {
                bool used = false;
                for (const auto& mapping : config.mappings)
                    used = used || mapping.target == id;

                if (! used)
                {
                    processor.getMetaState().setMapping (m, { id, 0.0f, 0.0f, polybrisk::macro::Curve::linear });
                    break;
                }
            }
        };

        column.cc.setFont (font (12.0f));
        column.cc.setColour (juce::Label::textColourId, colours::textDim);
        column.cc.setJustificationType (juce::Justification::centred);

        for (auto* c : std::initializer_list<juce::Component*> { &column.name, column.value.get(), &column.learn, &column.clearCc, &column.add, &column.cc })
            addAndMakeVisible (*c);
    }

    processor.getMetaState().addChangeListener (this);
    rebuild();
}

MacroEditor::~MacroEditor()
{
    // 画面を閉じたら MIDI Learn の待ち受けをやめる (閉じたあとに届いた CC が、見えないところで割り当たらないように)
    processor.cancelMidiLearn();
    processor.getMetaState().removeChangeListener (this);
}

void MacroEditor::commitRow (int macroIndex, Row& row)
{
    if (rebuilding)
        return;

    auto& meta = processor.getMetaState();
    const int targetIndex = row.target.getSelectedItemIndex();

    if (targetIndex < 0)
        return;

    const auto newTarget = targetIds[targetIndex];

    // スライダーは 1 % 刻みで表示している。動かしていない側は、表示用に丸めた値ではなく元の値を保つ
    // (プリセットには -0.012 のような 1 % より細かい値がある。Curve だけを変えたときに丸めてしまわない)
    float low = static_cast<float> (row.low.getValue() * 0.01);
    float high = static_cast<float> (row.high.getValue() * 0.01);

    for (const auto& existing : meta.getMacro (macroIndex).mappings)
        if (existing.target == row.currentTarget)
        {
            if (juce::approximatelyEqual (static_cast<float> (row.low.getValue()), std::round (existing.deltaMin * 100.0f)))
                low = existing.deltaMin;

            if (juce::approximatelyEqual (static_cast<float> (row.high.getValue()), std::round (existing.deltaMax * 100.0f)))
                high = existing.deltaMax;
        }

    // 対象を変えた場合は、古い割当を外してから付け直す (同じ対象が既にあれば、そちらを置き換える)
    if (newTarget != row.currentTarget)
        meta.removeMapping (macroIndex, row.currentTarget);

    meta.setMapping (macroIndex, { newTarget, low, high,
                                   static_cast<polybrisk::macro::Curve> (juce::jmax (0, row.curve.getSelectedItemIndex())) });
}

void MacroEditor::rebuild()
{
    rebuilding = true;
    auto& meta = processor.getMetaState();

    for (int m = 0; m < 4; ++m)
    {
        auto& column = columns[static_cast<size_t> (m)];
        const auto config = meta.getMacro (m);

        if (! column.name.hasKeyboardFocus (true))
            column.name.setText (config.name, juce::dontSendNotification);

        column.cc.setText (config.cc >= 0 ? "CC " + juce::String (config.cc) : "No CC assigned", juce::dontSendNotification);
        column.clearCc.setEnabled (config.cc >= 0);
        column.add.setEnabled (static_cast<int> (config.mappings.size()) < polybrisk::macro::maxMappingsPerMacro);

        // ドラッグ中のスライダーを作り直さないよう、行数が同じなら値だけを更新する
        if (column.rows.size() != config.mappings.size())
        {
            column.rows.clear();

            for (size_t r = 0; r < config.mappings.size(); ++r)
            {
                auto row = std::make_unique<Row>();
                auto* raw = row.get();

                for (int i = 0; i < targetNames.size(); ++i)
                    row->target.addItem (targetNames[i], i + 1);

                row->curve.addItem ("Linear", 1);
                row->curve.addItem ("Ease In", 2);
                row->curve.addItem ("Ease Out", 3);

                for (auto* s : { &row->low, &row->high })
                {
                    s->setRange (-100.0, 100.0, 1.0);
                    s->setTextBoxStyle (juce::Slider::TextBoxRight, false, 44, 18);
                    s->setTextValueSuffix ("%");
                    s->setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
                    s->setDoubleClickReturnValue (true, 0.0);
                    s->onValueChange = [this, m, raw] { commitRow (m, *raw); };
                }

                row->target.onChange = [this, m, raw] { commitRow (m, *raw); };
                row->curve.onChange = [this, m, raw] { commitRow (m, *raw); };
                row->remove.onClick = [this, m, raw]
                {
                    const auto target = raw->currentTarget;
                    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MacroEditor> (this), m, target]
                    {
                        if (safe != nullptr)
                            safe->processor.getMetaState().removeMapping (m, target);   // 行自身を消すので、コールバックの外で行う
                    });
                };

                for (auto* c : std::initializer_list<juce::Component*> { &row->target, &row->curve, &row->low, &row->high, &row->remove })
                    addAndMakeVisible (*c);

                column.rows.push_back (std::move (row));
            }
        }

        for (size_t r = 0; r < config.mappings.size(); ++r)
        {
            auto& row = *column.rows[r];
            const auto& mapping = config.mappings[r];
            row.currentTarget = mapping.target;
            row.target.setSelectedItemIndex (targetIds.indexOf (mapping.target), juce::dontSendNotification);
            row.curve.setSelectedItemIndex (static_cast<int> (mapping.curve), juce::dontSendNotification);

            if (! row.low.isMouseButtonDown())
                row.low.setValue (std::round (mapping.deltaMin * 100.0f), juce::dontSendNotification);
            if (! row.high.isMouseButtonDown())
                row.high.setValue (std::round (mapping.deltaMax * 100.0f), juce::dontSendNotification);
        }
    }

    rebuilding = false;
    releaseKeysAfterTextEntry (*this);      // 名前の入力欄は、入力を終えたらキーをホストへ返す (D-076)
    resized();
    repaint();
}

void MacroEditor::refresh()
{
    const int learning = processor.getMidiLearnTarget();

    for (int m = 0; m < 4; ++m)
    {
        auto& learn = columns[static_cast<size_t> (m)].learn;
        const bool active = learning == m;

        if (learn.getToggleState() != active)
        {
            learn.setToggleState (active, juce::dontSendNotification);
            learn.setButtonText (active ? "Move a CC..." : "MIDI Learn");
        }
    }
}

void MacroEditor::paint (juce::Graphics& g)
{
    const int width = (getWidth() - 8 * 5) / 4;

    for (int m = 0; m < 4; ++m)
    {
        const auto area = juce::Rectangle<int> (8 + m * (width + 8), 8, width, getHeight() - 16).toFloat();
        g.setColour (colours::panel);
        g.fillRoundedRectangle (area, 8.0f);
        g.setColour (colours::outline.withAlpha (0.6f));
        g.drawRoundedRectangle (area.reduced (0.5f), 8.0f, 1.0f);

        g.setColour (colours::textDim);
        g.setFont (font (10.5f));
        const int headerY = 174;
        g.drawText ("TARGET  /  LOW offset  /  HIGH offset  (+-% of range)", static_cast<int> (area.getX()) + 10, headerY, width - 20, 14, juce::Justification::centredLeft);

        if (columns[static_cast<size_t> (m)].rows.empty())
        {
            g.setFont (font (12.0f));
            g.drawFittedText ("No targets yet.\nAdd one, then set how far the macro moves it below (LOW) and above (HIGH) its centre.",
                              static_cast<int> (area.getX()) + 14, headerY + 24, width - 28, 70, juce::Justification::centredTop, 4);
        }
    }
}

void MacroEditor::resized()
{
    const int width = (getWidth() - 8 * 5) / 4;

    for (int m = 0; m < 4; ++m)
    {
        auto& column = columns[static_cast<size_t> (m)];
        auto area = juce::Rectangle<int> (8 + m * (width + 8), 8, width, getHeight() - 16).reduced (10, 8);

        column.name.setBounds (area.removeFromTop (28));
        area.removeFromTop (4);
        auto top = area.removeFromTop (92);
        column.value->setBounds (top.removeFromLeft (76));
        top.removeFromLeft (8);
        column.cc.setBounds (top.removeFromTop (22));
        column.learn.setBounds (top.removeFromTop (28).reduced (0, 1));
        top.removeFromTop (4);
        column.clearCc.setBounds (top.removeFromTop (26).reduced (0, 1));

        area.removeFromTop (6);
        column.add.setBounds (area.removeFromTop (26));
        area.removeFromTop (26);    // 見出し

        for (auto& row : column.rows)
        {
            auto line = area.removeFromTop (22);
            row->remove.setBounds (line.removeFromRight (22));
            line.removeFromRight (4);
            row->curve.setBounds (line.removeFromRight (76));
            line.removeFromRight (4);
            row->target.setBounds (line);

            auto sliders = area.removeFromTop (22);
            row->low.setBounds (sliders.removeFromLeft (sliders.getWidth() / 2).reduced (2, 0));
            row->high.setBounds (sliders.reduced (2, 0));
            area.removeFromTop (5);
        }
    }
}

// ===========================================================================
// AdvancedView
// ===========================================================================
void AdvancedView::buildOsc (Page& page, const polybrisk::params::OscIds& o, const juce::String& title, OscControls& controls)
{
    auto& s = page.add (title, 1.0f, true);
    controls.section = &s;
    // 幅は、波形が Pulse のとき (Pulse Width のノブが出る) でも 1 行に収まるように決めてある
    s.toggle (apvts, o.enabled, "Power", 46);
    controls.wave = &s.choice (apvts, o.wave, "Wave", 94);
    controls.octave = &s.knob (apvts, o.octave, "Octave");
    controls.semitone = &s.knob (apvts, o.semitone, "Semitone");
    controls.fine = &s.knob (apvts, o.fine, "Fine");
    s.knob (apvts, o.level, "Level");
    s.knob (apvts, o.unisonVoices, "Unison");
    s.knob (apvts, o.detune, "Detune");
    s.knob (apvts, o.unisonMix, "Uni Mix");
    s.knob (apvts, o.spread, "Spread");
    s.choice (apvts, o.phaseMode, "Phase", 92);
    controls.pulseWidth = &s.knob (apvts, o.pulseWidth, "Pulse Width");

    // SuperSAW を選んだとき、ユニゾンが 1 本のままなら既定の 7 本にする (SPEC §4.2、D-016)
    controls.wave->onUserChange = [this, unisonId = o.unisonVoices] (int index)
    {
        if (index == static_cast<int> (polybrisk::Waveform::superSaw) && juce::roundToInt (rawValue (apvts, unisonId)) <= 1)
            setParameterValue (apvts, unisonId, 7.0f);
    };
}

AdvancedView::AdvancedView (PolyBriskAudioProcessor& p)
    : processor (p), apvts (p.getValueTreeState()), macroEditor (p), gateGrid (p),
      filterEnvDisplay (apvts, ids::filterEnvAttack, ids::filterEnvDecay, ids::filterEnvSustain, ids::filterEnvRelease),
      ampEnvDisplay (apvts, ids::ampAttack, ids::ampDecay, ids::ampSustain, ids::ampRelease)
{
    // --- OSC ---
    buildOsc (oscPage, ids::oscA, "OSC A", oscA);
    buildOsc (oscPage, ids::oscB, "OSC B", oscB);
    {
        auto& s = oscPage.add ("Transient", 0.56f, true);
        s.toggle (apvts, ids::transientEnabled, "Power", 56);
        s.choice (apvts, ids::transientMode, "Mode", 124);
        s.knob (apvts, ids::transientAmount, "Amount");
        s.knob (apvts, ids::transientDuration, "Duration");
        s.knob (apvts, ids::transientNoiseColor, "Noise Color");
        s.knob (apvts, ids::transientPitchSnap, "Pitch Snap");

        auto& extra = oscPage.add ("Sub / Noise", 0.44f, false);
        extra.knob (apvts, ids::subLevel, "Sub");
        extra.choice (apvts, ids::subOctave, "Octave", 76);
        extra.choice (apvts, ids::subWave, "Wave", 74);
        extra.knob (apvts, ids::noiseLevel, "Noise");
        extra.knob (apvts, ids::noiseColor, "Color");
    }

    // --- Filter & EG ---
    {
        auto& s = filterPage.add ("Filter", 1.0f, true);
        s.choice (apvts, ids::filterCharacter, "Character", 110);
        s.choice (apvts, ids::filterMode, "Mode", 90);
        s.knob (apvts, ids::filterCutoff, "Cutoff");
        s.knob (apvts, ids::filterResonance, "Resonance");
        s.knob (apvts, ids::filterDrive, "Drive");
        s.knob (apvts, ids::filterEnvAmount, "Env Amount");
        s.knob (apvts, ids::filterKeyTrack, "Key Track");
        s.knob (apvts, ids::filterVelocity, "Velocity");

        auto& fe = filterPage.add ("Filter Envelope", 0.45f, true);
        fe.knob (apvts, ids::filterEnvAttack, "Attack");
        fe.knob (apvts, ids::filterEnvDecay, "Decay");
        fe.knob (apvts, ids::filterEnvSustain, "Sustain");
        fe.knob (apvts, ids::filterEnvRelease, "Release");
        fe.addCustom (filterEnvDisplay, 0, envelopeDisplayHeight);

        auto& ae = filterPage.add ("Amp Envelope", 0.55f, false);
        ae.knob (apvts, ids::ampAttack, "Attack");
        ae.knob (apvts, ids::ampDecay, "Decay");
        ae.knob (apvts, ids::ampSustain, "Sustain");
        ae.knob (apvts, ids::ampRelease, "Release");
        ae.knob (apvts, ids::ampVelocity, "Velocity");
        ae.addCustom (ampEnvDisplay, 0, envelopeDisplayHeight);

        auto& lfo = filterPage.add ("LFO", 1.0f, true);
        lfoSection = &lfo;
        lfo.choice (apvts, ids::lfoWave, "Wave", 100);
        lfo.toggle (apvts, ids::lfoSync, "Sync", 56);
        lfoRate = &lfo.knob (apvts, ids::lfoRate, "Rate");
        lfoDivision = &lfo.choice (apvts, ids::lfoDivision, "Rate", 90);
        lfo.knob (apvts, ids::lfoFade, "Fade In");
        lfo.toggle (apvts, ids::lfoRetrigger, "Key Sync", 64);
        lfo.knob (apvts, ids::lfoCutoff, "> Cutoff");
        lfo.knob (apvts, ids::lfoPulseWidth, "> Pulse W");
        lfo.knob (apvts, ids::lfoPitch, "> Pitch");
        lfo.knob (apvts, ids::lfoAmp, "> Amp");
    }

    // --- Performance ---
    {
        auto& bend = performancePage.add ("Pitch Bend", 0.45f, true);
        bend.choice (apvts, ids::bendMode, "Mode", 130);
        bend.knob (apvts, ids::bendUp, "Up");
        bend.knob (apvts, ids::bendDown, "Down");

        auto& vibrato = performancePage.add ("Vibrato (Mod Wheel)", 0.55f, false);
        vibrato.knob (apvts, ids::vibratoDepth, "Depth");
        vibrato.knob (apvts, ids::vibratoRate, "Rate");
        vibrato.knob (apvts, ids::vibratoDelay, "Delay");

        auto& voice = performancePage.add ("Voice", 0.30f, true);
        voice.choice (apvts, ids::voiceMode, "Mode", 110);
        voice.knob (apvts, ids::voiceGlide, "Glide");

        auto& pressure = performancePage.add ("Aftertouch", 0.35f, false);
        pressure.knob (apvts, ids::aftertouchVibrato, "> Vibrato");
        pressure.knob (apvts, ids::aftertouchCutoff, "> Cutoff");

        // Preset Level は音色ごとの音量の補正 (プリセットに保存)。全体の音量は上部の OUT (D-055)
        auto& out = performancePage.add ("Preset", 0.35f, false);
        out.knob (apvts, ids::presetLevel, "Level");
    }

    // --- FX ---
    {
        const auto power = [this] (Section& section, const char* parameterId)
        {
            fxPowerToggles.push_back (&section.toggle (apvts, parameterId, "Power", 52));
        };

        // 全体のバイパスは、いちばん先に目に入る左上に置く (途中の段にあると見落とす。D-074)。
        // 各セクションの幅の比は、中身が 1 行に収まることを画像で確かめた値。詰めると折り返してページがスクロールする
        auto& all = fxPage.add ("All FX", 0.16f, true);
        all.toggle (apvts, ids::fxBypass, "FX Bypass", 90).button.setColour (juce::TextButton::buttonOnColourId, colours::accentHot);

        auto& delay = fxPage.add ("Delay", 0.84f, false);
        power (delay, ids::delayEnabled);
        delay.choice (apvts, ids::delayMode, "Mode", 104);
        delay.toggle (apvts, ids::delaySync, "Sync", 56);
        delay.choice (apvts, ids::delayDivision, "Division", 86);
        delay.knob (apvts, ids::delayTime, "Time");
        delay.knob (apvts, ids::delayFeedback, "Feedback");
        delay.knob (apvts, ids::delayTone, "Tone");
        delay.knob (apvts, ids::delayMix, "Mix");
        delay.knob (apvts, ids::delayDucking, "Ducking");

        auto& drive = fxPage.add ("Drive", 0.70f, true);
        power (drive, ids::driveEnabled);
        drive.choice (apvts, ids::driveType, "Type", 140);
        drive.knob (apvts, ids::driveDrive, "Drive");
        drive.knob (apvts, ids::driveTone, "Tone");
        drive.knob (apvts, ids::driveMix, "Mix");
        drive.knob (apvts, ids::driveOutput, "Output");
        drive.toggle (apvts, ids::driveAutoGain, "Auto Gain", 86);

        auto& imager = fxPage.add ("Stereo Imager", 0.30f, false);
        power (imager, ids::imagerEnabled);
        imager.knob (apvts, ids::imagerWidth, "Width");
        imager.toggle (apvts, ids::imagerMonoBass, "Mono Bass", 90);

        auto& phaser = fxPage.add ("Phaser / Flanger", 0.50f, true);
        power (phaser, ids::phaserEnabled);
        phaser.choice (apvts, ids::phaserType, "Type", 78);
        phaser.knob (apvts, ids::phaserRate, "Rate");
        phaser.knob (apvts, ids::phaserDepth, "Depth");
        phaser.knob (apvts, ids::phaserFeedback, "Feedback");
        phaser.knob (apvts, ids::phaserMix, "Mix");

        auto& chorus = fxPage.add ("Chorus", 0.50f, false);
        power (chorus, ids::chorusEnabled);
        chorus.choice (apvts, ids::chorusType, "Type", 78);
        chorus.knob (apvts, ids::chorusRate, "Rate");
        chorus.knob (apvts, ids::chorusDepth, "Depth");
        chorus.knob (apvts, ids::chorusWidth, "Width");
        chorus.knob (apvts, ids::chorusMix, "Mix");

        auto& reverb = fxPage.add ("Reverb", 0.58f, true);
        power (reverb, ids::reverbEnabled);
        reverb.choice (apvts, ids::reverbType, "Type", 78);
        reverb.knob (apvts, ids::reverbSize, "Size");
        reverb.knob (apvts, ids::reverbDecay, "Decay");
        reverb.knob (apvts, ids::reverbPreDelay, "Pre-Delay");
        reverb.knob (apvts, ids::reverbTone, "Tone");
        reverb.knob (apvts, ids::reverbMix, "Mix");

        auto& eq = fxPage.add ("EQ", 0.42f, false);
        power (eq, ids::eqEnabled);
        eq.knob (apvts, ids::eqLowGain, "Low 100 Hz");
        eq.knob (apvts, ids::eqMidFreq, "Mid Freq");
        eq.knob (apvts, ids::eqMidGain, "Mid Gain");
        eq.knob (apvts, ids::eqHighGain, "High 8 kHz");
    }

    // --- Rhythm ---
    {
        auto& gate = rhythmPage.add ("Trance Gate", 1.0f, true);
        gate.toggle (apvts, ids::gateEnabled, "Power", 56);
        gate.choice (apvts, ids::gateSteps, "Steps", 62);
        gate.choice (apvts, ids::gateRate, "Rate", 78);
        gate.knob (apvts, ids::gateDepth, "Depth");
        gate.knob (apvts, ids::gateSmooth, "Smooth");
        gate.knob (apvts, ids::gateSwing, "Swing");
        gate.choice (apvts, ids::gateRestart, "Restart", 100);
        gate.toggle (apvts, ids::gateStereo, "Stereo", 56).button.setColour (juce::TextButton::buttonOnColourId, colours::cyan.darker (0.3f));
        gateWidth = &gate.knob (apvts, ids::gateWidth, "Width");

        fillPatternBox (gatePattern);
        gatePattern.onChange = [this]
        {
            if (gatePattern.getSelectedId() <= 0)
                return;

            // 選び直すとユーザーの編集が失われるので、直前のパターンを 1 つだけ覚えておく (Undo)
            auto& meta = processor.getMetaState();
            undoPattern = meta.getGatePattern();
            undoName = meta.getGatePatternName();
            undoEdited = meta.isGatePatternEdited();
            undoRate = rawValue (apvts, ids::gateRate);     // パターンを選ぶと Rate と Stereo も変わるので、一緒に戻せるようにする
            undoStereo = rawValue (apvts, ids::gateStereo);
            undoPresetId = presetKey();
            hasUndo = true;
            chooseFactoryPattern (processor, gatePattern.getSelectedId() - 1);
        };
        gateUndo.onClick = [this]
        {
            // 覚えたあとに別の音色へ切り替えていたら戻さない (前の音色のパターンを、いまの音色に書き込んでしまう)
            if (hasUndo && undoPresetId == presetKey())
            {
                processor.getMetaState().setGatePattern (undoPattern, undoName, undoEdited);

                if (undoRate >= 0.0f)
                    setParameterValue (apvts, ids::gateRate, undoRate);

                if (undoStereo >= 0.0f)
                    setParameterValue (apvts, ids::gateStereo, undoStereo);
            }

            hasUndo = false;
        };

        gate.addCustom (gatePatternBox, 164, row::height);
        gateRandom.onClick = [this]
        {
            // いまのパターンは Undo で戻せる
            auto& meta = processor.getMetaState();
            undoPattern = meta.getGatePattern();
            undoName = meta.getGatePatternName();
            undoEdited = meta.isGatePatternEdited();
            undoRate = -1.0f;   // Random は Rate も Stereo も変えない
            undoStereo = -1.0f;
            undoPresetId = presetKey();
            hasUndo = true;

            const bool stereo = rawValue (apvts, ids::gateStereo) >= 0.5f;
            meta.setGatePattern (polybrisk::makeRandomGatePattern (static_cast<std::uint32_t> (random.nextInt()), stereo), "Random", false);
        };

        gate.addCustom (gateButtons, 70, row::height);
        gate.addCustom (gateGrid, 0, 132);

        auto& arp = rhythmPage.add ("Arpeggiator", 1.0f, true);
        arp.toggle (apvts, ids::arpEnabled, "Power", 56);
        arp.choice (apvts, ids::arpMode, "Mode", 120);
        arp.choice (apvts, ids::arpRate, "Rate", 84);
        arp.knob (apvts, ids::arpOctaves, "Octaves");
        arp.knob (apvts, ids::arpGate, "Gate Length");
        arp.knob (apvts, ids::arpSwing, "Swing");
        arp.knob (apvts, ids::arpVelocity, "Velocity");
        arp.toggle (apvts, ids::arpLatch, "Latch", 70);
        arp.choice (apvts, ids::arpRestart, "Restart", 110);
    }

    Page* pages[] = { &oscPage, &filterPage, &performancePage, &fxPage, &rhythmPage };
    const char* names[] = { "OSC", "Filter & Mod", "Performance", "FX", "Rhythm" };

    for (int i = 0; i < 5; ++i)
    {
        viewports[i].setViewedComponent (pages[i], false);
        viewports[i].setScrollBarsShown (true, false);
        tabs.addTab (names[i], juce::Colours::transparentBlack, &viewports[i], false);
    }

    tabs.addTab ("Macros", juce::Colours::transparentBlack, &macroEditor, false);
    tabs.setTabBarDepth (36);
    tabs.setOutline (0);
    addAndMakeVisible (tabs);

    refresh();
}

void AdvancedView::refresh()
{
    // ページの大きさを表示領域に合わせる (高さが足りなければスクロール)
    Page* pages[] = { &oscPage, &filterPage, &performancePage, &fxPage, &rhythmPage };
    for (int i = 0; i < 5; ++i)
    {
        const int width = viewports[i].getMaximumVisibleWidth();
        if (width > 0)
        {
            const int height = pages[i]->layout (width, false);
            if (pages[i]->getWidth() != width || pages[i]->getHeight() != height)
                pages[i]->setSize (width, height);
        }
    }

    // 波形に関係のないパラメーターは隠す (SPEC §9.2): Pulse Width は Pulse のときだけ、Noise ではチューニングを隠す
    for (auto* osc : { &oscA, &oscB })
    {
        const int wave = osc->wave->box.getSelectedItemIndex();

        if (wave != osc->shownWave)
        {
            osc->shownWave = wave;
            const bool noise = wave == static_cast<int> (polybrisk::Waveform::noise);
            osc->pulseWidth->setVisible (wave == static_cast<int> (polybrisk::Waveform::pulse));
            osc->octave->setVisible (! noise);
            osc->semitone->setVisible (! noise);
            osc->fine->setVisible (! noise);
            osc->section->resized();
        }
    }

    const auto label = patternLabel (processor.getMetaState());
    if (label != shownPattern)
    {
        shownPattern = label;
        gatePattern.setText (label, juce::dontSendNotification);
    }

    const bool stereo = rawValue (apvts, ids::gateStereo) >= 0.5f;
    if (stereo != shownStereo || ! stereoShown)
    {
        shownStereo = stereo;
        stereoShown = true;
        dim (*gateWidth, stereo);
    }

    // LFO: Sync が ON なら音価、OFF なら Hz のどちらか一方だけを見せる
    const int lfoSync = rawValue (apvts, ids::lfoSync) >= 0.5f ? 1 : 0;
    if (lfoSync != shownLfoSync)
    {
        shownLfoSync = lfoSync;
        lfoRate->setVisible (lfoSync == 0);
        lfoDivision->setVisible (lfoSync == 1);
        lfoSection->resized();
    }

    // 一括バイパス中は、個々の FX のスイッチの色を落とす
    const bool bypass = rawValue (apvts, ids::fxBypass) >= 0.5f;
    if (bypass != shownBypass)
    {
        shownBypass = bypass;
        for (auto* toggle : fxPowerToggles)
            toggle->setMuted (bypass);
    }

    if (hasUndo && undoPresetId != presetKey())
        hasUndo = false;    // 音色を切り替えたら、覚えていたパターンは捨てる

    gateUndo.setEnabled (hasUndo);
    gateGrid.refresh();
    filterEnvDisplay.refresh();
    ampEnvDisplay.refresh();
    macroEditor.refresh();
}

// ===========================================================================
// PresetBrowser
// ===========================================================================
PresetBrowser::PresetBrowser (PolyBriskAudioProcessor& p) : processor (p), manager (p.getPresetManager())
{
    search.setTextToShowWhenEmpty ("Search name, tag, description...", colours::textDim);
    search.setFont (font (14.0f));
    search.onTextChange = [this] { updateList(); };

    category.onChange = [this] { updateList(); };
    favouritesOnly.setClickingTogglesState (true);
    favouritesOnly.onClick = [this] { updateList(); };
    close.onClick = [this] { if (onClose != nullptr) onClose(); };

    clearTags.setTooltip ("Clear the tag filter");
    clearTags.onClick = [this]
    {
        selectedTags.clear();
        for (auto* button : tagButtons)
            button->setToggleState (false, juce::dontSendNotification);
        updateList();
    };
    addAndMakeVisible (clearTags);

    list.setRowHeight (30);
    list.setWantsKeyboardFocus (false);     // 矢印キーで「選択だけ」が動くと、読み込んでいる音色と表示がずれる。ホストのキー操作も奪わない
    list.setOutlineThickness (1);

    saveAs.onClick = [this] { showSavePanel (true); };

    initButton.setTooltip ("Start from an empty sound: every setting back to its default, no macro assignments");
    initButton.onClick = [this]
    {
        const auto result = manager.loadInit();
        setMessage (result.wasOk() ? "Init: all settings are back to their defaults." : result.getErrorMessage(), result.failed());
    };

    // Delete は取り消せないので 2 回押しにする: 1 回目で対象の名前を表示し、同じ音色のままもう一度押すと消す
    remove.onClick = [this]
    {
        const int index = currentPreset();
        if (index < 0)
            return;

        const auto info = manager.getEntry (index).info;

        if (deleteArmedId != info.id)
        {
            // 確認待ち: ボタン自体を目立つ色と文言に変える (メッセージ欄だけでは、もう一度押すことに気づきにくい)
            deleteArmedId = info.id;
            remove.setButtonText ("Click again to delete");
            remove.setColour (juce::TextButton::buttonColourId, colours::accentHot);
            remove.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
            setMessage ("Delete \"" + info.name + "\"? This cannot be undone.", true);
            return;
        }

        disarmDelete();
        const auto result = manager.deleteUser (index);
        setMessage (result.wasOk() ? "Deleted." : result.getErrorMessage(), result.failed());
        updateList();
    };

    lock.setClickingTogglesState (true);
    lock.setTooltip ("Keep Pitch Bend range and Vibrato settings when changing presets");
    lock.onClick = [this] { manager.setPerformanceLock (lock.getToggleState()); };

    importButton.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Import preset", juce::File::getSpecialLocation (juce::File::userHomeDirectory), "*.json");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [safe = juce::Component::SafePointer<PresetBrowser> (this)] (const juce::FileChooser& fc)
        {
            if (safe == nullptr || fc.getResult() == juce::File())
                return;

            const auto result = safe->manager.importFile (fc.getResult());
            safe->setMessage (result.wasOk() ? "Imported." : result.getErrorMessage(), result.failed());
            safe->updateList();
        });
    };

    exportButton.onClick = [this]
    {
        const auto name = juce::File::createLegalFileName (processor.getMetaState().getPresetInfo().name) + ".json";
        chooser = std::make_unique<juce::FileChooser> ("Export preset", juce::File::getSpecialLocation (juce::File::userDesktopDirectory).getChildFile (name), "*.json");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                              [safe = juce::Component::SafePointer<PresetBrowser> (this)] (const juce::FileChooser& fc)
        {
            if (safe == nullptr || fc.getResult() == juce::File())
                return;

            const auto result = safe->manager.exportCurrent (fc.getResult().withFileExtension ("json"));
            safe->setMessage (result.wasOk() ? "Exported." : result.getErrorMessage(), result.failed());
        });
    };

    message.setFont (font (12.5f));
    message.setJustificationType (juce::Justification::centredLeft);
    detail.setFont (font (12.5f));
    detail.setColour (juce::Label::textColourId, colours::textDim);
    detail.setJustificationType (juce::Justification::topLeft);

    for (auto* c : std::initializer_list<juce::Component*> { &search, &category, &favouritesOnly, &close, &list, &saveAs, &remove, &importButton,
                                                             &exportButton, &lock, &initButton, &message, &detail })
        addAndMakeVisible (*c);

    // --- Save As パネル ---
    const char* labels[] = { "Name", "Category", "Tags (comma separated)", "Description" };
    for (int i = 0; i < 4; ++i)
    {
        saveLabels[i].setText (labels[i], juce::dontSendNotification);
        saveLabels[i].setFont (font (12.0f));
        saveLabels[i].setColour (juce::Label::textColourId, colours::textDim);
        savePanel.addAndMakeVisible (saveLabels[i]);
    }

    saveName.setInputRestrictions (64);
    saveTags.setInputRestrictions (160);
    saveDescription.setInputRestrictions (512);
    saveDescription.setMultiLine (true);
    saveCategory.setEditableText (true);
    for (int i = 0; i < polybrisk::PresetManager::standardCategories().size(); ++i)
        saveCategory.addItem (polybrisk::PresetManager::standardCategories()[i], i + 1);

    saveConfirm.onClick = [this]
    {
        juce::StringArray tags;
        tags.addTokens (saveTags.getText(), ",", "");
        tags.trim();
        tags.removeEmptyStrings();

        const auto result = manager.saveUser (saveName.getText(), saveCategory.getText(), tags, saveDescription.getText());
        setMessage (result.wasOk() ? "Saved to User presets." : result.getErrorMessage(), result.failed());

        if (result.wasOk())
            showSavePanel (false);

        updateList();
    };
    saveCancel.onClick = [this] { showSavePanel (false); };

    for (auto* c : std::initializer_list<juce::Component*> { &saveName, &saveCategory, &saveTags, &saveDescription, &saveConfirm, &saveCancel })
        savePanel.addAndMakeVisible (*c);

    addChildComponent (savePanel);

    manager.addChangeListener (this);
    processor.getMetaState().addChangeListener (this);
    updateList();
}

PresetBrowser::~PresetBrowser()
{
    processor.getMetaState().removeChangeListener (this);
    manager.removeChangeListener (this);
}

int PresetBrowser::currentPreset() const
{
    return manager.getCurrentIndex();
}

void PresetBrowser::disarmDelete()
{
    if (deleteArmedId.isNotEmpty())
    {
        deleteArmedId.clear();
        remove.setButtonText ("Delete");
        remove.removeColour (juce::TextButton::buttonColourId);
        remove.removeColour (juce::TextButton::textColourOffId);
    }
}

void PresetBrowser::openSaveAs()
{
    showSavePanel (true);
}

void PresetBrowser::visibilityChanged()
{
    if (! isVisible())
    {
        disarmDelete();
        return;
    }

    // 開くたびに読み直す: ほかのインスタンスで保存・削除した音色や、お気に入りの変更を取り込む
    manager.refresh();
    updateList();
}

void PresetBrowser::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // 隠れているあいだは作り直さない (マクロや Gate の編集のたびに全音色を絞り込み直すのは無駄)。開くときに更新する
    if (isVisible())
        updateList();
}

void PresetBrowser::setMessage (const juce::String& text, bool isError)
{
    message.setColour (juce::Label::textColourId, isError ? colours::accentHot : colours::cyan);
    message.setText (text, juce::dontSendNotification);
}

void PresetBrowser::showSavePanel (bool show)
{
    if (show)
    {
        const auto info = processor.getMetaState().getPresetInfo();
        saveName.setText (info.factory ? info.name + " (User)" : info.name, juce::dontSendNotification);
        saveCategory.setText (info.category, juce::dontSendNotification);
        saveTags.setText (info.tags.joinIntoString (", "), juce::dontSendNotification);
        saveDescription.setText (info.description, juce::dontSendNotification);
    }

    savePanel.setVisible (show);
    list.setVisible (! show);
    resized();

    if (show)
        saveName.grabKeyboardFocus();
}

void PresetBrowser::rebuildTagButtons()
{
    // なくなったタグは選択からも外す
    for (int i = selectedTags.size(); --i >= 0;)
        if (! knownTags.contains (selectedTags[i], true))
            selectedTags.remove (i);

    tagButtons.clear();

    for (const auto& tag : knownTags)
    {
        auto* button = tagButtons.add (new juce::TextButton (tag));
        button->setClickingTogglesState (true);
        button->setToggleState (selectedTags.contains (tag, true), juce::dontSendNotification);
        button->onClick = [this, button, tag]
        {
            if (button->getToggleState())
                selectedTags.addIfNotAlreadyThere (tag, true);
            else
                selectedTags.removeString (tag, true);

            updateList();
        };
        addAndMakeVisible (button);
    }

    resized();
}

int PresetBrowser::layoutTagButtons (juce::Rectangle<int> area, bool apply)
{
    constexpr int rowHeight = 24, gap = 5;
    int x = area.getX(), y = area.getY();

    const auto place = [&] (juce::TextButton& button, int width)
    {
        if (x > area.getX() && x + width > area.getRight())
        {
            x = area.getX();
            y += rowHeight + gap;
        }

        if (apply)
            button.setBounds (x, y, width, rowHeight);

        x += width + gap;
    };

    for (auto* button : tagButtons)
        place (*button, 20 + juce::GlyphArrangement::getStringWidthInt (font (13.0f, true), button->getButtonText()));

    place (clearTags, 56);
    return y + rowHeight - area.getY();
}

void PresetBrowser::updateList()
{
    // カテゴリの選択肢 (先頭は All)
    const auto current = category.getSelectedId() > 1 ? category.getText() : juce::String();
    category.clear (juce::dontSendNotification);
    category.addItem ("All categories", 1);
    int itemId = 2;
    for (const auto& name : manager.getCategories())
        category.addItem (name, itemId++);

    int selected = 1;
    for (int i = 0; i < category.getNumItems(); ++i)
        if (category.getItemText (i) == current)
            selected = category.getItemId (i);
    category.setSelectedId (selected, juce::dontSendNotification);

    // タグの一覧が変わったら (User の保存・削除・Import)、ボタンを作り直す
    if (const auto tags = manager.getTags(); tags != knownTags)
    {
        knownTags = tags;
        rebuildTagButtons();
    }

    clearTags.setEnabled (selectedTags.size() > 0);

    visible = manager.filter (search.getText(), selected > 1 ? category.getText() : juce::String(), favouritesOnly.getToggleState(), selectedTags);
    list.updateContent();

    const int index = manager.getCurrentIndex();
    int row = -1;
    for (size_t i = 0; i < visible.size(); ++i)
        if (visible[i] == index)
            row = static_cast<int> (i);

    if (row >= 0)
        list.selectRow (row, true, true);
    else
        list.deselectAllRows();

    const bool isUser = index >= 0 && ! manager.getEntry (index).info.factory;
    remove.setEnabled (isUser);

    // 確認待ちの Delete は、対象の音色が変わったら取り消す
    if (deleteArmedId.isNotEmpty() && (! isUser || manager.getEntry (index).info.id != deleteArmedId))
        disarmDelete();
    lock.setToggleState (manager.getPerformanceLock(), juce::dontSendNotification);

    const auto info = processor.getMetaState().getPresetInfo();
    juce::String text;
    text << info.name << (processor.getMetaState().isDirty() ? " *" : "") << "\n";
    if (info.category.isNotEmpty())
        text << info.category << (info.factory ? "  (Factory)" : "  (User)") << "\n";
    if (info.tags.size() > 0)
        text << info.tags.joinIntoString (" / ") << "\n";
    text << "\n" << info.description;
    detail.setText (text, juce::dontSendNotification);

    list.repaint();
}

void PresetBrowser::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (row < 0 || row >= static_cast<int> (visible.size()) || visible[static_cast<size_t> (row)] >= manager.getNumPresets())
        return;

    const auto& info = manager.getEntry (visible[static_cast<size_t> (row)]).info;

    if (selected)
    {
        g.setColour (colours::accent.withAlpha (0.28f));
        g.fillRect (0, 0, width, height);
    }

    // お気に入りの星 (ここをクリックすると、音色を切り替えずに付け外しできる)
    juce::Path star;
    star.addStar ({ static_cast<float> (starColumnWidth) * 0.5f, static_cast<float> (height) * 0.5f }, 5, 3.6f, 8.2f);

    if (manager.isFavourite (info.id))
    {
        g.setColour (colours::accentWarm);
        g.fillPath (star);
    }
    else
    {
        g.setColour (colours::outline.brighter (0.35f));
        g.strokePath (star, juce::PathStrokeType (1.2f));
    }

    g.setColour (colours::text);
    g.setFont (font (14.0f, selected));
    g.drawText (info.name, starColumnWidth + 4, 0, width / 2 - starColumnWidth - 6, height, juce::Justification::centredLeft);

    g.setColour (colours::textDim);
    g.setFont (font (12.0f));
    g.drawText (info.category + (info.factory ? "" : "  (User)"), width / 2, 0, width / 4, height, juce::Justification::centredLeft);
    g.drawText (info.tags.joinIntoString (" / "), width * 3 / 4, 0, width / 4 - 8, height, juce::Justification::centredLeft);
}

void PresetBrowser::listBoxItemClicked (int row, const juce::MouseEvent& e)
{
    // visible は一覧の添字を覚えている。保存・削除の直後で一覧が縮んでいたら、古い添字は使わない
    if (row < 0 || row >= static_cast<int> (visible.size()) || visible[static_cast<size_t> (row)] >= manager.getNumPresets())
        return;

    // 星の列: お気に入りの付け外しだけを行う (音色は読み込まない)
    if (e.x < starColumnWidth)
    {
        const auto id = manager.getEntry (visible[static_cast<size_t> (row)]).info.id;
        manager.setFavourite (id, ! manager.isFavourite (id));
        return;
    }

    const auto result = manager.load (visible[static_cast<size_t> (row)]);

    if (result.failed())
        setMessage (result.getErrorMessage(), true);
    else
        setMessage ({}, false);
}

void PresetBrowser::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);
    g.setColour (colours::text);
    g.setFont (font (16.0f, true));
    g.drawText ("PRESETS", 16, 10, 200, 26, juce::Justification::centredLeft);

    g.setColour (colours::textDim);
    g.setFont (font (12.0f));
    g.drawText (juce::String (manager.getNumFactoryPresets()) + " factory / " + juce::String (manager.getNumPresets() - manager.getNumFactoryPresets())
                    + " user", 110, 10, 300, 26, juce::Justification::centredLeft);
}

void PresetBrowser::resized()
{
    auto bounds = getLocalBounds().reduced (16, 10);
    auto top = bounds.removeFromTop (28);
    close.setBounds (top.removeFromRight (80));
    bounds.removeFromTop (10);

    auto side = bounds.removeFromRight (290);
    bounds.removeFromRight (14);

    auto filters = bounds.removeFromTop (28);
    favouritesOnly.setBounds (filters.removeFromRight (96));
    filters.removeFromRight (8);
    category.setBounds (filters.removeFromRight (190));
    filters.removeFromRight (8);
    search.setBounds (filters);
    bounds.removeFromTop (8);

    // タグのボタン (幅に合わせて折り返す)
    const int tagHeight = layoutTagButtons (bounds, false);
    layoutTagButtons (bounds.removeFromTop (tagHeight), true);
    bounds.removeFromTop (8);

    list.setBounds (bounds);
    savePanel.setBounds (bounds);

    // --- 右側: 現在の音色と操作 ---
    detail.setBounds (side.removeFromTop (150));
    const auto button = [&side] (juce::Component& c)
    {
        c.setBounds (side.removeFromTop (30));
        side.removeFromTop (8);
    };
    button (initButton);
    button (saveAs);
    button (remove);
    auto io = side.removeFromTop (30);
    importButton.setBounds (io.removeFromLeft (io.getWidth() / 2).withTrimmedRight (4));
    exportButton.setBounds (io.withTrimmedLeft (4));
    side.removeFromTop (16);
    button (lock);
    message.setBounds (side.removeFromTop (44));

    // --- Save As パネルの中身 ---
    auto panel = savePanel.getLocalBounds().reduced (4);
    const auto field = [&panel] (juce::Label& label, juce::Component& editor, int height)
    {
        label.setBounds (panel.removeFromTop (18));
        editor.setBounds (panel.removeFromTop (height));
        panel.removeFromTop (10);
    };
    field (saveLabels[0], saveName, 28);
    field (saveLabels[1], saveCategory, 28);
    field (saveLabels[2], saveTags, 28);
    field (saveLabels[3], saveDescription, 84);
    auto actions = panel.removeFromTop (30);
    saveConfirm.setBounds (actions.removeFromLeft (120));
    actions.removeFromLeft (10);
    saveCancel.setBounds (actions.removeFromLeft (120));
}

} // namespace polybrisk::ui
