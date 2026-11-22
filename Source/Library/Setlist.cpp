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
        entry.overrides.sourceStartSec = source->sourceStartMs / 1000.0;
        entry.overrides.sourceEndSec = source->sourceEndMs / 1000.0;
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
                { "range", makeObject ({ { "startSec", o.sourceStartSec }, { "endSec", o.sourceEndSec } }) },
            }) },
        }));
    }

    return makeObject ({
        { "formatVersion", formatVersion },
        { "setlistId",     setlistId },
        { "name",          name },
        { "createdAt",     createdAt.toISO8601 (true) },
        { "updatedAt",     updatedAt.toISO8601 (true) },
        { "liveDate",      liveDate },
        { "finished",      finished },
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
    s.liveDate = parseLiveDate (value["liveDate"].toString());   // 手で書き換えられても、日付の形のものだけ
    s.finished = value["finished"].isVoid() ? false : (bool) value["finished"];

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
            entry.overrides.sourceStartSec = juce::jmax (0.0, doubleOr (overrides["range"]["startSec"], 0.0));
            entry.overrides.sourceEndSec = juce::jmax (0.0, doubleOr (overrides["range"]["endSec"], 0.0));

            s.entries.push_back (entry);
        }
    }

    return s;
}

juce::String Setlist::formatLiveDate (const juce::String& isoDate)
{
    return isoDate.replaceCharacter ('-', '/');
}

juce::String Setlist::parseLiveDate (const juce::String& text, int defaultYear)
{
    // 「2026/11/21」「2026-11-21」「2026.11.21」と、「11/21」(年は defaultYear。0 なら今年) を受け付ける。月・日は1桁でもよい
    auto parts = juce::StringArray::fromTokens (text.trim(), "/-.", "");
    if (parts.size() == 2)
        parts.insert (0, juce::String (defaultYear > 0 ? defaultYear : juce::Time::getCurrentTime().getYear()));
    if (parts.size() != 3 || ! parts[0].containsOnly ("0123456789") || ! parts[1].containsOnly ("0123456789")
        || ! parts[2].containsOnly ("0123456789") || parts[0].length() != 4)
        return {};

    const auto year = parts[0].getIntValue(), month = parts[1].getIntValue(), day = parts[2].getIntValue();
    if (year < 1900 || year > 2999 || month < 1 || month > 12 || day < 1)
        return {};

    static const int daysInMonth[] = { 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    const bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    if (day > daysInMonth[month - 1] || (month == 2 && day == 29 && ! leap))
        return {};

    return juce::String (year) + "-" + juce::String (month).paddedLeft ('0', 2) + "-" + juce::String (day).paddedLeft ('0', 2);
}
