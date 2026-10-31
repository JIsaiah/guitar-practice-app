#include "SessionStore.h"

SessionStore::SessionStore()
    : directory (juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                     .getChildFile ("GuitarPractice")
                     .getChildFile ("Sessions"))
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
                             parsed["title"].toString(),
                             file.getLastModificationTime() });
    }

    std::sort (entries.begin(), entries.end(),
               [] (const Entry& a, const Entry& b) { return a.modified > b.modified; });
    return entries;
}

std::optional<Session> SessionStore::load (const juce::String& id) const
{
    const auto file = fileFor (id);
    if (! file.existsAsFile())
        return std::nullopt;

    const auto parsed = juce::JSON::parse (file.loadFileAsString());
    if (! parsed.isObject())
        return std::nullopt;

    auto session = Session::fromVar (parsed);
    session.id = id;
    return session;
}

bool SessionStore::save (Session& session)
{
    if (session.id.isEmpty())
        session.id = juce::Uuid().toString();

    if (! directory.createDirectory())
        return false;

    return fileFor (session.id).replaceWithText (juce::JSON::toString (session.toVar()));
}

bool SessionStore::remove (const juce::String& id)
{
    return fileFor (id).deleteFile();
}

juce::File SessionStore::fileFor (const juce::String& id) const
{
    return directory.getChildFile (id + fileExtension);
}
