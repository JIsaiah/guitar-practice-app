#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Audio/TapTempo.h"
#include "../Model/Session.h"

// 新しいセッションを作る画面 (設計書 4.1 の 2〜5)
// 音源 → タブ譜 (任意) → 音色 → BPM の順に決めて、練習を始める
class SessionSetupScreen final : public juce::Component
{
public:
    SessionSetupScreen();

    std::function<void()> onCancel;
    std::function<void (Session)> onStart;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    void chooseSourceFile();
    void pasteYouTubeUrl();
    void chooseScoreFile();
    void selectTone (int index);
    void tap();
    void updateState();

    Session session;
    TapTempo tapTempo;
    std::unique_ptr<juce::FileChooser> fileChooser;

    juce::Label heading;
    juce::Label titleLabel, sourceLabel, scoreLabel, toneLabel, bpmLabel;
    juce::Label sourceValue, scoreValue;
    juce::TextEditor titleEditor;
    juce::TextButton sourceFileButton, youtubeButton, scoreFileButton, scoreClearButton;
    std::array<juce::TextButton, 3> toneButtons;
    juce::Slider bpmSlider;
    juce::TextButton tapButton;
    juce::TextButton cancelButton, startButton;
};
