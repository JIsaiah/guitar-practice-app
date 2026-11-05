#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "../AppSettings.h"
#include "../Theme/ThemeManager.h"

// 設定画面 (一覧の「⚙ 設定」、練習画面の「⚙ 詳細」から開く)
//   一般: テーマ、文字の大きさ、戻る・進むの秒数、ホイール1目盛りで変わる量
//   デスメタルモード: オン・オフ、ロゴ調整ウィンドウ
//   詳細設定: 色 (アクセントなどを HEX で。背景と文字の色は変えない)、オーディオの入出力
// 変更はその場で反映し、アプリの設定ファイルに保存する
class SettingsPanel final : public juce::Component,
                            private juce::ChangeListener
{
public:
    SettingsPanel (AppSettings& settings, ThemeManager& theme, juce::AudioDeviceManager& deviceManager);
    ~SettingsPanel() override;

    std::function<void()> onOpenLogoTuner;
    std::function<void()> onOpenDebugLog;   // 「動作ログ・クラッシュの記録を見る…」

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void updateFromSettings();
    void storeWheelSteps();

    struct Row
    {
        juce::Label label;
        juce::Slider value;
        juce::Label unit;
    };

    // 色の見本 (四角)
    struct Swatch final : public juce::Component
    {
        juce::Colour colour;
        void paint (juce::Graphics& g) override;
    };

    // HEX で色を変える行: 名前、入力欄、見本、「既定に戻す」
    struct ColourRow
    {
        juce::Label label;
        juce::TextEditor hex;
        Swatch swatch;
        juce::TextButton reset;
        std::function<std::optional<juce::Colour>()> getCustom;
        std::function<juce::Colour()> getCurrent;
        std::function<void (std::optional<juce::Colour>)> setCustom;
    };

    void setUpRow (Row& row, const juce::String& name, const juce::String& unit, int min, int max);
    void setUpColourRow (ColourRow& row, const juce::String& name);
    void applyHex (ColourRow& row);
    void updateColourRow (ColourRow& row);

    AppSettings& settings;
    ThemeManager& theme;

    juce::Label generalHeading, deathMetalHeading, advancedHeading, wheelHeading, colourHeading, colourNote, colourError, audioHeading;
    juce::Label themeLabel, textScaleLabel;
    juce::ComboBox themeBox, textScaleBox;
    Row skipRow, preStartDelayRow, songGapRow, speedRow, volumeRow, offsetRow;
    Row guitarMasterRow;   // ギターの総音量 (横のスライダー)   // BPM のホイールは常に 1 (作者の指示で設定から外した)

    juce::ToggleButton deathMetalToggle;
    juce::TextButton logoTunerButton;
    juce::TextButton debugLogButton;

    ColourRow accentRow, goodRow;

    juce::AudioDeviceSelectorComponent audioSelector;
    std::vector<int> sectionLines;   // 区切り線の y 座標 (paint で描く)
};
