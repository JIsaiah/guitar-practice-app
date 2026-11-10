#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Audio/AudioEngine.h"
#include "../Library/SessionLibrary.h"
#include "../Library/SetlistStore.h"
#include "../Session/PlaybackSettingsResolver.h"
#include "../Session/SessionOpenContext.h"
#include "../Setlist/SetlistPlayer.h"

class SessionScreen;
#include "../Theme/ThemeManager.h"
#include "../AppSettings.h"
#include "../Web/YouTubePlayer.h"
#include "../Web/ScoreViewer.h"

// 画面の切り替えを担当する
//   セッション一覧 ─(新規)→ セッション作成 ─(練習を始める)→ セッション画面
//        ↑   └────────(開く)──────────────────────────────┘    │
//        └─────────────────(◀ 一覧)─────────────────────────────┘
class MainComponent final : public juce::Component
{
public:
    MainComponent (AudioEngine& engine, SessionLibrary& library, SetlistStore& setlists, ThemeManager& theme, AppSettings& settings);
    ~MainComponent() override;

    void resized() override;
    void paintOverChildren (juce::Graphics&) override;

private:
    void showList();
    void showSetup();
    void showSession (Session session, SessionOpenContext context = {}, PlaybackSettingsResolver resolver = {});
    // セットリストの曲を開き、startDelaySeconds 待ってから自動で再生を始める (setlist-mode.md 5、7)。負なら開くだけ
    void openSetlistEntryLater (const juce::String& setlistId, int entryIndex, double startDelaySeconds);
    void connectSetlist (SessionScreen& screen, const juce::String& setlistId, const juce::String& entryId);
    std::optional<SetlistPlayer> makeSetlistPlayer (const juce::String& setlistId, const juce::String& entryId);
    void openSessionLater (std::function<std::optional<Session> (MainComponent&)> load);
    void openSettings();
    void openLogoTuner();
    void openDebugLog();   // 動作ログ・クラッシュの記録 (設定画面のボタンから)

    // 画面のボタン処理の途中でその画面を破棄しないよう、切り替えは次のメッセージで行う
    void navigate (std::function<void (MainComponent&)> action);
    void setScreen (std::unique_ptr<juce::Component> screen);

    AudioEngine& engine;
    SessionLibrary& library;
    SessionStore& store;
    SetlistStore& setlistStore;
    ThemeManager& theme;
    AppSettings& settings;
    std::unique_ptr<juce::Component> currentScreen;

    // 通し再生: 曲が終わったら次の曲へ進むか (「通し再生を止める」で false。次・前・再開で true に戻る)
    bool setlistAutoAdvance = true;

    // 通し再生で次・前の曲へ移るときに、前の曲の画面から取っておく状態 (メトロノーム・右のパネル)。
    // 次の曲を開いたら当てて消す。一覧から通し再生を始めたときは無い
    std::optional<SetlistCarryOver> setlistCarryOver;

    // YouTube のプレイヤー (WebView)。アプリの起動中に1つだけ作り、練習画面に貸す (実装指示 T5)
    YouTubePlayer youtubePlayer;
    ScoreViewer scoreViewer;   // タブ譜 (alphaTab) の WebView。これも1つだけ作って使い回す

    // 時間の計測 (実装指示 T5): 次に描画されたときに記録する
    struct PendingMark { juce::String label; double startMs; bool isFirstList; };
    std::optional<PendingMark> pendingPaintMark;
    double listShownAtMs = 0.0;
    bool firstListShown = false;
    double sessionOpenedAtMs = 0.0;

    // デスメタルモードで、ロゴにしたボタンの元の文字列を出す
    juce::TooltipWindow tooltipWindow;

    // 開発用のロゴ調整ウィンドウ (Debug ビルドのみ)
    std::unique_ptr<juce::DocumentWindow> logoTuner;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
