#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Audio/TapTempo.h"
#include "../Model/Session.h"
#include "../Web/ScoreViewer.h"

// 新しいセッションを作る画面 (設計書 4.1 の 2〜5)
// 音源 → タブ譜 (任意) → 音色 → BPM の順に決めて、練習を始める
// タブ譜を選ぶと、タブ譜に書かれているテンポ・アーティスト・チューニングを自動で入れる (設計書 #11)。
// タブ譜は、練習画面と同じ ScoreViewer (alphaTab) で読む。この画面では ScoreViewer を表示しない
class SessionSetupScreen final : public juce::Component
{
public:
    explicit SessionSetupScreen (ScoreViewer& scoreReader);

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
    void readScoreDetails();   // タブ譜を読み、テンポなどが分かったら入れる

    ScoreViewer& scoreReader;
    Session session;
    TapTempo tapTempo;
    std::unique_ptr<juce::FileChooser> fileChooser;

    juce::Label heading;
    juce::Label titleLabel, artistLabel, sourceLabel, scoreLabel, toneLabel, bpmLabel, tuningLabel;
    juce::Label sourceValue, scoreValue, bpmNote;
    juce::TextEditor titleEditor, artistEditor, tuningEditor;
    juce::TextButton sourceFileButton, youtubeButton, scoreFileButton, scoreClearButton;
    std::array<juce::TextButton, 2> toneButtons;   // 簡易な音色 (getTonePresets の数)
    juce::Slider bpmSlider;
    juce::TextButton tapButton;
    juce::TextButton cancelButton, startButton;
};
