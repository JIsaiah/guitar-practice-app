#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Audio/AudioEngine.h"
#include "../Audio/TapTempo.h"
#include "../AppSettings.h"
#include "../Model/SessionStore.h"
#include "../Theme/MetalLogo/FixedTextButton.h"
#include "../Web/ScoreViewer.h"
#include "../Web/YouTubePlayer.h"
#include "Commands.h"
#include "PracticeViews.h"
#include "TunerPanel.h"

// 練習画面 (設計書 4.2)
//   左: タブ譜 (alphaTab) と再生位置バー     右: 音源 (動画)・音色・ミキサー・ずらし (隠せる・幅を変えられる)
//   下: 再生操作
class SessionScreen final : public juce::Component,
                            private juce::Timer,
                            private juce::ChangeListener
{
public:
    SessionScreen (AudioEngine& engine, SessionStore& store, ThemeManager& theme, AppSettings& settings,
                   YouTubePlayer& youtube, ScoreViewer& scoreViewer, Session session);
    ~SessionScreen() override;

    std::function<void()> onBackToList;
    std::function<void()> onOpenSettings;
    std::function<void()> onVideoReady;   // YouTube の動画を再生できるようになったとき (時間の計測用)

    void perform (Command command);

    void paint (juce::Graphics& g) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress& key) override;
    void lookAndFeelChanged() override;

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;   // 設定 (AppSettings) が変わったとき

    //==== 音源 ====
    bool isLocalSource() const noexcept { return session.sourceType == SourceType::local; }
    bool isYouTube() const noexcept { return session.sourceType == SourceType::youtube; }
    bool hasSource() const noexcept { return session.sourceType != SourceType::none; }

    // 再生位置などは、音源の種類によって取り出し元が違う (ローカル音源: AudioEngine、YouTube: YouTubePlayer)
    double getPosition() const;
    double getLength() const;
    bool isTransportPlaying() const;
    void seekTo (double seconds);
    void setTransportEnabled (bool enabled);

    void applySessionToEngine();
    void loadSource();
    void setUpYouTube();
    void handleYouTubeState();
    void changeYouTubeUrl (const juce::String& newUrl);
    void requestRemoveSource();
    void removeSource();
    void chooseSourceFile();
    void pasteYouTubeUrl();
    void updateSourceUi();

    //==== タブ譜 ====
    void loadScore();
    void chooseScoreFile();
    void removeScore();
    void updateScoreUi();

    //==== 保存・画面 ====
    bool isDirty() const;
    bool save();
    void requestBackToList();
    void setStatus (const juce::String& text);
    void applyAppSettings();
    void toggleSidePanel();
    void placeWebViews();

    //==== 操作 ====
    void togglePlay();
    void skip (double seconds);
    void tap();
    void setLoopPoint (bool isStart);
    void toggleLoop();
    void toggleMetronome();
    void toggleMetronomeSolo();
    void toggleTuner();
    void selectTone (int index);
    void updateLoop();
    void changeOffset (int deltaMs);

    AudioEngine& engine;
    SessionStore& store;
    ThemeManager& theme;
    YouTubePlayer& youtube;
    ScoreViewer& scoreViewer;
    AppSettings& settings;
    Session session;
    juce::String savedSnapshot;

    TapTempo tapTempo;
    bool sourceReady = false;
    double requestedRate = 1.0, rateRequestedAtMs = 0.0;   // YouTube に頼んだ速度 (YouTube 側での変更を見分けるため)
    bool metronomeOn = false, wasPlaying = false;
    bool sidePanelVisible = true;
    int sidePanelWidthAtDragStart = 0;
    std::unique_ptr<juce::FileChooser> fileChooser;

    // BPM のスライダーをダブルクリックしたときに戻す値 (この曲に保存されている値)
    double savedBpm = 120.0;
    TempoOrigin savedTempoOrigin = TempoOrigin::manual;

    // 上部
    FixedTextButton backButton, saveButton, settingsButton;
    juce::Label titleLabel, statusLabel;

    // 左: タブ譜と再生位置
    juce::Label scoreNameLabel, positionLabel;
    juce::TextButton scoreChooseButton, scoreRemoveButton;
    ScoreView scoreView;
    TimelineView timeline;

    // 境目と、右のパネル
    SplitterBar splitter;
    juce::TextButton sidePanelButton;   // 右のパネルを隠す・出すタブ
    std::vector<juce::Rectangle<int>> sideCards;   // 右のパネルの区切り (paint で面を描く)

    SectionHeading sourceHeading { "音源"_jp }, toneHeading { "音色"_jp }, mixerHeading { "ミキサー (音量)"_jp },
                   offsetHeading { "ずらし (音源とタブ譜・メトロノームの位置合わせ)"_jp };
    PlaceholderView videoView;
    juce::Label sourceInfoLabel;        // 動画の真下: YouTube の URL (ダブルクリックで変更) か、音源のファイル名
    TrashButton sourceRemoveButton;     // 音源を外す
    juce::TextButton chooseFileButton, pasteUrlButton;   // 音源がないとき
    std::array<juce::TextButton, 3> toneButtons;
    juce::Label sourceGainLabel, guitarGainLabel, metronomeGainLabel;
    WheelSlider sourceGainSlider, guitarGainSlider, metronomeGainSlider;
    juce::TextButton offsetDownButton, offsetUpButton;
    juce::Label offsetValueLabel;

    // 下部: 再生操作
    juce::TextButton rewindButton;
    FixedTextButton playButton;
    juce::TextButton forwardButton;
    juce::Label speedLabel, bpmLabel;
    WheelSlider speedSlider, bpmSlider;
    FixedTextButton tapButton, metronomeButton;
    juce::TextButton metronomeSoloButton;
    juce::TextButton loopStartButton, loopEndButton;
    FixedTextButton loopButton, tunerButton;

    std::unique_ptr<TunerPanel> tunerPanel;
};
