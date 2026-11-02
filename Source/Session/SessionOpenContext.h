#pragma once

#include <juce_core/juce_core.h>

// セッション画面がどこから開かれたか (setlist-mode.md 3.5)
// 「次の曲」はセッション自体ではなく、これで決まる
struct SessionOpenContext
{
    enum class Kind { Standalone, Setlist };
    Kind kind = Kind::Standalone;
    juce::String setlistId; // Setlist のときのみ
    juce::String entryId;   // Setlist のときのみ

    bool isSetlist() const noexcept { return kind == Kind::Setlist; }

    static SessionOpenContext forSetlist (const juce::String& setlistId, const juce::String& entryId)
    {
        return { Kind::Setlist, setlistId, entryId };
    }
};
