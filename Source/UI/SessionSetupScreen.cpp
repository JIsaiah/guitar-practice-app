#include "SessionSetupScreen.h"
#include "../Model/YouTubeUrl.h"
#include "../Model/TonePresets.h"
#include "../Text.h"

namespace
{
    bool looksLikeYouTubeUrl (const juce::String& text)
    {
        // 動画 ID を取り出せる URL だけを受け付ける (再生時に ID が必要なため)
        return YouTubeUrl::extractVideoId (text).isNotEmpty();
    }
}

SessionSetupScreen::SessionSetupScreen()
{
    heading.setText ("新しいセッション"_jp, juce::dontSendNotification);
    heading.setFont (juce::FontOptions (24.0f, juce::Font::bold));
    addAndMakeVisible (heading);

    titleLabel.setText ("曲名"_jp, juce::dontSendNotification);
    sourceLabel.setText ("1. 音源"_jp, juce::dontSendNotification);
    scoreLabel.setText ("2. タブ譜 (任意)"_jp, juce::dontSendNotification);
    toneLabel.setText ("3. 音色"_jp, juce::dontSendNotification);
    bpmLabel.setText ("4. BPM"_jp, juce::dontSendNotification);
    for (auto* label : { &titleLabel, &sourceLabel, &scoreLabel, &toneLabel, &bpmLabel, &sourceValue, &scoreValue })
        addAndMakeVisible (label);

    titleEditor.setTextToShowWhenEmpty ("音源を選ぶと自動で入ります"_jp, juce::Colours::grey);
    titleEditor.onTextChange = [this] { session.title = titleEditor.getText().trim(); };
    addAndMakeVisible (titleEditor);

    sourceFileButton.setButtonText ("ファイルを選ぶ…"_jp);
    youtubeButton.setButtonText ("YouTube の URL を貼り付け"_jp);
    scoreFileButton.setButtonText ("ファイルを選ぶ…"_jp);
    scoreClearButton.setButtonText ("なし"_jp);
    sourceFileButton.onClick = [this] { chooseSourceFile(); };
    youtubeButton.onClick = [this] { pasteYouTubeUrl(); };
    scoreFileButton.onClick = [this] { chooseScoreFile(); };
    scoreClearButton.onClick = [this] { session.scorePath = {}; updateState(); };
    for (auto* button : { &sourceFileButton, &youtubeButton, &scoreFileButton, &scoreClearButton })
        addAndMakeVisible (button);

    const auto& presets = getTonePresets();
    for (size_t i = 0; i < toneButtons.size(); ++i)
    {
        auto& button = toneButtons[i];
        button.setButtonText (juce::String::fromUTF8 (presets[i].name));
        button.setClickingTogglesState (true);
        button.setRadioGroupId (1);
        button.onClick = [this, i] { selectTone ((int) i); };
        addAndMakeVisible (button);
    }

    bpmSlider.setRange (40.0, 300.0, 1.0);
    bpmSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    bpmSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 28);
    bpmSlider.onValueChange = [this]
    {
        session.bpm = bpmSlider.getValue();
        session.tempoOrigin = TempoOrigin::manual;
    };
    addAndMakeVisible (bpmSlider);

    tapButton.setButtonText ("TAP");
    tapButton.onClick = [this] { tap(); };
    addAndMakeVisible (tapButton);

    cancelButton.setButtonText ("キャンセル"_jp);
    startButton.setButtonText ("練習を始める"_jp);
    cancelButton.onClick = [this] { if (onCancel != nullptr) onCancel(); };
    startButton.onClick = [this] { if (onStart != nullptr) onStart (session); };
    addAndMakeVisible (cancelButton);
    addAndMakeVisible (startButton);

    selectTone (0);
    bpmSlider.setValue (session.bpm, juce::dontSendNotification);
    updateState();
}

void SessionSetupScreen::chooseSourceFile()
{
    fileChooser = std::make_unique<juce::FileChooser> ("音源ファイルを選ぶ"_jp, juce::File(),
                                                       "*.mp3;*.wav;*.flac;*.ogg;*.aif;*.aiff");

    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& chooser)
                              {
                                  const auto file = chooser.getResult();
                                  if (file == juce::File())
                                      return;

                                  session.sourceType = SourceType::local;
                                  session.sourcePath = file.getFullPathName();
                                  session.youtubeUrl = {};

                                  if (titleEditor.isEmpty())
                                      titleEditor.setText (file.getFileNameWithoutExtension());

                                  updateState();
                              });
}

void SessionSetupScreen::pasteYouTubeUrl()
{
    // クリップボードは、このボタンを押したときだけ読む (設計書 4.1 の 2)
    const auto text = juce::SystemClipboard::getTextFromClipboard().trim();

    if (! looksLikeYouTubeUrl (text))
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                "YouTube の URL ではありません"_jp,
                                                "YouTube の動画ページの URL をコピーしてから、もう一度押してください。"_jp);
        return;
    }

    session.sourceType = SourceType::youtube;
    session.youtubeUrl = text;
    session.sourcePath = {};
    updateState();
}

void SessionSetupScreen::chooseScoreFile()
{
    fileChooser = std::make_unique<juce::FileChooser> ("タブ譜ファイルを選ぶ"_jp, juce::File(),
                                                       "*.gp;*.gpx;*.gp5;*.gp4;*.gp3;*.musicxml;*.xml");

    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& chooser)
                              {
                                  const auto file = chooser.getResult();
                                  if (file == juce::File())
                                      return;

                                  session.scorePath = file.getFullPathName();
                                  updateState();
                              });
}

void SessionSetupScreen::selectTone (int index)
{
    toneButtons[(size_t) index].setToggleState (true, juce::dontSendNotification);
    session.tonePresetId = getTonePresets()[(size_t) index].id;
}

void SessionSetupScreen::tap()
{
    if (const auto bpm = tapTempo.tap (juce::Time::getMillisecondCounterHiRes()))
    {
        bpmSlider.setValue (std::round (*bpm), juce::dontSendNotification);
        session.bpm = bpmSlider.getValue();
        session.tempoOrigin = TempoOrigin::tap;
    }
}

void SessionSetupScreen::updateState()
{
    switch (session.sourceType)
    {
        case SourceType::local:   sourceValue.setText (session.sourcePath, juce::dontSendNotification); break;
        case SourceType::youtube: sourceValue.setText ("YouTube: "_jp + session.youtubeUrl, juce::dontSendNotification); break;
        case SourceType::none:    sourceValue.setText ("(未選択)"_jp, juce::dontSendNotification); break;
    }

    scoreValue.setText (session.scorePath.isEmpty() ? "(なし)"_jp : session.scorePath, juce::dontSendNotification);
    startButton.setEnabled (session.sourceType != SourceType::none);
}

void SessionSetupScreen::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
}

void SessionSetupScreen::resized()
{
    auto area = getLocalBounds().reduced (24);
    heading.setBounds (area.removeFromTop (44));
    area.removeFromTop (16);

    constexpr int labelWidth = 160;
    constexpr int rowHeight = 36;
    constexpr int gap = 14;

    const auto nextRow = [&] (juce::Label& label)
    {
        auto row = area.removeFromTop (rowHeight);
        label.setBounds (row.removeFromLeft (labelWidth));
        area.removeFromTop (gap);
        return row;
    };

    titleEditor.setBounds (nextRow (titleLabel).removeFromLeft (480));

    auto sourceRow = nextRow (sourceLabel);
    sourceFileButton.setBounds (sourceRow.removeFromLeft (160));
    sourceRow.removeFromLeft (8);
    youtubeButton.setBounds (sourceRow.removeFromLeft (240));
    sourceRow.removeFromLeft (12);
    sourceValue.setBounds (sourceRow);

    auto scoreRow = nextRow (scoreLabel);
    scoreFileButton.setBounds (scoreRow.removeFromLeft (160));
    scoreRow.removeFromLeft (8);
    scoreClearButton.setBounds (scoreRow.removeFromLeft (80));
    scoreRow.removeFromLeft (12);
    scoreValue.setBounds (scoreRow);

    auto toneRow = nextRow (toneLabel);
    for (auto& button : toneButtons)
    {
        button.setBounds (toneRow.removeFromLeft (120));
        toneRow.removeFromLeft (8);
    }

    auto bpmRow = nextRow (bpmLabel);
    bpmSlider.setBounds (bpmRow.removeFromLeft (360));
    bpmRow.removeFromLeft (8);
    tapButton.setBounds (bpmRow.removeFromLeft (100));

    auto buttons = getLocalBounds().reduced (24).removeFromBottom (48);
    startButton.setBounds (buttons.removeFromRight (200));
    buttons.removeFromRight (12);
    cancelButton.setBounds (buttons.removeFromRight (140));
}
