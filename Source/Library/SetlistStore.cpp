#include "SetlistStore.h"
#include "../Text.h"
#include "../AppPaths.h"
#include "../Model/SafeJson.h"

SetlistStore::SetlistStore()
    : SetlistStore (AppPaths::documentsFolder().getChildFile ("Setlists"))
{
}

SetlistStore::SetlistStore (const juce::File& dir)
    : directory (dir)
{
    directory.createDirectory();
}

std::vector<Setlist> SetlistStore::list() const
{
    std::vector<Setlist> setlists;

    for (const auto& file : directory.findChildFiles (juce::File::findFiles, false, juce::String ("*") + fileExtension))
        if (auto setlist = Setlist::fromVar (SafeJson::parseFile (file)))
        {
            // ファイルの中の ID がファイル名に使えないもの (フォルダの外を指すなど) なら、ファイル名を ID にする
            if (! isSafeId (setlist->setlistId))
                setlist->setlistId = file.getFileNameWithoutExtension();
            setlists.push_back (std::move (*setlist));
        }

    std::sort (setlists.begin(), setlists.end(),
               [] (const Setlist& a, const Setlist& b) { return a.updatedAt > b.updatedAt; });
    return setlists;
}

std::optional<Setlist> SetlistStore::load (const juce::String& setlistId) const
{
    const auto file = fileFor (setlistId);
    if (! file.existsAsFile())
        return std::nullopt;

    auto setlist = Setlist::fromVar (SafeJson::parseFile (file));
    if (setlist.has_value() && ! isSafeId (setlist->setlistId))
        setlist->setlistId = setlistId;   // 上の list() と同じ (ファイル名を ID にする)
    return setlist;
}

bool SetlistStore::save (Setlist& setlist)
{
    const auto now = juce::Time::getCurrentTime();

    if (! isSafeId (setlist.setlistId))
        setlist.setlistId = juce::Uuid().toString();

    if (setlist.createdAt.toMilliseconds() == 0)
        setlist.createdAt = now;

    setlist.updatedAt = now;

    if (! directory.createDirectory())
        return false;

    return fileFor (setlist.setlistId).replaceWithText (juce::JSON::toString (setlist.toVar()));
}

bool SetlistStore::remove (const juce::String& setlistId)
{
    if (! isSafeId (setlistId))
        return false;   // File() の deleteFile は「無いので成功」を返すため、先に断る
    return fileFor (setlistId).deleteFile();
}

Setlist SetlistStore::createNew() const
{
    juce::StringArray names;
    for (const auto& setlist : list())
        names.add (setlist.name);

    const auto prefix = "セットリスト "_jp;
    int number = 1;
    while (names.contains (prefix + juce::String (number)))
        ++number;

    Setlist setlist;
    setlist.setlistId = juce::Uuid().toString();
    setlist.name = prefix + juce::String (number);
    setlist.createdAt = setlist.updatedAt = juce::Time::getCurrentTime();
    return setlist;
}

int SetlistStore::countSetlistsUsing (const juce::String& sessionId) const
{
    int count = 0;
    for (const auto& setlist : list())
        if (setlist.countOf (sessionId) > 0)
            ++count;

    return count;
}

bool SetlistStore::isSafeId (const juce::String& setlistId)
{
    return setlistId.isNotEmpty() && setlistId.length() <= 64
        && setlistId.containsOnly ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_{}");
}

juce::File SetlistStore::fileFor (const juce::String& setlistId) const
{
    // 使えない ID は、どのファイルも指さない (保存は失敗し、削除は何もしない)
    if (! isSafeId (setlistId))
        return {};
    return directory.getChildFile (setlistId + fileExtension);
}
