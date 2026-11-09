#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "../Theme/Palette.h"
#include "../Model/ScoreParts.h"
#include "../Model/MusicInfo.h"

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
    // 再生位置。rate と playing も送り、ページの中で次の報告までの間を補って、針をなめらかに動かす
    void setPosition (double seconds, double bpm, double offsetSeconds, double rate, bool playing);
    void setColours (const Palette& palette);
    void setEqualBarWidths (bool equal);   // 小節を同じ幅で並べる

    // 表示するトラックを変える (リード・バッキングの切り替え)。ファイルは読み直さない
    void showTrack (int trackIndex);

    // 読み込んだタブ譜のトラック (名前と、ギターか)
    const std::vector<ScoreParts::Track>& getTracks() const noexcept { return tracks; }

    // 再生前のカウントダウンを、タブ譜の上に大きく出す。number が 0 なら消す (setlist-mode.md 7.2)
    void showCountdown (int number, const juce::String& caption = {});

    bool hasScore() const noexcept { return scoreFile.existsAsFile(); }
    double getScoreTempo() const noexcept { return scoreTempo; }

    // 各小節が曲の頭から何拍目 (4分音符の数) で始まるか。タブ譜の拍子から求める。タブ譜がなければ空
    const std::vector<double>& getBarStartBeats() const noexcept { return barStartBeats; }
    // 各小節の拍子 (getBarStartBeats() と同じ数)
    const std::vector<MusicInfo::TimeSignature>& getBarSignatures() const noexcept { return barSignatures; }

    // タブ譜に書かれている曲の情報 (読み込めたあと)。無ければ空
    const juce::String& getScoreTitle() const noexcept { return scoreTitle; }
    const juce::String& getScoreArtist() const noexcept { return scoreArtist; }
    const juce::String& getScoreTuning() const noexcept { return scoreTuning; }   // 例: "Drop D"

    std::function<void (double tempo)> onLoaded;   // 読み込めたとき (タブ譜のテンポ)
    std::function<void (const juce::String& message)> onError;
    std::function<void (double barStartBeats)> onBarClicked;   // 小節をクリックした (その小節が曲の頭から何拍目で始まるか)
    std::function<void (const juce::String& key)> onKey;      // タブ譜の上で押されたキー ("Space"、"ArrowLeft" など)

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
    std::vector<double> barStartBeats;
    std::vector<MusicInfo::TimeSignature> barSignatures;
    juce::String scoreTitle, scoreArtist, scoreTuning;
    std::vector<ScoreParts::Track> tracks;
    double lastSentSeconds = -1.0, lastSentBpm = 0.0, lastSentOffset = 0.0, lastSentRate = 1.0, lastSentAtMs = 0.0;
    bool lastSentPlaying = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ScoreViewer)
};
