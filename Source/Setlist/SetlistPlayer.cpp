#include "SetlistPlayer.h"

SetlistPlayer::SetlistPlayer (Setlist s, SessionExists e, const juce::String& entryId)
    : setlist (std::move (s)), exists (std::move (e)), currentEntryId (entryId)
{
}

bool SetlistPlayer::isPlayable (int index) const
{
    return juce::isPositiveAndBelow (index, getNumEntries())
        && (exists == nullptr || exists (setlist.entries[(size_t) index].sessionId));
}

int SetlistPlayer::getCurrentIndex() const
{
    return setlist.indexOf (currentEntryId);
}

const SetlistEntry* SetlistPlayer::getCurrent() const
{
    return setlist.findEntry (currentEntryId);
}

int SetlistPlayer::findNext() const
{
    const auto current = getCurrentIndex();
    if (current < 0)
        return -1;

    return findPlayableFrom (current + 1);
}

int SetlistPlayer::findPrevious() const
{
    for (int i = getCurrentIndex() - 1; i >= 0; --i)
        if (isPlayable (i))
            return i;

    return -1;
}

const SetlistEntry* SetlistPlayer::getNext() const
{
    const auto next = findNext();
    return next >= 0 ? &setlist.entries[(size_t) next] : nullptr;
}

bool SetlistPlayer::goNext()
{
    const auto next = findNext();
    if (next < 0)
        return false;

    currentEntryId = setlist.entries[(size_t) next].entryId;
    return true;
}

bool SetlistPlayer::goPrevious()
{
    const auto previous = findPrevious();
    if (previous < 0)
        return false;

    currentEntryId = setlist.entries[(size_t) previous].entryId;
    return true;
}

int SetlistPlayer::findPlayableFrom (int index) const
{
    for (int i = juce::jmax (0, index); i < getNumEntries(); ++i)
        if (isPlayable (i))
            return i;

    return -1;
}

double SetlistPlayer::getProgress (double fractionOfCurrentSong) const
{
    const auto current = getCurrentIndex();
    if (current < 0 || getNumEntries() == 0)
        return 0.0;

    return juce::jlimit (0.0, 1.0, (current + juce::jlimit (0.0, 1.0, fractionOfCurrentSong)) / getNumEntries());
}
