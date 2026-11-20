#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

// アプリのページ (タブ譜・YouTube) を表示する WebView。ページ自体は、アプリに同梱したページ (https://juce.backend/) から
// ほかへ移れないようにする (セキュリティ)。
//
// このページには、アプリとつながる通り道 (window.__JUCE__) がある。YouTube のページは、YouTube のスクリプトをネットから読むので、
// もしそれがページごとほかの場所へ移ると、移った先のページもこの通り道を使えてしまう。
// 移れるのは同梱のページの中だけにし、YouTube の動画の枠 (iframe) の中の移動は、ここでは止めない (WebView2 はページ自体の移動だけを知らせる)。
// 新しい窓は、JUCE がもともと開かない (newWindowAttemptingToLoad は何もしない)
class LockedWebBrowser final : public juce::WebBrowserComponent
{
public:
    using juce::WebBrowserComponent::WebBrowserComponent;

    static bool isAllowedPage (const juce::String& url)
    {
        return url.startsWith (getResourceProviderRoot()) || url == "about:blank";
    }

    bool pageAboutToLoad (const juce::String& url) override { return isAllowedPage (url); }
};
