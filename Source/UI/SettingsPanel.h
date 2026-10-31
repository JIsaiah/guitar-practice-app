#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "../AppSettings.h"
#include "../Theme/ThemeManager.h"

// 設定画面 (一覧の「設定」、練習画面の「⚙ 詳細」から開く)
//   上: 一般 (テーマ、戻る・進むの秒数、ホイール1目盛りで変わる量)
//   下: 詳細設定 (オーディオの入出力)
// 変更はその場で反映し、アプリの設定ファイルに保存する
class SettingsPanel final : public juce::Component,
                            private juce::ChangeListener
{
public:
    SettingsPanel (AppSettings& settings, ThemeManager& theme, juce::AudioDeviceManager& deviceManager);
    ~SettingsPanel() override;

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

    void setUpRow (Row& row, const juce::String& name, const juce::String& unit, int min, int max);

    AppSettings& settings;
    ThemeManager& theme;

    juce::Label generalHeading, advancedHeading, wheelHeading;
    juce::Label themeLabel;
    juce::ComboBox themeBox;
    Row skipRow, speedRow, bpmRow, volumeRow, offsetRow;

    juce::AudioDeviceSelectorComponent audioSelector;
};
