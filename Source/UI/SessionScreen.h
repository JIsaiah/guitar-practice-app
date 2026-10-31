#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Audio/AudioEngine.h"
#include "../Audio/TapTempo.h"
#include "../Model/SessionStore.h"
#include "Commands.h"
#include "PracticeViews.h"
#include "TunerPanel.h"

// 練習画面 (設計書 4.2)
class SessionScreen final : public juce::Component,
                            private juce::Timer
{
public:
    SessionScreen (AudioEngine& engine, SessionStore& store, Session session);
    ~SessionScreen() override;

    std::function<void()> onBackToList;
    std::function<void()> onOpenSettings;

    void perform (Command command);

    void paint (juce::Graphics& g) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress& key) override;

private:
    void timerCallback() override;

    void applySessionToEngine();
    void loadSource();
    bool isLocalSource() const noexcept { return session.sourceType == SourceType::local; }
    bool isDirty() const;
    bool save();
    void requestBackToList();
    void setStatus (const juce::String& text);

    void togglePlay();
    void skip (double seconds);
    void tap();
    void setLoopPoint (bool isStart);
    void toggleLoop();
    void toggleMetronome();
    void toggleTuner();
    void selectTone (int index);
    void updateLoop();

    AudioEngine& engine;
    SessionStore& store;
    Session session;
    juce::String savedSnapshot;

    TapTempo tapTempo;
    bool sourceReady = false;
    bool metronomeOn = false;

    // 上部
    juce::TextButton backButton, saveButton, settingsButton;
    juce::Label titleLabel, statusLabel;

    // 左: タブ譜と再生位置
    ScoreView scoreView;
    TimelineView timeline;

    // 右: 動画、音色、ミキサー
    PlaceholderView videoView;
    std::array<juce::TextButton, 3> toneButtons;
    juce::Label sourceGainLabel, guitarGainLabel, metronomeGainLabel, offsetLabel;
    juce::Slider sourceGainSlider, guitarGainSlider, metronomeGainSlider, offsetSlider;

    // 下部: 再生操作
    juce::TextButton rewindButton, playButton, forwardButton;
    juce::Label speedLabel, bpmLabel;
    juce::Slider speedSlider, bpmSlider;
    juce::TextButton tapButton, metronomeButton, loopStartButton, loopEndButton, loopButton, tunerButton;

    std::unique_ptr<TunerPanel> tunerPanel;
};
