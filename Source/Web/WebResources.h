#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

// WebView (https://juce.backend/) に、アプリに埋め込んだファイルを返す (ネットには出ない)
// YouTubePlayer と ScoreViewer の両方が使う
namespace WebResources
{
    using Resource = juce::WebBrowserComponent::Resource;

    inline Resource fromMemory (const void* data, size_t size, const char* mimeType)
    {
        Resource resource;
        resource.data.resize (size);
        std::memcpy (resource.data.data(), data, size);
        resource.mimeType = mimeType;
        return resource;
    }

    // 埋め込んだファイルを、パスで探す。無ければ nullopt
    std::optional<Resource> findBundled (const juce::String& path);
}
