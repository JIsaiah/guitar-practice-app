#include "SessionLibrary.h"

SessionLibrary::SessionLibrary (SessionStore& s)
    : store (s)
{
}

void SessionLibrary::rescan()
{
    store.assignMissingSessionIds();

    items.clear();
    knownMissing.clear();
    for (const auto& entry : store.list())
        if (entry.sessionId.isNotEmpty())
            items[entry.sessionId] = { entry.id, entry.title, entry.lengthSeconds };
}

const SessionLibrary::Item* SessionLibrary::find (const juce::String& sessionId)
{
    if (sessionId.isEmpty())
        return nullptr;

    // 表にあっても、ファイルが消えていれば走査し直す (移動・アプリの外での削除)
    const auto isValid = [this] (auto it)
    {
        return it != items.end()
            && store.getDirectory().getChildFile (it->second.fileId + SessionStore::fileExtension).existsAsFile();
    };

    auto it = items.find (sessionId);
    if (! isValid (it))
    {
        if (knownMissing.count (sessionId) > 0)
            return nullptr;

        rescan();
        it = items.find (sessionId);
        if (! isValid (it))
        {
            knownMissing.insert (sessionId);
            return nullptr;
        }
    }

    return &it->second;
}

juce::String SessionLibrary::findFileId (const juce::String& sessionId)
{
    if (const auto* item = find (sessionId))
        return item->fileId;
    return {};
}

std::optional<Session> SessionLibrary::load (const juce::String& sessionId)
{
    const auto fileId = findFileId (sessionId);
    if (fileId.isEmpty())
        return std::nullopt;

    auto session = store.load (fileId);

    // ファイルの中身が別のセッションに置き換わっていたら、リンク切れとみなす
    if (session.has_value() && session->sessionId != sessionId)
    {
        rescan();
        return std::nullopt;
    }

    return session;
}

juce::String SessionLibrary::getTitle (const juce::String& sessionId)
{
    if (const auto* item = find (sessionId))
        return item->title;
    return {};
}

double SessionLibrary::getLengthSeconds (const juce::String& sessionId)
{
    if (const auto* item = find (sessionId))
        return item->lengthSeconds;
    return 0.0;
}

void SessionLibrary::noteSaved (const Session& session)
{
    if (session.sessionId.isNotEmpty() && session.id.isNotEmpty())
    {
        items[session.sessionId] = { session.id, session.title, session.lengthSeconds };
        knownMissing.erase (session.sessionId);
    }
}

void SessionLibrary::noteRemoved (const juce::String& sessionId)
{
    items.erase (sessionId);
}
