#include "state/PresetManager.h"

#include "PresetData.h"
#include "state/PresetSerializer.h"

namespace polybrisk
{

namespace
{
/** プリセット名からファイル名 (拡張子なし) を作る。フォルダーの外を指せる文字は残さない:
    区切り文字は createLegalFileName が落とす。先頭の "~" (JUCE がホームとして解釈する) と "." (隠しファイル、"..") はここで落とす。 */
juce::String safeFileName (const juce::String& name)
{
    auto cleaned = juce::File::createLegalFileName (name.trim());

    while (cleaned.startsWithChar ('~') || cleaned.startsWithChar ('.') || cleaned.startsWithChar (' '))
        cleaned = cleaned.substring (1);

    cleaned = cleaned.substring (0, 60).trim();
    return cleaned.isNotEmpty() ? cleaned : juce::String ("Untitled");
}

/** タグの上限 (読み込み側の parseInfo と同じ: 16 個、各 32 文字)。"|" はホストの状態での区切りなので使わせない。 */
juce::StringArray tidyTags (const juce::StringArray& tags)
{
    juce::StringArray result;

    for (const auto& tag : tags)
    {
        const auto cleaned = tag.removeCharacters ("|").trim().substring (0, 32);

        if (cleaned.isNotEmpty() && result.size() < 16)
            result.addIfNotAlreadyThere (cleaned);
    }

    return result;
}

constexpr int maxUserPresetFiles = 5000;     // これを超える分は一覧に入れない (ホストの起動を止めないため)
} // namespace

const juce::StringArray& PresetManager::standardCategories()
{
    // SPEC §11.1 のカテゴリ (Trance Gate は D-042、Bass と Pad / Strings は D-054 で追加)
    static const juce::StringArray categories { "SuperSAW", "Synth Brass", "Stab / Chord Hit", "Bright Poly",
                                                "Synth Pluck", "Lead / Hook", "Bass", "Pad / Strings",
                                                "Octave / Layered", "Trance Gate" };
    return categories;
}

PresetManager::PresetManager (juce::AudioProcessorValueTreeState& state, MetaState& metaState, const juce::File& userRoot)
    : apvts (state), meta (metaState), root (userRoot == juce::File() ? defaultRoot() : userRoot)
{
    loadSettings();
    rescan();
}

juce::File PresetManager::defaultRoot()
{
#if JUCE_WINDOWS
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
               .getChildFile ("CHEEBOW").getChildFile ("POLYBRISK");
#else
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("Application Support").getChildFile ("POLYBRISK");
#endif
}

int PresetManager::getNumEmbeddedPresets()
{
    return PresetData::namedResourceListSize;
}

juce::String PresetManager::getEmbeddedPresetJson (int index)
{
    if (index < 0 || index >= PresetData::namedResourceListSize)
        return {};

    int size = 0;
    const char* data = PresetData::getNamedResource (PresetData::namedResourceList[index], size);
    return data != nullptr ? juce::String::fromUTF8 (data, size) : juce::String();
}

juce::String PresetManager::getEmbeddedPresetFileName (int index)
{
    if (index < 0 || index >= PresetData::namedResourceListSize)
        return {};

    return PresetData::originalFilenames[index];
}

int PresetManager::getNumFactoryPresets() const noexcept
{
    int count = 0;
    for (const auto& e : entries)
        count += e.info.factory ? 1 : 0;
    return count;
}

void PresetManager::rescan()
{
    entries.clear();

    for (int i = 0; i < getNumEmbeddedPresets(); ++i)
    {
        Entry entry;

        if (preset::parseInfo (getEmbeddedPresetJson (i), entry.info).wasOk())
        {
            entry.info.factory = true;
            entry.factoryIndex = i;

            if (entry.info.id.isEmpty())
                entry.info.id = "factory:" + getEmbeddedPresetFileName (i);

            entries.push_back (entry);
        }
    }

    const auto folder = getUserPresetFolder();

    if (folder.isDirectory())
    {
        auto files = folder.findChildFiles (juce::File::findFiles, false, "*.json");
        files.sort();       // 読む順をファイル名で固定する (ID が重なったときに、どれが元の ID を持つかが毎回同じになる)
        int scanned = 0;

        for (const auto& file : files)
        {
            if (++scanned > maxUserPresetFiles)
                break;

            // 大きすぎるファイルは読まない (破損・誤配置への備え)
            if (file.getSize() > 1024 * 1024)
                continue;

            Entry entry;

            if (preset::parseInfo (file.loadFileAsString(), entry.info).wasOk())
            {
                entry.info.factory = false;
                entry.file = file;

                // ID は一覧の中で一意にする。空、または既にある ID (Factory を書き出して取り込んだコピー、
                // 同じファイルを二度取り込んだもの、手でコピーしたファイル) は、ファイル名から作る。
                // 重なったままだと、現在の音色の特定・前後移動・お気に入りが別の音色を指す (D-068)。
                if (entry.info.id.isEmpty() || idInUse (entry.info.id))
                    entry.info.id = "user:" + file.getFileNameWithoutExtension();

                entries.push_back (entry);
            }
        }
    }

    // Factory を先に、カテゴリ (SPEC の順) ごとに並べる。カテゴリ内は、Factory は収録順 (ファイル番号順。
    // 代表の音色を先頭に置ける。D-043)、User は名前の順
    const auto categoryRank = [] (const juce::String& category)
    {
        const int index = standardCategories().indexOf (category);
        return index >= 0 ? index : standardCategories().size();
    };

    std::stable_sort (entries.begin(), entries.end(), [&] (const Entry& a, const Entry& b)
    {
        if (a.info.factory != b.info.factory)
            return a.info.factory;

        const int ra = categoryRank (a.info.category), rb = categoryRank (b.info.category);
        if (ra != rb)
            return ra < rb;

        if (a.info.factory)
            return a.factoryIndex < b.factoryIndex;

        return a.info.name.compareNatural (b.info.name) < 0;
    });

    sendChangeMessage();
}

std::vector<int> PresetManager::filter (const juce::String& search, const juce::String& category, bool favouritesOnly,
                                        const juce::StringArray& tags) const
{
    std::vector<int> result;
    juce::StringArray words;
    words.addTokens (search.toLowerCase(), " \t", "");
    words.removeEmptyStrings();

    for (int i = 0; i < getNumPresets(); ++i)
    {
        const auto& info = entries[static_cast<size_t> (i)].info;

        if (category.isNotEmpty() && info.category != category)
            continue;

        if (favouritesOnly && ! isFavourite (info.id))
            continue;

        bool hasAllTags = true;
        for (const auto& tag : tags)
            hasAllTags = hasAllTags && info.tags.contains (tag, true);

        if (! hasAllTags)
            continue;

        const auto haystack = (info.name + " " + info.description + " " + info.category + " " + info.tags.joinIntoString (" ")).toLowerCase();
        bool matches = true;

        for (const auto& word : words)
            matches = matches && haystack.contains (word);

        if (matches)
            result.push_back (i);
    }

    return result;
}

juce::StringArray PresetManager::getTags() const
{
    juce::StringArray names;
    std::vector<int> counts;

    for (const auto& e : entries)
        for (const auto& tag : e.info.tags)
        {
            const auto trimmed = tag.trim();
            if (trimmed.isEmpty())
                continue;

            const int existing = names.indexOf (trimmed, true);

            if (existing >= 0)
                ++counts[static_cast<size_t> (existing)];
            else
            {
                names.add (trimmed);
                counts.push_back (1);
            }
        }

    std::vector<int> order (static_cast<size_t> (names.size()));
    for (size_t i = 0; i < order.size(); ++i)
        order[i] = static_cast<int> (i);

    std::sort (order.begin(), order.end(), [&] (int a, int b)
    {
        const int ca = counts[static_cast<size_t> (a)], cb = counts[static_cast<size_t> (b)];
        return ca != cb ? ca > cb : names[a].compareIgnoreCase (names[b]) < 0;
    });

    juce::StringArray sorted;
    for (const int i : order)
        sorted.add (names[i]);

    return sorted;
}

juce::StringArray PresetManager::getCategories() const
{
    juce::StringArray categories;

    for (const auto& standard : standardCategories())
        for (const auto& e : entries)
            if (e.info.category == standard)
            {
                categories.addIfNotAlreadyThere (standard);
                break;
            }

    for (const auto& e : entries)
        if (e.info.category.isNotEmpty())
            categories.addIfNotAlreadyThere (e.info.category);

    return categories;
}

int PresetManager::getCurrentIndex() const
{
    const auto current = meta.getPresetInfo();

    if (current.id.isEmpty())
        return -1;

    for (int i = 0; i < getNumPresets(); ++i)
        if (entries[static_cast<size_t> (i)].info.id == current.id)
            return i;

    return -1;
}

bool PresetManager::idInUse (const juce::String& presetId) const
{
    for (const auto& e : entries)
        if (e.info.id == presetId)
            return true;

    return false;
}

bool PresetManager::isInsideUserFolder (const juce::File& file) const
{
    return file.getParentDirectory() == getUserPresetFolder();
}

juce::Result PresetManager::loadJson (const juce::String& json, bool factory)
{
    preset::Parsed parsed;
    const auto result = preset::parse (json, parsed);

    if (result.failed())
        return result;      // 壊れたプリセットでは現在の音色を変えない

    parsed.info.factory = factory;

    // Performance Lock は別のインスタンスが切り替えているかもしれないので、適用の前に読み直す
    loadSettings();

    if (! preset::apply (parsed, apvts, meta, performanceLock))
        return juce::Result::fail ("Could not apply the preset");

    if (onPresetLoaded != nullptr)
        onPresetLoaded();

    return juce::Result::ok();
}

juce::Result PresetManager::load (int index)
{
    if (index < 0 || index >= getNumPresets())
        return juce::Result::fail ("No presets");

    const auto entry = entries[static_cast<size_t> (index)];
    const auto json = entry.info.factory ? getEmbeddedPresetJson (entry.factoryIndex) : entry.file.loadFileAsString();
    const auto result = loadJson (json, entry.info.factory);

    if (result.wasOk())
    {
        // 一覧と同じ ID を使う (ファイルに id がない場合のフォールバックを含む)
        auto info = meta.getPresetInfo();
        info.id = entry.info.id;
        meta.setPresetInfo (info);
        meta.setDirty (false);
        sendChangeMessage();
    }

    return result;
}

juce::Result PresetManager::loadInit()
{
    // 項目のないプリセットを読むと、すべて既定値になる (欠落は既定値で補う)。一覧の音色ではないので ID は空
    const auto result = loadJson (R"({ "schemaVersion": 1, "name": "Init", "parameters": {} })", false);

    if (result.wasOk())
    {
        auto info = meta.getPresetInfo();
        info.id = {};
        meta.setPresetInfo (info);
        meta.setDirty (false);
        sendChangeMessage();
    }

    return result;
}

juce::Result PresetManager::loadAdjacent (int delta)
{
    const int count = getNumPresets();

    if (count == 0)
        return juce::Result::fail ("No presets");

    const int current = getCurrentIndex();
    const int next = current < 0 ? (delta >= 0 ? 0 : count - 1) : ((current + delta) % count + count) % count;
    return load (next);
}

juce::File PresetManager::fileForName (const juce::String& name) const
{
    const auto folder = getUserPresetFolder();
    const auto stem = safeFileName (name);
    auto file = folder.getChildFile (stem + ".json");

    // 既にあるファイルを上書きしてよいのは、中身が同じ名前の音色のときだけ。
    // 名前が違う ("A/B" と "AB"、大文字小文字だけ違う、60 文字より先だけ違う)、または読めないファイルなら、番号を付けて避ける。
    for (int n = 2; file.exists() && n < 1000; ++n)
    {
        PresetInfo existing;

        if (file.existsAsFile() && file.getSize() <= 1024 * 1024
            && preset::parseInfo (file.loadFileAsString(), existing).wasOk() && existing.name == name)
            break;

        file = folder.getChildFile (stem + " (" + juce::String (n) + ").json");
    }

    return file;
}

juce::Result PresetManager::writePreset (const juce::File& file, const PresetInfo& info)
{
    // User フォルダーの直下以外には書かない (名前の処理に漏れがあっても、外へは出さない)
    if (! isInsideUserFolder (file))
        return juce::Result::fail ("That name cannot be used");

    if (! file.replaceWithText (preset::toJson (apvts, meta, info)))
        return juce::Result::fail ("Could not save: " + file.getFullPathName());

    meta.setPresetInfo (info);
    meta.setDirty (false);
    rescan();
    return juce::Result::ok();
}

juce::Result PresetManager::saveUser (const juce::String& name, const juce::String& category, const juce::StringArray& tags,
                                      const juce::String& description)
{
    const auto trimmed = name.trim().substring (0, 64);

    if (trimmed.isEmpty())
        return juce::Result::fail ("Enter a name");

    const auto folder = getUserPresetFolder();

    if (! folder.createDirectory())
        return juce::Result::fail ("Could not create the folder: " + folder.getFullPathName());

    const auto file = fileForName (trimmed);

    PresetInfo info;
    info.name = trimmed;
    info.category = category.trim().substring (0, 64);
    info.description = description.trim().substring (0, 512);
    info.tags = tidyTags (tags);
    info.author = "User";
    info.factory = false;

    // 同名を上書きする場合は、一覧上の ID を引き継ぐ (お気に入りを保つ)
    for (const auto& e : entries)
        if (! e.info.factory && e.file == file)
            info.id = e.info.id;

    if (info.id.isEmpty())
        info.id = juce::Uuid().toDashedString();

    return writePreset (file, info);
}

juce::Result PresetManager::saveCurrent()
{
    const int index = getCurrentIndex();

    if (index < 0 || entries[static_cast<size_t> (index)].info.factory)
        return juce::Result::fail ("Not a User preset that can be overwritten");

    const auto entry = entries[static_cast<size_t> (index)];

    auto info = meta.getPresetInfo();
    info.id = entry.info.id;
    info.tags = tidyTags (info.tags);
    info.factory = false;
    return writePreset (entry.file, info);
}

juce::Result PresetManager::deleteUser (int index)
{
    if (index < 0 || index >= getNumPresets() || entries[static_cast<size_t> (index)].info.factory)
        return juce::Result::fail ("Factory presets cannot be deleted");

    const auto entry = entries[static_cast<size_t> (index)];

    if (! entry.file.deleteFile())
        return juce::Result::fail ("Could not delete: " + entry.file.getFullPathName());

    setFavourite (entry.info.id, false);
    rescan();
    return juce::Result::ok();
}

juce::Result PresetManager::renameUser (int index, const juce::String& newName)
{
    if (index < 0 || index >= getNumPresets() || entries[static_cast<size_t> (index)].info.factory)
        return juce::Result::fail ("Factory presets cannot be renamed");

    const auto trimmed = newName.trim().substring (0, 64);

    if (trimmed.isEmpty())
        return juce::Result::fail ("Enter a name");

    const auto entry = entries[static_cast<size_t> (index)];
    juce::var json;

    if (juce::JSON::parse (entry.file.loadFileAsString(), json).failed() || json.getDynamicObject() == nullptr)
        return juce::Result::fail ("Could not read the preset");

    json.getDynamicObject()->setProperty ("name", trimmed);
    json.getDynamicObject()->setProperty ("id", entry.info.id);      // ファイル名由来の ID でも、名前を変えたあとに同じ音色として扱う
    const auto target = getUserPresetFolder().getChildFile (safeFileName (trimmed) + ".json");

    if (! isInsideUserFolder (target))
        return juce::Result::fail ("That name cannot be used");

    if (target != entry.file && target.exists())
        return juce::Result::fail ("A preset with that name already exists");

    if (! target.replaceWithText (juce::JSON::toString (json)))
        return juce::Result::fail ("Could not save");

    if (target != entry.file)
        entry.file.deleteFile();

    auto current = meta.getPresetInfo();
    if (current.id == entry.info.id)
    {
        current.name = trimmed;
        meta.setPresetInfo (current);
    }

    rescan();
    return juce::Result::ok();
}

juce::Result PresetManager::importFile (const juce::File& source)
{
    if (! source.existsAsFile() || source.getSize() > 1024 * 1024)
        return juce::Result::fail ("The file cannot be read");

    auto json = source.loadFileAsString();
    preset::Parsed parsed;
    const auto result = preset::parse (json, parsed);

    if (result.failed())
        return result;

    const auto folder = getUserPresetFolder();

    if (! folder.createDirectory())
        return juce::Result::fail ("Could not create the folder");

    // ID が空、または既に一覧にある (Factory を書き出したもの、同じファイルの二度目の取り込み) なら、新しい ID を振る (D-068)
    if (parsed.info.id.isEmpty() || idInUse (parsed.info.id))
    {
        juce::var document;

        if (juce::JSON::parse (json, document).wasOk() && document.getDynamicObject() != nullptr)
        {
            document.getDynamicObject()->setProperty ("id", juce::Uuid().toDashedString());
            json = juce::JSON::toString (document);
        }
    }

    const auto target = folder.getNonexistentChildFile (safeFileName (parsed.info.name), ".json", false);

    if (! isInsideUserFolder (target))
        return juce::Result::fail ("That name cannot be used");

    if (! target.replaceWithText (json))
        return juce::Result::fail ("Could not copy the file");

    rescan();
    return juce::Result::ok();
}

juce::Result PresetManager::exportCurrent (const juce::File& destination) const
{
    auto info = meta.getPresetInfo();

    // Factory の音色を書き出すときは、新しい ID にする (取り込んだコピーが Factory と同じ音色として扱われないように)
    if (info.factory || info.id.isEmpty() || info.id.startsWith ("factory:") || info.id.startsWith ("user:"))
        info.id = juce::Uuid().toDashedString();

    info.factory = false;

    return destination.replaceWithText (preset::toJson (apvts, meta, info))
               ? juce::Result::ok()
               : juce::Result::fail ("Could not export: " + destination.getFullPathName());
}

void PresetManager::setFavourite (const juce::String& presetId, bool shouldBeFavourite)
{
    // 設定ファイルは複数のインスタンスが共有する。書く前に読み直して、ほかのインスタンスの変更を消さない (D-068)
    loadSettings();

    if (presetId.isEmpty() || shouldBeFavourite == favourites.contains (presetId))
        return;

    if (shouldBeFavourite)
        favourites.add (presetId);
    else
        favourites.removeString (presetId);

    saveSettings();
    sendChangeMessage();
}

void PresetManager::setPerformanceLock (bool shouldLock)
{
    loadSettings();     // 書く前に読み直す (setFavourite と同じ)

    if (performanceLock == shouldLock)
        return;

    performanceLock = shouldLock;
    saveSettings();
    sendChangeMessage();
}

void PresetManager::loadSettings()
{
    favourites.clear();
    performanceLock = false;

    const auto file = settingsFile();
    juce::var json;

    if (! file.existsAsFile() || file.getSize() > 1024 * 1024 || juce::JSON::parse (file.loadFileAsString(), json).failed())
        return;

    if (const auto* list = json["favourites"].getArray())
        for (const auto& id : *list)
            if (id.isString())
                favourites.addIfNotAlreadyThere (id.toString());

    performanceLock = json["performanceLock"].isBool() && static_cast<bool> (json["performanceLock"]);
}

void PresetManager::saveSettings() const
{
    auto* object = new juce::DynamicObject();
    juce::var json (object);

    juce::Array<juce::var> list;
    for (const auto& id : favourites)
        list.add (id);

    object->setProperty ("favourites", list);
    object->setProperty ("performanceLock", performanceLock);

    if (settingsFile().getParentDirectory().createDirectory())
        settingsFile().replaceWithText (juce::JSON::toString (json));
}

} // namespace polybrisk
