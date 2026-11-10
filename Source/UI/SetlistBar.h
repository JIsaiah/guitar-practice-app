#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// セットリストから開いたときだけ、練習画面の下に出すメニュー (setlist-mode.md 5.1)
//   セットリスト名、「3 / 8」、全体の進み具合、次の曲名、「◀ 前の曲」「次の曲 ▶」「通し再生を止める」
class SetlistBar final : public juce::Component
{
public:
    struct Info
    {
        juce::String setlistName;
        int position = 0, count = 0;    // 今は何曲目か (1 から) と曲数
        juce::String nextTitle;         // 空なら最後の曲
        bool hasPrevious = false, hasNext = false;
        bool autoAdvance = true;        // 曲が終わったら次へ進むか (「通し再生を止める」で false)
    };

    SetlistBar();

    void setInfo (const Info& info);
    const Info& getInfo() const noexcept { return info; }
    void setProgress (double progress);            // 0〜1
    void setNotice (const juce::String& notice);   // 「ループ中のため自動で次に進みません」など

    std::function<void()> onPrevious, onNext;
    std::function<void (bool resume)> onAutoAdvanceToggled;   // 止める (false) ／再開 (true)

    static constexpr int preferredHeight = 80;     // 次の曲名を大きく出すため、64 から高くした
    static constexpr int headingRowHeight = 24;    // 中央の上の行 (「次の曲」と注意)。その下が曲名

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    Info info;
    double progress = 0.0;
    juce::String notice;
    juce::TextButton previousButton, nextButton, stopButton;
    juce::Rectangle<int> textArea, progressArea;
};
