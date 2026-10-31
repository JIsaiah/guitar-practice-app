#include "SettingsPanel.h"
#include "../Text.h"

SettingsPanel::SettingsPanel (AppSettings& s, ThemeManager& t, juce::AudioDeviceManager& deviceManager)
    : settings (s), theme (t),
      audioSelector (deviceManager,
                     0, 2,     // 入力チャンネル数 (最小, 最大)
                     2, 2,     // 出力チャンネル数
                     false, false,
                     true, false)
{
    for (auto* heading : { &generalHeading, &advancedHeading })
    {
        heading->setFont (juce::FontOptions (20.0f, juce::Font::bold));
        addAndMakeVisible (heading);
    }
    generalHeading.setText ("一般"_jp, juce::dontSendNotification);
    advancedHeading.setText ("詳細設定 (オーディオの入出力)"_jp, juce::dontSendNotification);

    wheelHeading.setText ("マウスホイール1目盛りで変わる量"_jp, juce::dontSendNotification);
    wheelHeading.setColour (juce::Label::textColourId, findColour (ThemeColourIds::textMuted));
    addAndMakeVisible (wheelHeading);

    // テーマ
    themeLabel.setText ("テーマ"_jp, juce::dontSendNotification);
    addAndMakeVisible (themeLabel);
    for (auto mode : { ColourTheme::System, ColourTheme::Light, ColourTheme::Dark })
        themeBox.addItem (ThemeManager::getColourThemeName (mode), (int) mode + 1);
    themeBox.onChange = [this] { theme.setColourTheme ((ColourTheme) (themeBox.getSelectedId() - 1)); };
    addAndMakeVisible (themeBox);

    setUpRow (skipRow, "戻る・進むの幅"_jp, "秒"_jp, AppSettings::minSkipSeconds, AppSettings::maxSkipSeconds);
    setUpRow (speedRow, "速度"_jp, "%", 1, 50);
    setUpRow (bpmRow, "BPM", {}, 1, 20);
    setUpRow (volumeRow, "音量"_jp, "%", 1, 50);
    setUpRow (offsetRow, "ずらし"_jp, "ms", 1, 500);

    skipRow.value.onValueChange = [this] { settings.setSkipSeconds ((int) skipRow.value.getValue()); };
    for (auto* row : { &speedRow, &bpmRow, &volumeRow, &offsetRow })
        row->value.onValueChange = [this] { storeWheelSteps(); };

    addAndMakeVisible (audioSelector);

    updateFromSettings();
    theme.addChangeListener (this);
    settings.addChangeListener (this);

    setSize (580, 820);
}

SettingsPanel::~SettingsPanel()
{
    theme.removeChangeListener (this);
    settings.removeChangeListener (this);
}

void SettingsPanel::setUpRow (Row& row, const juce::String& name, const juce::String& unit, int min, int max)
{
    row.label.setText (name, juce::dontSendNotification);
    row.unit.setText (unit, juce::dontSendNotification);
    row.value.setSliderStyle (juce::Slider::IncDecButtons);
    row.value.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 60, 26);
    row.value.setRange (min, max, 1.0);
    addAndMakeVisible (row.label);
    addAndMakeVisible (row.value);
    addAndMakeVisible (row.unit);
}

void SettingsPanel::updateFromSettings()
{
    themeBox.setSelectedId ((int) theme.getColourTheme() + 1, juce::dontSendNotification);
    skipRow.value.setValue (settings.getSkipSeconds(), juce::dontSendNotification);

    const auto& steps = settings.getWheelSteps();
    speedRow.value.setValue (steps.speedPercent, juce::dontSendNotification);
    bpmRow.value.setValue (steps.bpm, juce::dontSendNotification);
    volumeRow.value.setValue (steps.volumePercent, juce::dontSendNotification);
    offsetRow.value.setValue (steps.offsetMs, juce::dontSendNotification);
}

void SettingsPanel::storeWheelSteps()
{
    AppSettings::WheelSteps steps;
    steps.speedPercent = (int) speedRow.value.getValue();
    steps.bpm = (int) bpmRow.value.getValue();
    steps.volumePercent = (int) volumeRow.value.getValue();
    steps.offsetMs = (int) offsetRow.value.getValue();
    settings.setWheelSteps (steps);
}

void SettingsPanel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    updateFromSettings();
    wheelHeading.setColour (juce::Label::textColourId, findColour (ThemeColourIds::textMuted));
    repaint();
}

void SettingsPanel::paint (juce::Graphics& g)
{
    g.fillAll (findColour (juce::ResizableWindow::backgroundColourId));

    // 「一般」と「詳細設定」の境目
    g.setColour (findColour (ThemeColourIds::border));
    g.fillRect (advancedHeading.getBounds().withHeight (1).translated (0, -10));
}

void SettingsPanel::resized()
{
    auto area = getLocalBounds().reduced (20, 16);

    const auto row = [&area] (juce::Component& label, juce::Component& control, juce::Component* unit)
    {
        auto r = area.removeFromTop (32);
        label.setBounds (r.removeFromLeft (200));
        control.setBounds (r.removeFromLeft (150));
        if (unit != nullptr)
            unit->setBounds (r.removeFromLeft (60));
        area.removeFromTop (6);
    };

    generalHeading.setBounds (area.removeFromTop (30));
    area.removeFromTop (6);
    row (themeLabel, themeBox, nullptr);
    row (skipRow.label, skipRow.value, &skipRow.unit);

    area.removeFromTop (4);
    wheelHeading.setBounds (area.removeFromTop (24));
    for (auto* r : { &speedRow, &bpmRow, &volumeRow, &offsetRow })
        row (r->label, r->value, &r->unit);

    area.removeFromTop (22);
    advancedHeading.setBounds (area.removeFromTop (30));
    area.removeFromTop (4);
    audioSelector.setBounds (area);
}
