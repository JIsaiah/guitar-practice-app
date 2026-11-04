#include "MainComponent.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include "../Text.h"
#include "SessionListScreen.h"
#include "SessionScreen.h"
#include "SessionSetupScreen.h"
#include "SettingsPanel.h"
#include "../Theme/Dev/MetalLogoTuner.h"
#include "../TimingLog.h"

MainComponent::MainComponent (AudioEngine& e, SessionLibrary& l, SetlistStore& sl, ThemeManager& t, AppSettings& a)
    : engine (e), library (l), store (l.getStore()), setlistStore (sl), theme (t), settings (a)
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
    auto screen = std::make_unique<SessionListScreen> (library, setlistStore, theme, settings);

    screen->onNewSession = [this] { navigate ([] (MainComponent& m) { m.showSetup(); }); };
    screen->onOpenSettings = [this] { openSettings(); };
    screen->onOpenSession = [this] (const juce::String& id)
    {
        openSessionLater ([id] (MainComponent& m) { return m.store.load (id); });
    };
    screen->onOpenSessionById = [this] (const juce::String& sessionId)
    {
        openSessionLater ([sessionId] (MainComponent& m) { return m.library.load (sessionId); });
    };
    screen->onPlaySetlist = [this] (const juce::String& setlistId, int entryIndex)
    {
        // 「通しで再生」「この曲から通しで再生」: 1曲目は開くだけで、再生は自分で始める (作者の指示)。
        // 2曲目からは自動で送る
        setlistAutoAdvance = true;
        openSetlistEntryLater (setlistId, entryIndex, -1.0);
    };

    setScreen (std::move (screen));

    if (! firstListShown)
    {
        firstListShown = true;
        pendingPaintMark = PendingMark { "1. 起動 → 一覧の表示"_jp, TimingLog::processStart(), true };
    }
}

void MainComponent::openSessionLater (std::function<std::optional<Session> (MainComponent&)> load)
{
    const auto clickedAt = TimingLog::now();
    navigate ([load = std::move (load), clickedAt] (MainComponent& m)
    {
        if (auto session = load (m))
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
}

void MainComponent::openSetlistEntryLater (const juce::String& setlistId, int entryIndex, double startDelaySeconds)
{
    // セットリストから開く: 速度・ループはエントリの専用設定、それ以外はセッションの設定 (setlist-mode.md 3.5、3.6)
    const auto clickedAt = TimingLog::now();
    navigate ([setlistId, entryIndex, startDelaySeconds, clickedAt] (MainComponent& m)
    {
        const auto setlist = m.setlistStore.load (setlistId);
        if (setlist.has_value() && juce::isPositiveAndBelow (entryIndex, (int) setlist->entries.size()))
        {
            const auto& entry = setlist->entries[(size_t) entryIndex];
            if (auto session = m.library.load (entry.sessionId))
            {
                // 通し再生なので、ループ (くり返し) は外して開く (作者の指示)
                PlaybackSettingsResolver resolver (entry.overrides);
                auto working = resolver.makeWorkingCopy (*session, true);
                m.showSession (std::move (working), SessionOpenContext::forSetlist (setlistId, entry.entryId), std::move (resolver));

                // 次の曲は、待っている間に読み込む (読み込みが間に合わなければ「読み込み中…」のあとに始まる)
                if (auto* screen = dynamic_cast<SessionScreen*> (m.currentScreen.get()))
                {
                    m.connectSetlist (*screen, setlistId, entry.entryId);
                    if (startDelaySeconds >= 0.0)
                        screen->startAutomatically (startDelaySeconds);
                }

                m.pendingPaintMark = PendingMark { "3. 開く → 練習画面の表示"_jp, clickedAt, false };
                m.sessionOpenedAtMs = clickedAt;
                return;
            }
        }

        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                "開けませんでした"_jp,
                                                "セットリストか、その曲のセッションが見つかりません。"_jp);
    });
}

std::optional<SetlistPlayer> MainComponent::makeSetlistPlayer (const juce::String& setlistId, const juce::String& entryId)
{
    // 練習中にセットリストが変わっていてもよいように、毎回ファイルから作る
    auto setlist = setlistStore.load (setlistId);
    if (! setlist.has_value())
        return std::nullopt;

    return SetlistPlayer (std::move (*setlist), [this] (const juce::String& id) { return library.contains (id); }, entryId);
}

void MainComponent::connectSetlist (SessionScreen& screen, const juce::String& setlistId, const juce::String& entryId)
{
    const auto safe = juce::Component::SafePointer<SessionScreen> (&screen);

    const auto refreshInfo = [this, safe, setlistId, entryId]
    {
        auto player = makeSetlistPlayer (setlistId, entryId);
        if (safe == nullptr || ! player.has_value())
            return;

        SetlistBar::Info info;
        info.setlistName = player->getSetlist().name;
        info.position = player->getCurrentIndex() + 1;
        info.count = player->getNumEntries();
        if (const auto* next = player->getNext())
        {
            const auto title = library.getTitle (next->sessionId);
            info.nextTitle = title.isNotEmpty() ? title : "(無題)"_jp;
        }
        info.hasPrevious = player->hasPrevious();
        info.hasNext = player->hasNext();
        info.autoAdvance = setlistAutoAdvance;
        safe->setSetlistInfo (info);
    };
    refreshInfo();

    // 次・前の曲へ。今の曲の変更は保存してから移る
    const auto moveTo = [this, safe, setlistId] (int index, double delaySeconds)
    {
        if (safe != nullptr)
        {
            safe->stopForSetlist();
            safe->saveChangesQuietly();
        }
        openSetlistEntryLater (setlistId, index, delaySeconds);
    };

    // 曲が終わった: 曲間の待ち時間のあとに次の曲。最後の曲なら終わり (setlist-mode.md 7.1、7.4)
    screen.onSongFinished = [this, safe, setlistId, entryId, moveTo]
    {
        auto player = makeSetlistPlayer (setlistId, entryId);
        if (safe == nullptr || ! player.has_value() || ! setlistAutoAdvance)
            return;

        const auto next = player->findNext();
        if (next < 0)
            safe->showSetlistEnd();
        else
            moveTo (next, settings.getSongGapSeconds());
    };

    // 手で次・前へ移ったときも、自動送りと同じ待ち時間 (作者の指示でひとつにした)
    screen.onSetlistNext = [this, setlistId, entryId, moveTo]
    {
        if (auto player = makeSetlistPlayer (setlistId, entryId); player.has_value() && player->hasNext())
        {
            setlistAutoAdvance = true;
            moveTo (player->findNext(), settings.getSongGapSeconds());
        }
    };
    screen.onSetlistPrevious = [this, setlistId, entryId, moveTo]
    {
        if (auto player = makeSetlistPlayer (setlistId, entryId); player.has_value() && player->hasPrevious())
        {
            setlistAutoAdvance = true;
            moveTo (player->findPrevious(), settings.getSongGapSeconds());
        }
    };

    // 「通し再生を止める」: 今の曲を止め、曲が終わっても次へ進まない。「再開」で今の曲から始め直す
    screen.onSetlistAutoAdvanceToggled = [this, safe, refreshInfo] (bool resume)
    {
        setlistAutoAdvance = resume;
        if (safe != nullptr)
        {
            if (resume)
                safe->startAutomatically (settings.getSongGapSeconds());
            else
                safe->stopForSetlist();
        }
        refreshInfo();
    };
}

void MainComponent::showSetup()
{
    auto screen = std::make_unique<SessionSetupScreen> (scoreViewer);   // タブ譜のテンポなどを読むのに使う (画面には出さない)

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

void MainComponent::showSession (Session session, SessionOpenContext context, PlaybackSettingsResolver resolver)
{
    // 前の画面を先に消す。練習画面から練習画面へ移るとき (セットリストの次の曲)、
    // 前の画面の後片付け (音源を外す、YouTube の動画を外す) が、新しい画面の読み込みを消してしまうため
    currentScreen.reset();

    auto screen = std::make_unique<SessionScreen> (engine, store, theme, settings, youtubePlayer, scoreViewer, std::move (session),
                                                   context, std::move (resolver));

    if (context.isSetlist())
    {
        // エントリの専用設定 (速度・ループ) をセットリストのファイルに書く。
        // 練習中にセットリストが変わっていてもよいように、毎回読み直してから書く
        screen->onSaveOverrides = [this, context] (const SetlistEntry::Overrides& overrides)
        {
            auto setlist = setlistStore.load (context.setlistId);
            if (! setlist.has_value())
                return false;

            auto* entry = setlist->findEntry (context.entryId);
            if (entry == nullptr)
                return false;

            entry->overrides = overrides;
            return setlistStore.save (*setlist);
        };
    }

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
