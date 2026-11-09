#include "WebResources.h"
#include <BinaryData.h>

std::optional<WebResources::Resource> WebResources::findBundled (const juce::String& path)
{
    struct Entry { const char* path; const char* data; int size; const char* mimeType; };

    // パス → 埋め込んだデータ (CMakeLists.txt の juce_add_binary_data)
    static const Entry entries[] {
        { "/youtube-player.html",           BinaryData::youtubeplayer_html, BinaryData::youtubeplayer_htmlSize, "text/html" },
        { "/score-view.html",               BinaryData::scoreview_html,     BinaryData::scoreview_htmlSize,     "text/html" },
        { "/tokens.css",                    BinaryData::tokens_css,         BinaryData::tokens_cssSize,         "text/css" },
        { "/alphatab/alphaTab.min.js",      BinaryData::alphaTab_min_js,    BinaryData::alphaTab_min_jsSize,    "text/javascript" },
        { "/alphatab/font/Bravura.woff2",   BinaryData::Bravura_woff2,      BinaryData::Bravura_woff2Size,      "font/woff2" },
        { "/alphatab/font/Bravura.woff",    BinaryData::Bravura_woff,       BinaryData::Bravura_woffSize,       "font/woff" },
        // タブ譜の音 (MIDI) の音源。SONiVOX (Apache License 2.0。Resources/Web/alphatab/sonivox-LICENSE.txt)
        { "/alphatab/soundfont/sonivox.sf2", BinaryData::sonivox_sf2,       BinaryData::sonivox_sf2Size,        "application/octet-stream" },
    };

    const auto clean = path.upToFirstOccurrenceOf ("?", false, false);

    for (const auto& e : entries)
        if (clean == e.path)
            return fromMemory (e.data, (size_t) e.size, e.mimeType);

    return std::nullopt;
}
