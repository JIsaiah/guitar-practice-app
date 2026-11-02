#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// 再生前のカウントダウンの大きな数字 (setlist-mode.md 7.2)。すぐに始めるのは再生ボタンの2回押し (クリックは受けない)
// タブ譜・動画 (WebView) の上には JUCE の部品を描けないので、そのときはページの中に出す
// (ScoreViewer / YouTubePlayer::showCountdown)。これはタブ譜も動画もメインの枠に無いときに使う
class CountdownOverlay final : public juce::Component
{
public:
    CountdownOverlay();

    void setCountdown (int number, const juce::String& caption);

    void paint (juce::Graphics& g) override;

private:
    int number = 0;
    juce::String caption;
};
