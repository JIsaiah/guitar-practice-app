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

    // 選べる範囲
    static constexpr int minSkipSeconds = 1, maxSkipSeconds = 30;

private:
    void save();

    juce::PropertiesFile& file;
    int skipSeconds = 5;
    WheelSteps wheel;
    int sidePanelWidth = 400;
    bool scoreEqualBars = false;
};
