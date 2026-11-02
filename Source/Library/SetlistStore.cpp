#include "SetlistStore.h"
#include "../Text.h"

SetlistStore::SetlistStore()
    : SetlistStore (juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                        .getChildFile ("GuitarPractice")
                        .getChildFile ("Setlists"))
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
        if (auto setlist = Setlist::fromVar (juce::JSON::parse (file.loadFileAsString())))
            setlists.push_back (std::move (*setlist));

    std::sort (setlists.begin(), setlists.end(),
               [] (const Setlist& a, const Setlist& b) { return a.updatedAt > b.updatedAt; });
    return setlists;
}

std::optional<Setlist> SetlistStore::load (const juce::String& setlistId) const
{
    const auto file = fileFor (setlistId);
    if (! file.existsAsFile())
        return std::nullopt;

    return Setlist::fromVar (juce::JSON::parse (file.loadFileAsString()));
}

bool SetlistStore::save (Setlist& setlist)
{
    const auto now = juce::Time::getCurrentTime();

    if (setlist.setlistId.isEmpty())
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

juce::File SetlistStore::fileFor (const juce::String& setlistId) const
{
    return directory.getChildFile (setlistId + fileExtension);
}
