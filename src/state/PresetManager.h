#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "state/MetaState.h"

#include <functional>
#include <vector>

namespace polybrisk
{

/** プリセットの一覧・読込・保存 (SPEC §11)。メッセージスレッド専用。
    - Factory: バイナリに組み込んだ JSON (読み取り専用)。
    - User: ~/Library/Application Support/POLYBRISK/Presets/User/ の .json
    - お気に入りと Performance Lock はユーザー設定側 (settings.json) に保存し、プリセットファイルには含めない。 */
class PresetManager final : public juce::ChangeBroadcaster
{
public:
    struct Entry
    {
        PresetInfo info;
        juce::File file;            // User のみ
        int factoryIndex = -1;      // Factory のみ (組み込みリソースの番号)
    };

    /** userRoot を省略すると既定の場所 (Application Support) を使う。テストでは一時フォルダーを渡す。 */
    PresetManager (juce::AudioProcessorValueTreeState& apvts, MetaState& meta, const juce::File& userRoot = {});

    static juce::File defaultRoot();
    juce::File getUserPresetFolder() const { return root.getChildFile ("Presets").getChildFile ("User"); }

    /** Factory と User を読み直す。壊れたファイルは一覧に入れない。 */
    void rescan();

    /** 設定 (お気に入り・Performance Lock) と一覧を読み直す。ほかのインスタンスでの保存・削除・お気に入りを取り込む。
        ブラウザーを開くときに呼ぶ。 */
    void refresh() { loadSettings(); rescan(); }

    int getNumPresets() const noexcept { return static_cast<int> (entries.size()); }
    const Entry& getEntry (int index) const { return entries[static_cast<size_t> (index)]; }
    int getNumFactoryPresets() const noexcept;

    /** 検索 (名前・説明・タグ・カテゴリ)、カテゴリ、お気に入り、タグで絞り込んだ添字の一覧。
        tags を複数渡すと、そのすべてを持つ音色だけが残る (大文字小文字は区別しない)。 */
    std::vector<int> filter (const juce::String& search, const juce::String& category, bool favouritesOnly,
                             const juce::StringArray& tags = {}) const;
    juce::StringArray getCategories() const;

    /** 音色に付いているタグの一覧。使っている音色が多い順 (同数なら名前順)。表記は最初に見つかったもの。 */
    juce::StringArray getTags() const;

    /** 現在の音色に対応する一覧上の添字 (なければ -1)。 */
    int getCurrentIndex() const;

    juce::Result load (int index);

    /** 空の音色 (Init) にする: すべての設定を既定値に戻し、マクロの割当を消し、Gate を既定のパターンにする。
        音作りの出発点。一覧には載らない (Factory の音色数には数えない)。Output Gain は変えない (D-057)。
        Performance Lock が ON のあいだは、ほかの音色を読むときと同じく、ベンドとビブラートの設定も変えない。 */
    juce::Result loadInit();

    /** 一覧上で delta (+1 / -1) だけ隣のプリセットを読む。端では反対側へ回る。 */
    juce::Result loadAdjacent (int delta);

    /** 現在の音色を User プリセットとして保存する (Save As)。同じ名前の User プリセットは上書きする。
        名前は違うのにファイル名が重なる場合 ("A/B" と "AB" など) は、上書きせず別のファイルにする (D-068)。 */
    juce::Result saveUser (const juce::String& name, const juce::String& category, const juce::StringArray& tags,
                           const juce::String& description);

    /** いま読み込んでいる User プリセットを、そのファイルへ上書き保存する (ヘッダーの Save)。
        名前からファイルを決め直さないので、同名の別のプリセットを壊さない (D-068)。
        Factory や未保存の音色では失敗する (Save As を使う)。 */
    juce::Result saveCurrent();

    juce::Result deleteUser (int index);
    juce::Result renameUser (int index, const juce::String& newName);

    /** 外部の JSON を検証して User フォルダーへ取り込む。 */
    juce::Result importFile (const juce::File& source);

    /** 現在の音色を任意の場所へ書き出す。 */
    juce::Result exportCurrent (const juce::File& destination) const;

    bool isFavourite (const juce::String& presetId) const { return favourites.contains (presetId); }
    void setFavourite (const juce::String& presetId, bool shouldBeFavourite);

    bool getPerformanceLock() const noexcept { return performanceLock; }
    void setPerformanceLock (bool shouldLock);

    /** プリセットを読み込んだ直後に呼ばれる (プロセッサーが Arp の生成ノートを終了させる)。 */
    std::function<void()> onPresetLoaded;

    /** Factory プリセットの JSON テキスト (テスト・検証用)。 */
    static int getNumEmbeddedPresets();
    static juce::String getEmbeddedPresetJson (int index);
    static juce::String getEmbeddedPresetFileName (int index);

    static const juce::StringArray& standardCategories();

private:
    juce::Result loadJson (const juce::String& json, bool factory);
    juce::Result writePreset (const juce::File& file, const PresetInfo& info);
    juce::File fileForName (const juce::String& name) const;
    bool isInsideUserFolder (const juce::File& file) const;
    bool idInUse (const juce::String& presetId) const;
    void loadSettings();
    void saveSettings() const;
    juce::File settingsFile() const { return root.getChildFile ("settings.json"); }

    juce::AudioProcessorValueTreeState& apvts;
    MetaState& meta;
    juce::File root;
    std::vector<Entry> entries;
    juce::StringArray favourites;
    bool performanceLock = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetManager)
};

} // namespace polybrisk
