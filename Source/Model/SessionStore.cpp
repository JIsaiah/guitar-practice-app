#include "SessionStore.h"

SessionStore::SessionStore()
    : SessionStore (juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                        .getChildFile ("GuitarPractice")
                        .getChildFile ("Sessions"))
{
}

SessionStore::SessionStore (const juce::File& dir)
    : directory (dir)
{
    directory.createDirectory();
}

std::vector<SessionStore::Entry> SessionStore::list() const
{
    std::vector<Entry> entries;

    for (const auto& file : directory.findChildFiles (juce::File::findFiles, false, juce::String ("*") + fileExtension))
    {
        const auto parsed = juce::JSON::parse (file.loadFileAsString());
        if (! parsed.isObject())
            continue;

        entries.push_back ({ file.getFileNameWithoutExtension(),
                             parsed["session_id"].toString(),
                             parsed["title"].toString(),
                             file.getLastModificationTime(),
                             (double) parsed["source"]["length_sec"] });
    }

    std::sort (entries.begin(), entries.end(),
               [] (const Entry& a, const Entry& b) { return a.modified > b.modified; });
    return entries;
}

std::optional<Session> SessionStore::load (const juce::String& id)
{
    const auto file = fileFor (id);
    if (! file.existsAsFile())
        return std::nullopt;

    auto parsed = juce::JSON::parse (file.loadFileAsString());
    if (! parsed.isObject())
        return std::nullopt;

    ensureSessionId (file, parsed);

    auto session = Session::fromVar (parsed);
    session.id = id;
    return session;
}

bool SessionStore::save (Session& session)
{
    if (session.id.isEmpty())
        session.id = juce::Uuid().toString();

    if (session.sessionId.isEmpty())
        session.sessionId = juce::Uuid().toString();

    if (! directory.createDirectory())
        return false;

    return fileFor (session.id).replaceWithText (juce::JSON::toString (session.toVar()));
}

bool SessionStore::remove (const juce::String& id)
{
    return fileFor (id).deleteFile();
}

void SessionStore::assignMissingSessionIds()
{
    for (const auto& file : directory.findChildFiles (juce::File::findFiles, false, juce::String ("*") + fileExtension))
    {
        auto parsed = juce::JSON::parse (file.loadFileAsString());
        if (parsed.isObject())
            ensureSessionId (file, parsed);
    }
}

juce::String SessionStore::ensureSessionId (const juce::File& file, juce::var& parsed)
{
    auto sessionId = parsed["session_id"].toString();
    if (sessionId.isNotEmpty())
        return sessionId;

    sessionId = juce::Uuid().toString();

    if (auto* object = parsed.getDynamicObject())
    {
        object->setProperty ("session_id", sessionId);
        if (! object->hasProperty ("version"))
            object->setProperty ("version", 1);   // バージョンの無い古いファイル。中身の形はそのままなので 1 とする

        // 一覧は更新日時の新しい順に並ぶ。ID を付けただけで順番が変わらないよう、更新日時を戻す
        const auto modified = file.getLastModificationTime();
        if (file.replaceWithText (juce::JSON::toString (parsed)))
            file.setLastModificationTime (modified);
    }

    return sessionId;
}

juce::File SessionStore::fileFor (const juce::String& id) const
{
    return directory.getChildFile (id + fileExtension);
}
