#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include <juce_events/juce_events.h>

// アプリ全体の操作の設定 (設定画面の「一般」)。セッションではなく、アプリの設定ファイルに保存する
// 値が変わると sendChangeMessage() で知らせる (開いている練習画面がすぐに反映する)
class AppSettings final : public juce::ChangeBroadcaster
{
public:
    explicit AppSettings (juce::PropertiesFile& file);

    // 「◀◀」「▶▶」と ← → キーで移動する秒数
    int getSkipSeconds() const noexcept { return skipSeconds; }
    void setSkipSeconds (int seconds);

    // 再生ボタンの待ち時間 (秒)。「▶ 再生」を押してから、ギターに手を戻すまでの時間。0 ならすぐに始める
    int getPreStartDelaySeconds() const noexcept { return preStartDelaySeconds; }
    void setPreStartDelaySeconds (int seconds);
    static constexpr int maxPreStartDelaySeconds = 10;
    static constexpr int defaultPreStartDelaySeconds = 3;   // 【仮決め】

    // セットリストのカウントダウン (秒)。曲が終わって次の曲を始めるときと、「次の曲」「前の曲」「通し再生を再開」のとき
    // (setlist-mode.md 7.1)。作者の指示で、再生ボタンの待ち時間とは別にし、セットリストの中ではひとつにした
    int getSongGapSeconds() const noexcept { return songGapSeconds; }
    void setSongGapSeconds (int seconds);
    static constexpr int maxSongGapSeconds = 30;
    static constexpr int defaultSongGapSeconds = 3;

    // 一覧で開いていたセットリスト (練習画面から戻ったときと、次の起動のときに開いたままにする)
    juce::StringArray getExpandedSetlists() const;
    void setExpandedSetlists (const juce::StringArray& setlistIds);

    // ホイール1目盛りで変わる量 (設計書 4.3)
    struct WheelSteps
    {
        int speedPercent = 5;   // 速度 (%)
        int bpm = 1;            // BPM
        int volumePercent = 5;  // 音量 (%。ミキサーの値 1.0 = 100%)
        int offsetMs = 10;      // ずらし (ms)
    };
    const WheelSteps& getWheelSteps() const noexcept { return wheel; }
    void setWheelSteps (const WheelSteps& steps);

    // 練習画面の右パネル (動画・音色・ミキサー) の幅。境目のバーを引っ張って変える
    int getSidePanelWidth() const noexcept { return sidePanelWidth; }
    void setSidePanelWidth (int width);
    static constexpr int minSidePanelWidth = 340, maxSidePanelWidth = 760;

    // タブ譜の小節を同じ幅で並べるか (練習画面のトグル)
    bool getScoreEqualBars() const noexcept { return scoreEqualBars; }
    void setScoreEqualBars (bool equal);

    // 一覧画面で最後に開いていたタブ (0: セッション、1: セットリスト。setlist-mode.md 4.1)
    int getLibraryTab() const noexcept { return libraryTab; }
    void setLibraryTab (int tab);

    // 選べる範囲
    static constexpr int minSkipSeconds = 1, maxSkipSeconds = 30;

private:
    void save();

    juce::PropertiesFile& file;
    int skipSeconds = 5;
    int preStartDelaySeconds = defaultPreStartDelaySeconds;
    int songGapSeconds = defaultSongGapSeconds;
    WheelSteps wheel;
    int sidePanelWidth = 400;
    bool scoreEqualBars = false;
    int libraryTab = 0;
};
