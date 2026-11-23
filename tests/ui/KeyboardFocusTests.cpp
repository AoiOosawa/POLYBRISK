#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "ui/Controls.h"

namespace
{
struct TempRoot
{
    TempRoot() : folder (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("polybrisk_test_" + juce::Uuid().toString()))
    {
        folder.createDirectory();
    }

    ~TempRoot() { folder.deleteRecursively(); }

    juce::File folder;
};

/** ホスト (DAW) のキー操作を横取りしないこと (D-076)。
    キーボードフォーカスを持ち続けてよいのは、文字の入力欄と、キーを処理しない「受け皿」だけ。
    ほかの部品 (ボタン、スイッチ、選択欄など) がフォーカスを受け取ったら、エディターが受け皿へ移す。

    ここで確かめるのは「どの部品なら移すか」の判断と、入力欄がフォーカスを手放す配線まで。
    実際にウインドウの中でフォーカスが移ること (Desktop からの通知) は、画面のない自動テストでは確かめられない。 */
class KeyboardFocusTests final : public juce::UnitTest
{
public:
    KeyboardFocusTests() : juce::UnitTest ("Keyboard focus", "ui") {}

    struct Census
    {
        int textEditors = 0, sinks = 0, buttons = 0, comboBoxes = 0, sliders = 0;
        juce::StringArray wronglyKept, wronglyMoved, unwiredEditors;
    };

    static juce::String describe (juce::Component& component)
    {
        return juce::String (typeid (component).name()) + " \"" + component.getName() + "\"";
    }

    void visit (PolyBriskAudioProcessorEditor& editor, juce::Component& component, Census& census, bool insideTextEditor)
    {
        const bool isTextEditor = dynamic_cast<juce::TextEditor*> (&component) != nullptr;
        const bool isSink = polybrisk::ui::isFocusSink (component);
        const bool mayKeep = isTextEditor || insideTextEditor || isSink;

        census.textEditors += isTextEditor ? 1 : 0;
        census.sinks += isSink ? 1 : 0;
        census.buttons += dynamic_cast<juce::Button*> (&component) != nullptr ? 1 : 0;
        census.comboBoxes += dynamic_cast<juce::ComboBox*> (&component) != nullptr ? 1 : 0;
        census.sliders += dynamic_cast<juce::Slider*> (&component) != nullptr ? 1 : 0;

        if (&component != &editor)
        {
            auto* target = editor.focusTargetFor (&component);

            if (mayKeep && target != nullptr)
                census.wronglyMoved.add (describe (component));

            if (! mayKeep && (target == nullptr || ! polybrisk::ui::isFocusSink (*target)))
                census.wronglyKept.add (describe (component));
        }

        // 入力欄は、Esc (と、1 行の欄の Return) でフォーカスを手放すよう配線されている
        if (auto* text = dynamic_cast<juce::TextEditor*> (&component); text != nullptr && ! insideTextEditor)
            if (text->onEscapeKey == nullptr || (! text->isMultiLine() && text->onReturnKey == nullptr))
                census.unwiredEditors.add (describe (component));

        for (auto* child : component.getChildren())
            visit (editor, *child, census, insideTextEditor || isTextEditor);
    }

    void checkEditor (juce::AudioProcessor::WrapperType wrapper, const juce::String& what)
    {
        TempRoot root;
        juce::AudioProcessor::setTypeOfNextNewPlugin (wrapper);
        PolyBriskAudioProcessor processor (root.folder);
        juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_Undefined);

        // User プリセットを 1 つ作っておく (一覧とタグのボタンに User の分も含める)
        expect (processor.getPresetManager().load (0).wasOk());
        expect (processor.getPresetManager().saveUser ("Focus Test", "Bass", { "MyTag" }, "x").wasOk());

        std::unique_ptr<juce::AudioProcessorEditor> base (processor.createEditor());
        auto* editor = dynamic_cast<PolyBriskAudioProcessorEditor*> (base.get());
        expect (editor != nullptr);

        if (editor == nullptr)
            return;

        const auto check = [&] (const juce::String& state)
        {
            Census census;
            visit (*editor, *editor, census, false);
            const auto where = what + " / " + state + ": ";
            expect (census.wronglyKept.isEmpty(), where + "would keep the keyboard: " + census.wronglyKept.joinIntoString (", "));
            expect (census.wronglyMoved.isEmpty(), where + "would lose the keyboard: " + census.wronglyMoved.joinIntoString (", "));
            expect (census.unwiredEditors.isEmpty(), where + "text field never releases the keyboard: " + census.unwiredEditors.joinIntoString (", "));
            expect (census.sinks >= 1, where + "no focus sink");
            return census;
        };

        // Simple
        const auto simple = check ("simple");
        expect (simple.buttons > 10 && simple.comboBoxes >= 3 && simple.sliders >= 8, what + ": the walk did not reach the controls");

        // Advanced の全タブ (表示していないタブの部品は階層にいないので、1 つずつ表示して確かめる)
        int advancedControls = 0, macroNameFields = 0;

        for (int tab = 0; tab < PolyBriskAudioProcessorEditor::numAdvancedTabs; ++tab)
        {
            editor->showAdvancedTab (tab);
            const auto census = check ("advanced tab " + juce::String (tab));
            advancedControls += census.buttons + census.comboBoxes + census.sliders;
            macroNameFields = juce::jmax (macroNameFields, census.textEditors);
        }

        expect (advancedControls > 300, what + ": the walk did not reach the Advanced pages");

        // ブラウザー (検索欄 1 + Save As の入力欄 3) を開いた状態。Macros タブの名前欄 4 つも入力欄
        editor->showBrowser (true);
        const auto browser = check ("browser");
        expect (browser.textEditors >= 4, what + ": expected the search and Save As fields");
        expect (macroNameFields >= 8, what + ": expected the macro name fields");

        // 自分の画面の外の部品と、何もフォーカスを持っていない状態には関与しない
        juce::TextButton outside;
        expect (editor->focusTargetFor (&outside) == nullptr);
        expect (editor->focusTargetFor (nullptr) == nullptr);
    }

    void runTest() override
    {
        beginTest ("Buttons, switches, menus and knobs hand the keyboard to the sink; text fields keep it and know how to release it");
        {
            checkEditor (juce::AudioProcessor::wrapperType_VST3, "VST3");
            checkEditor (juce::AudioProcessor::wrapperType_AudioUnit, "AU");
            checkEditor (juce::AudioProcessor::wrapperType_Standalone, "Standalone");     // 画面内鍵盤とホイールを含む
        }

        beginTest ("mayHoldKeyboardFocus: text fields and everything inside them, and sinks; nothing else");
        {
            juce::Component parent, plain;
            juce::TextEditor text;
            juce::TextButton button { "Button" };
            juce::ComboBox combo;
            juce::Slider slider;
            juce::Label inner;
            text.addAndMakeVisible (inner);

            for (auto* c : std::initializer_list<juce::Component*> { &plain, &text, &button, &combo, &slider })
                parent.addAndMakeVisible (*c);

            expect (polybrisk::ui::mayHoldKeyboardFocus (text));
            expect (polybrisk::ui::mayHoldKeyboardFocus (inner));       // 入力欄の内側
            expect (! polybrisk::ui::mayHoldKeyboardFocus (button));
            expect (! polybrisk::ui::mayHoldKeyboardFocus (combo));
            expect (! polybrisk::ui::mayHoldKeyboardFocus (slider));
            expect (! polybrisk::ui::mayHoldKeyboardFocus (plain));
            expect (! polybrisk::ui::mayHoldKeyboardFocus (parent));

            polybrisk::ui::markAsFocusSink (parent);
            expect (polybrisk::ui::isFocusSink (parent));
            expect (parent.getWantsKeyboardFocus());
            expect (polybrisk::ui::mayHoldKeyboardFocus (parent));
            expect (! polybrisk::ui::mayHoldKeyboardFocus (button));    // 受け皿の子は受け皿ではない
        }

        beginTest ("releaseKeysAfterTextEntry: Escape always, Return on single-line fields; existing handlers still run, once");
        {
            juce::Component parent;
            juce::TextEditor single, multi;
            multi.setMultiLine (true);
            int returns = 0, escapes = 0;
            single.onReturnKey = [&returns] { ++returns; };
            single.onEscapeKey = [&escapes] { ++escapes; };
            parent.addAndMakeVisible (single);
            parent.addAndMakeVisible (multi);

            polybrisk::ui::releaseKeysAfterTextEntry (parent);
            polybrisk::ui::releaseKeysAfterTextEntry (parent);      // 何度呼んでもよい (処理を二重に重ねない)

            expect (multi.onEscapeKey != nullptr);
            expect (multi.onReturnKey == nullptr);                  // 複数行の欄の Return には手を付けない

            single.onReturnKey();
            single.onEscapeKey();
            expectEquals (returns, 1);
            expectEquals (escapes, 1);
        }
    }
};

KeyboardFocusTests keyboardFocusTests;
} // namespace
