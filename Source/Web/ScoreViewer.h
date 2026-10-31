#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "../Theme/Palette.h"

// タブ譜の表示 (設計書 #5、PoC-3)
// - WebView2 の中で alphaTab (同梱) を動かし、タブ譜を描く
// - 再生位置 (秒) と BPM・ずらしから、今の小節と小節の中の位置を求めて、カーソルを動かす
//   (設計書 3.1 の「開始位置のずらし + 一定テンポ」。拍子はタブ譜から読む)
// - YouTubePlayer と同じく、MainComponent の子のまま動かさず、練習画面のタブ譜の枠に重ねる
//
// ※ 設計書 v0.7 は「WebView は1つ」。今は練習画面がネイティブなので、YouTube 用とタブ譜用の2つを使う。
//   練習画面全体を HTML にするときに1つにまとめる
class ScoreViewer final : public juce::Component
{
public:
    ScoreViewer();
    ~ScoreViewer() override;

    void prepare();
    void placeOver (juce::Component& target);
    void hide();

    // タブ譜のファイルを読み込む。空の File なら消す
    void load (const juce::File& file, int trackIndex);
    void setPosition (double seconds, double bpm, double offsetSeconds);
    void setColours (const Palette& palette);

    bool hasScore() const noexcept { return scoreFile.existsAsFile(); }
    double getScoreTempo() const noexcept { return scoreTempo; }

    std::function<void (double tempo)> onLoaded;   // 読み込めたとき (タブ譜のテンポ)
    std::function<void (const juce::String& message)> onError;

    void resized() override;

private:
    void handleEvent (const juce::var& event);
    void send (const juce::var& command);
    std::optional<juce::WebBrowserComponent::Resource> provide (const juce::String& path);

    std::unique_ptr<juce::WebBrowserComponent> browser;
    bool pageReady = false, rendered = false;
    std::vector<juce::var> pending;   // ページの準備ができる前に来た命令

    juce::CriticalSection fileLock;   // 読み込みの要求は、別のスレッドから来ることがあるため
    juce::File scoreFile;
    int loadCount = 0;
    double scoreTempo = 0.0;
    double lastSentSeconds = -1.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ScoreViewer)
};
