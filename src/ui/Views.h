#pragma once

#include "PluginProcessor.h"
#include "ui/Controls.h"

namespace polybrisk::ui
{

/** セクションを行単位で並べるページ (Advanced の各タブ)。収まらなければ縦にスクロールする。 */
class Page : public juce::Component
{
public:
    Section& add (const juce::String& title, float weight, bool newRow);
    void resized() override;
    int layout (int width, bool apply);

protected:
    struct Cell
    {
        std::unique_ptr<Section> section;
        float weight;
        bool newRow;
    };

    std::vector<Cell> cells;
};

/** Trance Gate のステップ編集グリッド。クリックで ON/OFF、縦ドラッグで高さ (音量) を変える。 */
class GateGrid final : public juce::Component
{
public:
    explicit GateGrid (PolyBriskAudioProcessor& p) : processor (p) {}

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

    /** 表示を更新する (タイマーから呼ぶ)。変化がなければ再描画しない。 */
    void refresh();

private:
    int stepCount() const;
    int stepAt (int x) const;
    bool isStereo() const;
    juce::Rectangle<float> laneBounds (int lane, bool stereo) const;
    void setFromMouse (const juce::MouseEvent&, bool toggle);

    PolyBriskAudioProcessor& processor;
    polybrisk::GatePattern shown;
    int shownSteps = 16;
    int shownPlaying = -1;
    float dragValue = 1.0f;
    int dragLane = -1;          // 0 = 左、1 = 右、-1 = 両方
    bool dragged = false;
    bool shownStereo = false;
};

/** Simple 表示: 4 マクロ、FX のサマリー、RHYTHM、最小限の Performance 表示 (SPEC §9.1)。 */
class SimpleView final : public juce::Component
{
public:
    explicit SimpleView (PolyBriskAudioProcessor&);

    void paint (juce::Graphics&) override;
    void resized() override;
    void refresh();

private:
    static constexpr int macroKnobSize = 92;

    struct Areas
    {
        juce::Rectangle<int> macros, tone, fx, gate, arp, footer;
    };

    Areas areas() const;

    PolyBriskAudioProcessor& processor;
    juce::AudioProcessorValueTreeState& apvts;

    std::array<std::unique_ptr<ParamKnob>, 4> macros;
    std::vector<std::unique_ptr<ParamToggle>> fxToggles;
    ParamToggle fxBypass, gateOn, arpOn;
    ParamKnob gateDepth;
    juce::ComboBox gatePattern;
    LabeledControl gatePatternBox;
    ParamChoice arpMode, arpRate;
    ParamKnob toneCutoff, toneResonance, toneAttack, toneRelease;   // よく直す 4 つ (D-065)。Advanced と同じパラメーター
    juce::Label info, performance;
    juce::String shownPattern;
    juce::String shownInfo;
    bool shownGate = false, shownArp = false, rhythmShown = false;
    bool shownBypass = false;
};

/** Macros タブ: マクロ名、値、MIDI Learn、割当 (対象・Low / High オフセット・カーブ) の編集 (SPEC §10)。 */
class MacroEditor final : public juce::Component, private juce::ChangeListener
{
public:
    explicit MacroEditor (PolyBriskAudioProcessor&);
    ~MacroEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void refresh();     // MIDI Learn の表示更新 (タイマー)

private:
    struct Row
    {
        juce::ComboBox target, curve;
        juce::Slider low { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
        juce::Slider high { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
        juce::TextButton remove { "x" };
        juce::String currentTarget;
    };

    struct Column
    {
        juce::TextEditor name;
        std::unique_ptr<ParamKnob> value;
        juce::TextButton learn { "MIDI Learn" }, clearCc { "Clear CC" }, add { "+ Add target" };
        juce::Label cc;
        std::vector<std::unique_ptr<Row>> rows;
    };

    void changeListenerCallback (juce::ChangeBroadcaster*) override { rebuild(); }
    void rebuild();
    void commitRow (int macroIndex, Row& row);

    PolyBriskAudioProcessor& processor;
    std::array<Column, 4> columns;
    juce::StringArray targetIds, targetNames;
    bool rebuilding = false;
};

/** Advanced 表示: OSC / Filter & EG / Performance / FX / Rhythm / Macros (SPEC §9.2 / §13)。 */
class AdvancedView final : public juce::Component
{
public:
    static constexpr int numTabs = 6;

    explicit AdvancedView (PolyBriskAudioProcessor&);

    void resized() override { tabs.setBounds (getLocalBounds()); }
    void refresh();
    void showTab (int index) { tabs.setCurrentTabIndex (index); }

private:
    struct OscControls
    {
        ParamChoice* wave = nullptr;
        ParamKnob* pulseWidth = nullptr;
        ParamKnob* octave = nullptr;
        ParamKnob* semitone = nullptr;
        ParamKnob* fine = nullptr;
        Section* section = nullptr;
        int shownWave = -1;
    };

    void buildOsc (Page& page, const polybrisk::params::OscIds& ids, const juce::String& title, OscControls& controls);

    PolyBriskAudioProcessor& processor;
    juce::AudioProcessorValueTreeState& apvts;
    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
    Page oscPage, filterPage, performancePage, fxPage, rhythmPage;
    juce::Viewport viewports[5];
    MacroEditor macroEditor;
    GateGrid gateGrid;
    static constexpr int envelopeDisplayHeight = 104;

    juce::ComboBox gatePattern;
    LabeledControl gatePatternBox { "Pattern", gatePattern };
    juce::TextButton gateUndo { "Undo" };
    EnvelopeDisplay filterEnvDisplay, ampEnvDisplay;
    OscControls oscA, oscB;
    /** 2 つのボタンを縦に重ねて 1 つの枠に置く (Undo / Random)。 */
    struct ButtonPair final : public juce::Component
    {
        ButtonPair (juce::Button& upper, juce::Button& lower) : first (upper), second (lower)
        {
            addAndMakeVisible (first);
            addAndMakeVisible (second);
        }

        void resized() override
        {
            first.setBounds (0, row::centreY - 27, getWidth(), 24);
            second.setBounds (0, row::centreY + 3, getWidth(), 24);
        }

        juce::Button& first;
        juce::Button& second;
    };

    juce::TextButton gateRandom { "Random" };
    ButtonPair gateButtons { gateUndo, gateRandom };
    juce::Random random;

    ParamKnob* gateWidth = nullptr;
    ParamKnob* lfoRate = nullptr;
    ParamChoice* lfoDivision = nullptr;
    Section* lfoSection = nullptr;
    std::vector<ParamToggle*> fxPowerToggles;
    int shownLfoSync = -1;
    bool shownBypass = false;
    bool shownStereo = false, stereoShown = false;
    polybrisk::GatePattern undoPattern;
    juce::String undoName, shownPattern;
    bool undoEdited = false, hasUndo = false;
    float undoRate = -1.0f;     // パターンを選ぶ前の gate.rate (選択肢の番号)。負なら Rate は戻さない
    float undoStereo = -1.0f;   // パターンを選ぶ前の gate.stereo (0 / 1)。負なら Stereo は戻さない
    juce::String undoPresetId;  // Undo を覚えたときの音色。別の音色に切り替えたら Undo を捨てる

    /** いまの音色を見分けるキー (ID と名前)。Init のように ID のない音色もあるので名前も含める。 */
    juce::String presetKey() const
    {
        const auto info = processor.getMetaState().getPresetInfo();
        return info.id + "\n" + info.name;
    }
};

/** プリセットブラウザー: 検索・カテゴリ・お気に入り・保存・削除・Import / Export (SPEC §11)。 */
class PresetBrowser final : public juce::Component, private juce::ListBoxModel, private juce::ChangeListener
{
public:
    explicit PresetBrowser (PolyBriskAudioProcessor&);
    ~PresetBrowser() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override {}   // 背面のコントロールへクリックを通さない

    std::function<void()> onClose;

    /** Save As のパネルを開く (ヘッダーの Save を Factory / 未保存の音色で押したとき)。 */
    void openSaveAs();

    void visibilityChanged() override;

    static constexpr int starColumnWidth = 30;   // 行の左端。ここをクリックするとお気に入りを付け外しする

private:
    int getNumRows() override { return static_cast<int> (visible.size()); }
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    void updateList();
    void rebuildTagButtons();
    int layoutTagButtons (juce::Rectangle<int> area, bool apply);   // 折り返して並べ、使った高さを返す
    void showSavePanel (bool show);
    void setMessage (const juce::String& text, bool isError);
    int currentPreset() const;     // いま読み込んでいる音色の添字 (一覧の選択行ではない)
    void disarmDelete();

    PolyBriskAudioProcessor& processor;
    polybrisk::PresetManager& manager;
    std::vector<int> visible;

    juce::TextEditor search;
    juce::ComboBox category;
    juce::TextButton favouritesOnly { "Favorites" }, close { "Close" };

    // タグの絞り込み: いま音色に付いているタグをボタンとして並べる。ON のタグをすべて持つ音色だけを表示する
    juce::OwnedArray<juce::TextButton> tagButtons;
    juce::StringArray knownTags, selectedTags;
    juce::TextButton clearTags { "Clear" };
    juce::ListBox list { "presets", this };
    juce::TextButton saveAs { "Save As..." }, remove { "Delete" }, importButton { "Import..." }, exportButton { "Export..." };
    juce::TextButton lock { "Performance Lock" };
    juce::TextButton initButton { "Init" };
    juce::Label message, detail;

    // Save As パネル
    juce::Component savePanel;
    juce::Label saveLabels[4];
    juce::TextEditor saveName, saveTags, saveDescription;
    juce::ComboBox saveCategory;
    juce::TextButton saveConfirm { "Save" }, saveCancel { "Cancel" };

    juce::String deleteArmedId;     // Delete の確認待ちの音色 (空なら待っていない)
    std::unique_ptr<juce::FileChooser> chooser;
};

} // namespace polybrisk::ui
