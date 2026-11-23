#include "PluginEditor.h"

using namespace polybrisk::ui;

PolyBriskAudioProcessorEditor::PolyBriskAudioProcessorEditor (PolyBriskAudioProcessor& p)
    : AudioProcessorEditor (&p),
      polyBrisk (p),
      showKeyboard (p.wrapperType == juce::AudioProcessor::wrapperType_Standalone),
      simpleView (p),
      advancedView (p),
      browser (p),
      keyboard (p.getKeyboardState(), juce::MidiKeyboardComponent::horizontalKeyboard)
{
    setLookAndFeel (&lookAndFeel);
    addAndMakeVisible (content);

    for (auto* c : std::initializer_list<juce::Component*> { &previous, &next, &presetName, &save, &simpleTab, &advancedTab, &outputGain,
                                                             &outputLabel, &simpleView, &advancedView })
        content.addAndMakeVisible (*c);

    // 出力音量 (ヘッダー右端)
    outputGain.setTextBoxStyle (juce::Slider::TextBoxRight, false, 62, 20);
    outputGain.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    outputGain.setNumDecimalPlacesToDisplay (1);
    outputAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.getValueTreeState(), polybrisk::params::id::outputGain, outputGain);
    outputGain.textFromValueFunction = [] (double v) { return juce::String (std::abs (v) < 0.05 ? 0.0 : v, 1) + " dB"; };   // "-0.0" と出さない
    outputGain.setDoubleClickReturnValue (true, 0.0);
    outputGain.updateText();
    outputLabel.setText ("OUT", juce::dontSendNotification);
    outputLabel.setFont (font (11.0f, true));
    outputLabel.setColour (juce::Label::textColourId, colours::textDim);
    outputLabel.setJustificationType (juce::Justification::centredRight);

    content.addAndMakeVisible (outputMeter);
    content.addChildComponent (browser);
    content.addChildComponent (keyboard);
    keyboard.setWantsKeyboardFocus (false);   // 画面内鍵盤は、パソコンのキーボードでは弾かせない (ホストのキー操作を奪わない)
    keyboard.setVisible (showKeyboard);       // Standalone のときだけ画面内鍵盤を出す (SPEC §13)
    keyboard.setAvailableRange (lowestKey, highestKey);
    keyboard.setKeyWidth (static_cast<float> (baseWidth - wheelAreaWidth) / static_cast<float> (whiteKeys));   // 右端まで鍵盤で埋める

    // 鍵盤の左にピッチベンドとモジュレーションのホイール (D-067)
    content.addChildComponent (bendWheel);
    content.addChildComponent (modWheel);
    bendWheel.setVisible (showKeyboard);
    modWheel.setVisible (showKeyboard);
    bendWheel.onChange = [this] (float v) { polyBrisk.setUiPitchBend (v); };
    modWheel.onChange = [this] (float v) { polyBrisk.setUiModWheel (v); };

    presetName.setColour (juce::TextButton::buttonColourId, colours::panel);
    presetName.setColour (juce::TextButton::textColourOffId, colours::text);
    presetName.onClick = [this] { showBrowser (! browser.isVisible()); };

    previous.onClick = [this] { polyBrisk.getPresetManager().loadAdjacent (-1); };
    next.onClick = [this] { polyBrisk.getPresetManager().loadAdjacent (1); };

    // Save: User プリセットは、読み込んだファイルへそのまま上書きする。Factory / 未保存の音色、または上書きに
    // 失敗したとき (フォルダーが書き込み不可など) は、ブラウザーの Save As を開く
    save.onClick = [this]
    {
        if (polyBrisk.getPresetManager().saveCurrent().failed())
        {
            showBrowser (true);
            browser.openSaveAs();
        }
    };

    simpleTab.setRadioGroupId (1);
    advancedTab.setRadioGroupId (1);
    simpleTab.setClickingTogglesState (true);
    advancedTab.setClickingTogglesState (true);
    simpleTab.setConnectedEdges (juce::Button::ConnectedOnRight);
    advancedTab.setConnectedEdges (juce::Button::ConnectedOnLeft);
    // ブラウザーを開いている間に押されたら、ブラウザーを閉じて選んだ画面を見せる (すでに選ばれている側を押した場合も)
    simpleTab.onClick = [this] { if (simpleTab.getToggleState()) { setAdvanced (false); showBrowser (false); } };
    advancedTab.onClick = [this] { if (advancedTab.getToggleState()) { setAdvanced (true); showBrowser (false); } };

    browser.onClose = [this] { showBrowser (false); };

    shownAdvanced = polyBrisk.getMetaState().isAdvancedView();
    simpleTab.setToggleState (! shownAdvanced, juce::dontSendNotification);
    advancedTab.setToggleState (shownAdvanced, juce::dontSendNotification);
    simpleView.setVisible (! shownAdvanced);
    advancedView.setVisible (shownAdvanced);

    // 縦横比を保って 70%〜200% に拡大縮小できる (SPEC §13: 画面スケーリング)
    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio (static_cast<double> (baseWidth) / static_cast<double> (baseHeight()));
    setResizeLimits (baseWidth * 7 / 10, baseHeight() * 7 / 10, baseWidth * 2, baseHeight() * 2);
    setSize (baseWidth, baseHeight());

    // ホストのキー操作を横取りしない (D-076): キーを受け取るのは文字の入力欄だけ。ほかの部品がフォーカスを
    // 受け取ったら、キーを処理しない content へすぐに移す (globalFocusChanged)。入力欄は、入力を終えたら手放す
    markAsFocusSink (content);
    releaseKeysAfterTextEntry (*this);
    juce::Desktop::getInstance().addFocusChangeListener (this);

    timerCallback();
    startTimerHz (30);      // メーターがなめらかに見える周期
}

PolyBriskAudioProcessorEditor::~PolyBriskAudioProcessorEditor()
{
    juce::Desktop::getInstance().removeFocusChangeListener (this);
    stopTimer();
    setLookAndFeel (nullptr);
}

juce::Component* PolyBriskAudioProcessorEditor::focusTargetFor (juce::Component* focused)
{
    // 自分の画面の外 (ほかのウインドウ、選択欄のメニュー) には関与しない
    if (focused == nullptr || ! isParentOf (focused))
        return nullptr;

    return mayHoldKeyboardFocus (*focused) ? nullptr : &content;
}

void PolyBriskAudioProcessorEditor::globalFocusChanged (juce::Component* focused)
{
    // ボタンや選択欄がフォーカスを受け取った: キーを処理しない content へ移す。以後のキーはホストへ渡る
    if (auto* target = focusTargetFor (focused))
        target->grabKeyboardFocus();
}

void PolyBriskAudioProcessorEditor::setAdvanced (bool advanced)
{
    // 表示を切り替えるだけ。パラメーターにも音にも触れない (SPEC §9)
    polyBrisk.getMetaState().setAdvancedView (advanced);
    shownAdvanced = advanced;
    simpleView.setVisible (! advanced);
    advancedView.setVisible (advanced);
}

void PolyBriskAudioProcessorEditor::showAdvancedTab (int index)
{
    simpleTab.setToggleState (false, juce::dontSendNotification);
    advancedTab.setToggleState (true, juce::dontSendNotification);
    setAdvanced (true);
    advancedView.showTab (index);
    advancedView.refresh();
}

void PolyBriskAudioProcessorEditor::showBrowser (bool show)
{
    browser.setVisible (show);

    if (show)
        browser.toFront (false);
}

void PolyBriskAudioProcessorEditor::timerCallback()
{
    // プリセット名と Dirty 表示 (SPEC §13)
    auto& meta = polyBrisk.getMetaState();
    const auto name = meta.getPresetInfo().name + (meta.isDirty() ? " *" : "");

    if (name != shownPresetName)
    {
        shownPresetName = name;
        presetName.setButtonText (name);
    }

    outputMeter.push (polyBrisk.takeOutputPeak (0), polyBrisk.takeOutputPeak (1));

    // 外部の MIDI キーボードのホイールに、画面のホイールの表示を合わせる (D-067)
    if (showKeyboard)
    {
        float v = 0.0f;

        if (polyBrisk.takeMidiPitchBend (v))
            bendWheel.setDisplayValue (v);

        if (polyBrisk.takeMidiModWheel (v))
            modWheel.setDisplayValue (v);
    }

    // ホストの状態復元などで表示モードが変わっていたら追従する
    if (meta.isAdvancedView() != shownAdvanced)
    {
        simpleTab.setToggleState (! meta.isAdvancedView(), juce::dontSendNotification);
        advancedTab.setToggleState (meta.isAdvancedView(), juce::dontSendNotification);
        setAdvanced (meta.isAdvancedView());
    }

    if (shownAdvanced)
        advancedView.refresh();
    else
        simpleView.refresh();
}

void PolyBriskAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);
}

void PolyBriskAudioProcessorEditor::resized()
{
    // 中身は常に基準サイズで配置し、ウインドウの大きさに合わせて拡大縮小する
    const float scale = static_cast<float> (getWidth()) / static_cast<float> (baseWidth);
    content.setTransform (juce::AffineTransform::scale (scale));
    content.setBounds (0, 0, baseWidth, baseHeight());
}

void PolyBriskAudioProcessorEditor::Content::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);

    auto header = getLocalBounds().removeFromTop (headerHeight);
    g.setColour (colours::panel);
    g.fillRect (header);
    g.setColour (colours::outline.withAlpha (0.7f));
    g.fillRect (header.removeFromBottom (1));

    drawLogoMark (g, juce::Rectangle<float> (12.0f, 9.0f, 36.0f, 36.0f));
    g.setGradientFill (juce::ColourGradient (colours::accentWarm, 56.0f, 0.0f, colours::accentHot, 190.0f, 0.0f, false));
    g.setFont (font (22.0f, true));
    g.drawText ("POLYBRISK", 56, 0, 150, headerHeight, juce::Justification::centredLeft);
}

void PolyBriskAudioProcessorEditor::Content::resized()
{
    owner.layoutContent();
}

void PolyBriskAudioProcessorEditor::layoutContent()
{
    auto bounds = content.getLocalBounds();
    auto header = bounds.removeFromTop (headerHeight).reduced (10, 11);
    header.removeFromLeft (200);

    previous.setBounds (header.removeFromLeft (30));
    header.removeFromLeft (4);
    presetName.setBounds (header.removeFromLeft (250));
    header.removeFromLeft (4);
    next.setBounds (header.removeFromLeft (30));
    header.removeFromLeft (10);
    save.setBounds (header.removeFromLeft (64));

    outputGain.setBounds (header.removeFromRight (150));
    outputLabel.setBounds (header.removeFromRight (36));

    // メーター: スライダーの溝の下 (数値の表示 62 px を除いた幅)。LIMIT の表示は OUT のラベルの下
    const auto track = outputGain.getBounds().withTrimmedRight (62).reduced (6, 0);
    outputMeter.setBounds (outputLabel.getX(), outputGain.getY(), track.getRight() - outputLabel.getX(), outputGain.getHeight());
    outputMeter.setBarLeft (track.getX() - outputLabel.getX());
    header.removeFromRight (8);
    advancedTab.setBounds (header.removeFromRight (104));
    simpleTab.setBounds (header.removeFromRight (88));

    if (showKeyboard)
    {
        auto strip = bounds.removeFromBottom (keyboardHeight);
        auto wheels = strip.removeFromLeft (wheelAreaWidth).reduced (5, 2);
        bendWheel.setBounds (wheels.removeFromLeft (wheelWidth));
        wheels.removeFromLeft (6);
        modWheel.setBounds (wheels.removeFromLeft (wheelWidth));
        keyboard.setBounds (strip);
    }

    simpleView.setBounds (bounds);
    advancedView.setBounds (bounds.reduced (6, 4));
    browser.setBounds (bounds);
}
