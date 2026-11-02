#include "Setlist.h"

namespace
{
    juce::var makeObject (std::initializer_list<std::pair<juce::Identifier, juce::var>> properties)
    {
        auto* object = new juce::DynamicObject();

        for (const auto& [key, value] : properties)
            object->setProperty (key, value);

        return juce::var (object);
    }

    juce::Time timeFromString (const juce::String& text)
    {
        return text.isEmpty() ? juce::Time() : juce::Time::fromISO8601 (text);
    }

    double doubleOr (const juce::var& value, double fallback)
    {
        return value.isVoid() ? fallback : (double) value;
    }
}

//==============================================================================
SetlistEntry& Setlist::addEntry (const juce::String& sessionId, const Session* source)
{
    SetlistEntry entry;
    entry.entryId = juce::Uuid().toString();
    entry.sessionId = sessionId;

    if (SetlistDefaults::copyLoopRangeFromSession && source != nullptr)
    {
        entry.overrides.loopStartSec = source->loopStartMs / 1000.0;
        entry.overrides.loopEndSec = source->loopEndMs / 1000.0;
    }

    entries.push_back (entry);
    return entries.back();
}

bool Setlist::removeEntry (const juce::String& entryId)
{
    const auto index = indexOf (entryId);
    if (index < 0)
        return false;

    entries.erase (entries.begin() + index);
    return true;
}

bool Setlist::moveEntry (const juce::String& entryId, int newIndex)
{
    const auto index = indexOf (entryId);
    if (index < 0)
        return false;

    newIndex = juce::jlimit (0, (int) entries.size() - 1, newIndex);
    if (newIndex == index)
        return true;

    auto entry = entries[(size_t) index];
    entries.erase (entries.begin() + index);
    entries.insert (entries.begin() + newIndex, entry);
    return true;
}

int Setlist::indexOf (const juce::String& entryId) const
{
    for (size_t i = 0; i < entries.size(); ++i)
        if (entries[i].entryId == entryId)
            return (int) i;

    return -1;
}

SetlistEntry* Setlist::findEntry (const juce::String& entryId)
{
    const auto index = indexOf (entryId);
    return index >= 0 ? &entries[(size_t) index] : nullptr;
}

const SetlistEntry* Setlist::findEntry (const juce::String& entryId) const
{
    const auto index = indexOf (entryId);
    return index >= 0 ? &entries[(size_t) index] : nullptr;
}

int Setlist::countOf (const juce::String& sessionId) const
{
    return (int) std::count_if (entries.begin(), entries.end(),
                                [&] (const SetlistEntry& e) { return e.sessionId == sessionId; });
}

//==============================================================================
juce::var Setlist::toVar() const
{
    juce::Array<juce::var> entryList;

    for (const auto& entry : entries)
    {
        const auto& o = entry.overrides;
        entryList.add (makeObject ({
            { "entryId",   entry.entryId },
            { "sessionId", entry.sessionId },
            { "overrides", makeObject ({
                { "speed", o.speed },
                { "loop",  makeObject ({ { "enabled", o.loopEnabled }, { "startSec", o.loopStartSec }, { "endSec", o.loopEndSec } }) },
            }) },
        }));
    }

    return makeObject ({
        { "formatVersion", formatVersion },
        { "setlistId",     setlistId },
        { "name",          name },
        { "createdAt",     createdAt.toISO8601 (true) },
        { "updatedAt",     updatedAt.toISO8601 (true) },
        { "entries",       entryList },
    });
}

std::optional<Setlist> Setlist::fromVar (const juce::var& value)
{
    if (! value.isObject() || value["setlistId"].toString().isEmpty())
        return std::nullopt;

    Setlist s;
    s.setlistId = value["setlistId"].toString();
    s.name = value["name"].toString();
    s.createdAt = timeFromString (value["createdAt"].toString());
    s.updatedAt = timeFromString (value["updatedAt"].toString());

    if (const auto* list = value["entries"].getArray())
    {
        for (const auto& item : *list)
        {
            SetlistEntry entry;
            entry.entryId = item["entryId"].toString();
            entry.sessionId = item["sessionId"].toString();

            if (entry.entryId.isEmpty())   // 手で書き換えられたファイルでも、エントリを区別できるように
                entry.entryId = juce::Uuid().toString();

            const auto& overrides = item["overrides"];
            const auto& loop = overrides["loop"];
            entry.overrides.speed = doubleOr (overrides["speed"], 1.0);
            entry.overrides.loopEnabled = loop["enabled"].isVoid() ? false : (bool) loop["enabled"];
            entry.overrides.loopStartSec = doubleOr (loop["startSec"], 0.0);
            entry.overrides.loopEndSec = doubleOr (loop["endSec"], 0.0);

            s.entries.push_back (entry);
        }
    }

    return s;
}
