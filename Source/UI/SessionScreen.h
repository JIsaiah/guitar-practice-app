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
#include "ToneDetailPanel.h"

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
    void skipBars (int direction);   // -1: 前の小節の頭、+1: 次の小節の頭
    bool isBarMode() const;          // タブ譜モード (タブ譜を大きく表示している) なら、戻る・進むは小節単位
    void updateSkipButtons();
    void tap();
    void setLoopPoint (bool isStart);
    void toggleLoop();
    void toggleMetronome();
    void toggleMetronomeSolo();
    void toggleTuner();
    void selectTone (int index, bool fromUser);   // index: 0〜2 が簡易な音色、3 が NAM
    void loadNamTone();                           // セッションの NAM のファイルを、エンジンに読み込ませる
    void updateToneSummary();
    void toggleTonePanel();
    void showOverlay (juce::Component* panel);    // チューナー・音色の設定を開いたとき (後ろを暗くする)
    void updateLoop();

    // 小節の頭 (ループの A・B を合わせる先)。タブ譜があればその拍子で、なければ 4/4 とみなす
    bool usesScoreBars() const;
    int barIndexNear (double seconds) const;     // いちばん近い小節の頭の番号 (0 = 1小節目)
    int barIndexAt (double seconds) const;       // その位置を含む小節の番号
    double barStartSeconds (int index) const;   // 小節の頭の位置 (秒)
    void setSyncOffset (int ms);    // 拍の位置 (メトロノームとタブ譜)
    void setScoreOffset (int ms);   // タブ譜だけ
    double getScoreOffsetSeconds() const noexcept { return (session.syncOffsetMs + session.scoreOffsetMs) / 1000.0; }
    void seekToBar (double barStartBeats);
    void handlePageKey (const juce::String& key);   // WebView (タブ譜・動画) の上で押されたキー
    void toggleSwap();
    bool isSwapped() const noexcept { return session.videoInMainArea && isYouTube(); }
    void updateSwapUi();
    void updateMixerDefaults();     // ミキサーのスライダーをダブルクリックしたときに戻す値 (保存されている値)
    void renameSession (const juce::String& newTitle);
    void toggleSourceMute();
    void applySourceGain();   // ミキサーの「音源」の値とミュートを、エンジン (と YouTube) に送る

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
    bool sourceMuted = false;   // 音源だけを消す (タブ譜だけを流したいとき)。セッションには保存しない
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
    juce::TextButton swapButton;        // タブ譜と動画の入れ替え
    juce::TextButton equalBarsButton;   // 小節を同じ幅で並べる
    ScoreView scoreView;
    TimelineView timeline;

    // 境目と、右のパネル
    SplitterBar splitter;
    juce::TextButton sidePanelButton;   // 右のパネルを隠す・出すタブ
    std::vector<juce::Rectangle<int>> sideCards;   // 右のパネルの区切り (paint で面を描く)

    SectionHeading sourceHeading { "音源"_jp }, toneHeading { "音色"_jp }, mixerHeading { "ミキサー (音量)"_jp },
                   offsetHeading { "テンポとずらし (タブ譜・メトロノームの位置合わせ)"_jp };
    PlaceholderView videoView;
    juce::Label sourceInfoLabel;        // 動画の真下: YouTube の URL (ダブルクリックで変更) か、音源のファイル名
    TrashButton sourceRemoveButton;     // 音源を外す
    juce::TextButton chooseFileButton, pasteUrlButton;   // 音源がないとき
    std::array<juce::TextButton, 4> toneButtons;   // クリーン・クランチ・歪み・NAM
    juce::Label toneSummaryLabel;                  // NAM のモデルと IR の名前
    juce::TextButton toneDetailButton;             // 音色の設定 (NAM) を開く
    static constexpr int namToneIndex = 3;
    juce::Label sourceGainLabel, guitarGainLabel, metronomeGainLabel;
    WheelSlider sourceGainSlider, guitarGainSlider, metronomeGainSlider;
    juce::TextButton sourceMuteButton;   // 音源だけのミュート
    juce::Label bpmLabel, scoreOffsetLabel, offsetLabel;
    WheelSlider bpmSlider;
    FixedTextButton tapButton;
    OffsetControl scoreOffsetControl, offsetControl;

    // 上部: チューナー
    FixedTextButton tunerButton;

    // 下部: 再生操作。「再生／速度／メトロノーム／ループ」のグループに分け、それぞれに見出しを付ける
    SectionHeading playHeading { "再生"_jp }, speedHeading { "速度 (%)"_jp },
                   metronomeHeading { "メトロノーム"_jp }, loopHeading { "ループ (A〜B をくり返す)"_jp };
    std::vector<int> bottomDividers;   // グループの区切り線の x 座標 (paint で描く)
    static constexpr int bottomBarHeight = 84;
    juce::TextButton toStartButton, rewindButton;
    FixedTextButton playButton;
    juce::TextButton forwardButton;
    WheelSlider speedSlider;
    FixedTextButton metronomeButton;
    juce::TextButton metronomeSoloButton;
    juce::TextButton loopStartButton, loopEndButton;
    FixedTextButton loopButton;

    std::unique_ptr<TunerPanel> tunerPanel;
    std::unique_ptr<ToneDetailPanel> tonePanel;
    DimOverlay dimOverlay;   // チューナー・音色の設定の後ろを暗くする
};
