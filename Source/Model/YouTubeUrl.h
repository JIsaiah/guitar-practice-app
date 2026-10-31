#pragma once

#include <juce_core/juce_core.h>

// YouTube の URL から動画 ID (11文字) を取り出す。対応する形:
//   https://www.youtube.com/watch?v=ID   https://m.youtube.com/watch?v=ID&t=30s
//   https://youtu.be/ID?t=30             https://www.youtube.com/shorts/ID
//   https://www.youtube.com/embed/ID     https://music.youtube.com/watch?v=ID
// 取り出せなければ空文字
namespace YouTubeUrl
{
    inline bool isValidId (const juce::String& id)
    {
        return id.length() == 11 && id.containsOnly ("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_");
    }

    inline juce::String extractVideoId (const juce::String& urlText)
    {
        const auto url = urlText.trim();

        if (! (url.startsWithIgnoreCase ("http://") || url.startsWithIgnoreCase ("https://")))
            return {};

        juce::String id;

        if (url.containsIgnoreCase ("youtu.be/"))
            id = url.fromFirstOccurrenceOf ("youtu.be/", false, true);
        else if (url.containsIgnoreCase ("youtube.com/shorts/"))
            id = url.fromFirstOccurrenceOf ("/shorts/", false, true);
        else if (url.containsIgnoreCase ("youtube.com/embed/"))
            id = url.fromFirstOccurrenceOf ("/embed/", false, true);
        else if (url.containsIgnoreCase ("youtube.com/") && (url.contains ("?v=") || url.contains ("&v=")))
            id = url.fromFirstOccurrenceOf (url.contains ("?v=") ? "?v=" : "&v=", false, false);

        id = id.upToFirstOccurrenceOf ("?", false, false)
               .upToFirstOccurrenceOf ("&", false, false)
               .upToFirstOccurrenceOf ("#", false, false)
               .upToFirstOccurrenceOf ("/", false, false);

        return isValidId (id) ? id : juce::String();
    }

    // URL の t= (開始位置) を秒にする。"90", "90s", "1m30s" に対応。無ければ 0
    inline double extractStartSeconds (const juce::String& urlText)
    {
        auto query = urlText.fromFirstOccurrenceOf ("?", false, false);
        juce::String t;

        for (const auto& part : juce::StringArray::fromTokens (query.replaceCharacter ('#', '&'), "&", {}))
            if (part.startsWith ("t=") || part.startsWith ("start="))
                t = part.fromFirstOccurrenceOf ("=", false, false);

        if (t.isEmpty())
            return 0.0;

        double seconds = 0.0, number = 0.0;
        for (auto c : t)
        {
            if (juce::CharacterFunctions::isDigit (c))           number = number * 10.0 + (c - '0');
            else if (c == 'h')                                   { seconds += number * 3600.0; number = 0.0; }
            else if (c == 'm')                                   { seconds += number * 60.0; number = 0.0; }
            else if (c == 's')                                   { seconds += number; number = 0.0; }
        }
        return seconds + number;
    }
}
