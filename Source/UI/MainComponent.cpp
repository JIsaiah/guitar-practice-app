#include "MainComponent.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include "../Text.h"
#include "SessionListScreen.h"
#include "SessionScreen.h"
#include "SessionSetupScreen.h"
#include "SettingsPanel.h"
#include "../Theme/Dev/MetalLogoTuner.h"
#include "../TimingLog.h"

MainComponent::MainComponent (AudioEngine& e, SessionStore& s, ThemeManager& t, AppSettings& a)
    : engine (e), store (s), theme (t), settings (a)
{
    addChildComponent (youtubePlayer);
    addChildComponent (scoreViewer);

    youtubePlayer.onApiReady = [this]
    {
        if (listShownAtMs > 0.0)
            TimingLog::mark ("2. 一覧の表示 → WebView (YouTube) の準備完了"_jp, listShownAtMs);
    };

    setSize (1280, 800);
    showList();
}

void MainComponent::paintOverChildren (juce::Graphics&)
{
    if (! pendingPaintMark.has_value())
        return;

    const auto mark = *pendingPaintMark;
    pendingPaintMark.reset();
    TimingLog::mark (mark.label, mark.startMs);

    if (mark.isFirstList)
    {
        // 一覧を表示した直後に、裏で WebView を準備する (描画を待たせないよう、次のメッセージで)
        listShownAtMs = TimingLog::now();
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer (this)]
        {
            if (safe != nullptr)
            {
                safe->youtubePlayer.prepare();
                safe->scoreViewer.prepare();
            }
        });
    }
}

MainComponent::~MainComponent() = default;

void MainComponent::resized()
{
    if (currentScreen != nullptr)
        currentScreen->setBounds (getLocalBounds());
}

//==============================================================================
void MainComponent::showList()
{
    auto screen = std::make_unique<SessionListScreen> (store, theme);

    screen->onNewSession = [this] { navigate ([] (MainComponent& m) { m.showSetup(); }); };
    screen->onOpenSettings = [this] { openSettings(); };
    screen->onOpenSession = [this] (const juce::String& id)
    {
        const auto clickedAt = TimingLog::now();
        navigate ([id, clickedAt] (MainComponent& m)
        {
            if (auto session = m.store.load (id))
            {
                m.showSession (std::move (*session));
                m.pendingPaintMark = PendingMark { "3. 開く → 練習画面の表示"_jp, clickedAt, false };
                m.sessionOpenedAtMs = clickedAt;
            }
            else
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                        "開けませんでした"_jp,
                                                        "セッションファイルが壊れているか、見つかりません。"_jp);
        });
    };

    setScreen (std::move (screen));

    if (! firstListShown)
    {
        firstListShown = true;
        pendingPaintMark = PendingMark { "1. 起動 → 一覧の表示"_jp, TimingLog::processStart(), true };
    }
}

void MainComponent::showSetup()
{
    auto screen = std::make_unique<SessionSetupScreen>();

    screen->onCancel = [this] { navigate ([] (MainComponent& m) { m.showList(); }); };
    screen->onStart = [this] (Session session)
    {
        navigate ([session] (MainComponent& m) mutable
        {
            // 作った時点で保存し、一覧に出るようにする
            if (! m.store.save (session))
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                        "保存できませんでした"_jp,
                                                        m.store.getDirectory().getFullPathName());
            m.showSession (std::move (session));
        });
    };

    setScreen (std::move (screen));
}

void MainComponent::showSession (Session session)
{
    auto screen = std::make_unique<SessionScreen> (engine, store, theme, settings, youtubePlayer, scoreViewer, std::move (session));

    screen->onBackToList = [this] { navigate ([] (MainComponent& m) { m.showList(); }); };
    screen->onOpenSettings = [this] { openSettings(); };
    screen->onVideoReady = [this]
    {
        if (sessionOpenedAtMs > 0.0)
            TimingLog::mark ("3b. 開く → YouTube の動画を再生できる状態"_jp, sessionOpenedAtMs);
        sessionOpenedAtMs = 0.0;
    };

    setScreen (std::move (screen));
}

void MainComponent::openSettings()
{
    // 上: 一般 (テーマ、戻る・進むの秒数、ホイールの量)、下: 詳細設定 (オーディオ)
    juce::DialogWindow::LaunchOptions options;
    // 設定画面は縦に長いので、スクロールできる枠に入れる
    auto* panel = new SettingsPanel (settings, theme, engine.getDeviceManager());
    panel->onOpenLogoTuner = [this] { openLogoTuner(); };

    auto* viewport = new juce::Viewport();
    viewport->setViewedComponent (panel, true);
    viewport->setScrollBarsShown (true, false);
    viewport->setSize (panel->getWidth() + viewport->getScrollBarThickness(), juce::jmin (panel->getHeight(), 760));
    options.content.setOwned (viewport);
    options.dialogTitle = "設定"_jp;
    options.dialogBackgroundColour = getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId);
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.componentToCentreAround = this;
    options.launchAsync();
}

void MainComponent::openLogoTuner()
{
    if (logoTuner == nullptr)
        logoTuner = std::make_unique<MetalLogoTunerWindow> (theme);

    logoTuner->setVisible (true);
    logoTuner->toFront (true);
}

//==============================================================================
void MainComponent::navigate (std::function<void (MainComponent&)> action)
{
    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer (this), action = std::move (action)]
    {
        if (safe != nullptr)
            action (*safe);
    });
}

void MainComponent::setScreen (std::unique_ptr<juce::Component> screen)
{
    currentScreen = std::move (screen);
    addAndMakeVisible (*currentScreen);
    currentScreen->setBounds (getLocalBounds());

    if (currentScreen->isShowing())
        currentScreen->grabKeyboardFocus();
}
