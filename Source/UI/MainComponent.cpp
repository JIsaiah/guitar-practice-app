#include "MainComponent.h"
#include "AppDialog.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include "../Text.h"
#include "../Audio/PluginLocator.h"
#include "../Library/SessionRisk.h"
#include "SessionListScreen.h"
#include "SessionScreen.h"
#include "SessionSetupScreen.h"
#include "SettingsPanel.h"
#include "DebugLogPanel.h"
#include "../Theme/Dev/MetalLogoTuner.h"
#include "../TimingLog.h"
#include "../AppLog.h"

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

    // アプリの中に重ねた画面 (確認・入力・設定) を開いた・閉じたら、練習画面のタブ譜・動画・プラグインの画面を隠す・出す
    // (それらは Windows の別の窓として、アプリの部品の上に描かれるため)
    AppOverlay::onOpenCountChanged = [this]
    {
        if (auto* screen = dynamic_cast<SessionScreen*> (currentScreen.get()))
            screen->refreshNativeViews();
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

MainComponent::~MainComponent()
{
    // 画面を先に消す。練習画面のデストラクタは、YouTube のプレイヤーやタブ譜の WebView (このクラスのメンバー) を使って
    // 後片付けをする。メンバーは宣言と逆の順に消えるので、何もしないとプレイヤーが先に消え、
    // アプリを閉じるときに落ちる (クラッシュダンプで確認)
    AppOverlay::onOpenCountChanged = nullptr;
    currentScreen.reset();
}

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
    // ここでは読むだけ。最後に開いた日時は、確かめて開くと決めてから記録する (openSessionLater)
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
        setlistCarryOver.reset();   // 1曲目は、そのセッションの開き方のまま
        openSetlistEntryLater (setlistId, entryIndex, -1.0);
    };

    auto* listScreen = screen.get();
    setScreen (std::move (screen));

    if (! firstListShown)
    {
        firstListShown = true;
        pendingPaintMark = PendingMark { "1. 起動 → 一覧の表示"_jp, TimingLog::processStart(), true };

        // アプリを起動したら、いちばん上のセッションにキーボード操作の枠を出しておく (作者の指示: すぐ Enter で開けるように)
        listScreen->showNavigationOnFirstSession();
    }
}

void MainComponent::openSessionLater (std::function<std::optional<Session> (MainComponent&)> load)
{
    const auto clickedAt = TimingLog::now();
    navigate ([load = std::move (load), clickedAt] (MainComponent& m)
    {
        auto session = load (m);
        if (! session.has_value())
        {
            AppDialog::showMessageBoxAsync (juce::MessageBoxIconType::NoIcon,
                                                    "開けませんでした"_jp,
                                                    "セッションファイルが壊れているか、見つかりません。"_jp);
            return;
        }

        // 人から受け取ったセッションなら、危ないものを確かめてから開く (セキュリティ。SessionRisk.h)
        m.checkBeforeOpening (std::move (*session), [mp = &m, clickedAt] (Session s)
        {
            // 開くと決めてから、最後に開いた日時を記録する (ファイル名が曲名の名前に変わることがあるので、新しい名前を使う)
            s.id = mp->store.markOpened (s.id);
            s.lastOpened = juce::Time::getCurrentTime();
            mp->showSession (std::move (s));
            mp->pendingPaintMark = PendingMark { "3. 開く → 練習画面の表示"_jp, clickedAt, false };
            mp->sessionOpenedAtMs = clickedAt;
        });
    });
}

// この PC で保存していないセッション (人から受け取った・アプリの外で書き換えた) に、危ないもの (プラグイン・ネットワークの場所・
// プラグインの状態) があれば、開く前に聞く (セキュリティ。SessionTrust.h・SessionRisk.h)。
// この PC のセッションや、危ないものが無いセッションは、聞かずに proceed を呼ぶ。「開かない」なら何もしない (今の画面のまま)
void MainComponent::checkBeforeOpening (Session session, std::function<void (Session)> proceed)
{
    if (session.id.isEmpty() || store.isFromThisPc (session.id))
    {
        proceed (std::move (session));
        return;
    }

    const auto folders = PluginLocator::getVst3Folders();
    const auto risks = SessionRisk::collect (session, folders);
    if (risks.empty())
    {
        proceed (std::move (session));
        return;
    }

    const auto title = session.title.isNotEmpty() ? session.title : "(無題)"_jp;
    auto options = juce::MessageBoxOptions()
                       .withIconType (juce::MessageBoxIconType::WarningIcon)
                       .withTitle ("人から受け取ったセッションです"_jp)
                       .withMessage ("「"_jp + title + "」は、このパソコンで保存したセッションではありません"
                                     " (人から受け取った、またはアプリの外で書き換えたファイル)。次のものが含まれています。\n\n"_jp
                                     + SessionRisk::describe (risks)
                                     + "\n\n「安全に開く」: これらを外して開きます。外したものは、あとで自分で選び直せます。\n"
                                       "「信頼して開く」: このまま開きます。作った人を信頼できるときだけ選んでください。"_jp)
                       .withButton ("安全に開く"_jp)
                       .withButton ("信頼して開く"_jp)
                       .withButton ("開かない"_jp)
                       .withAssociatedComponent (this);

    // 3ボタンの戻り値: 1番目 = 1、2番目 = 2、3番目 = 0
    AppDialog::showAsync (options, [safe = juce::Component::SafePointer (this), session, folders,
                                            proceed = std::move (proceed)] (int result)
    {
        if (safe == nullptr || result == 0)
            return;

        if (result == 1)
        {
            auto cleaned = session;
            SessionRisk::makeSafe (cleaned, folders);   // ファイルは、保存するまで変わらない
            proceed (std::move (cleaned));
            return;
        }

        safe->store.trustSession (session.id);   // 次からは聞かない
        proceed (session);
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
            // セットリストで弾いた曲は「最後の練習日」にしない (作者の指示: 一覧の表示順に影響させない)。
            // そのため、開いた日時を記録する openForPractice ではなく、読むだけの load を使う
            if (auto session = m.library.load (entry.sessionId))
            {
                // 人から受け取ったセッションなら、危ないものを確かめてから開く (セキュリティ。「開かない」なら今の画面のまま)
                m.checkBeforeOpening (std::move (*session),
                                      [mp = &m, setlistId, entry, startDelaySeconds, clickedAt] (Session checked)
                {
                    auto& m2 = *mp;

                    // 通し再生なので、ループ (くり返し) は外して開く (作者の指示)
                    PlaybackSettingsResolver resolver (entry.overrides);
                    auto working = resolver.makeWorkingCopy (checked, true);
                    m2.showSession (std::move (working), SessionOpenContext::forSetlist (setlistId, entry.entryId), std::move (resolver));

                    // 次の曲は、待っている間に読み込む (読み込みが間に合わなければ「読み込み中…」のあとに始まる)
                    if (auto* screen = dynamic_cast<SessionScreen*> (m2.currentScreen.get()))
                    {
                        m2.connectSetlist (*screen, setlistId, entry.entryId);

                        // 前の曲のメトロノーム・右のパネルの状態を引き継ぐ (曲間でも保つ。作者の指示)
                        if (m2.setlistCarryOver.has_value())
                            screen->applySetlistCarryOver (*m2.setlistCarryOver);
                        m2.setlistCarryOver.reset();

                        if (startDelaySeconds >= 0.0)
                            screen->startAutomatically (startDelaySeconds);
                    }

                    m2.pendingPaintMark = PendingMark { "3. 開く → 練習画面の表示"_jp, clickedAt, false };
                    m2.sessionOpenedAtMs = clickedAt;
                });
                return;
            }
        }

        AppDialog::showMessageBoxAsync (juce::MessageBoxIconType::NoIcon,
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
            setlistCarryOver = safe->getSetlistCarryOver();   // 画面を消す前に、引き継ぐ状態を取っておく
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
            // 一覧でフォルダを選んでいたら、そのフォルダに入れる (戻ったときに、作ったセッションが見えるように)
            if (const auto filter = m.settings.getSessionFolderFilter(); filter.startsWith ("folder:"))
                session.folder = filter.fromFirstOccurrenceOf ("folder:", false, false);

            // 作った時点で保存し、一覧に出るようにする (作ってそのまま練習画面を開くので、最後に開いた日時も今)
            session.lastOpened = juce::Time::getCurrentTime();
            if (! m.store.save (session))
                AppDialog::showMessageBoxAsync (juce::MessageBoxIconType::NoIcon,
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
    // 上: 一般 (テーマ、戻る・進むの秒数、ホイールの量)、下: 詳細設定 (オーディオ)。
    // アプリの中に重ねて出す (作者の指示: 別のウィンドウで開かない。AppPanelOverlay)
    // 設定画面は縦に長いので、スクロールできる枠に入れる
    auto* panel = new SettingsPanel (settings, theme, engine.getDeviceManager());
    panel->onOpenLogoTuner = [this] { openLogoTuner(); };
    panel->onOpenDebugLog = [this] { openDebugLog(); };

    auto* viewport = new juce::Viewport();
    viewport->setViewedComponent (panel, true);
    viewport->setScrollBarsShown (true, false);
    viewport->setSize (panel->getWidth() + viewport->getScrollBarThickness(), juce::jmin (panel->getHeight(), 760));
    auto* overlay = new AppPanelOverlay ("設定"_jp, viewport, viewport->getWidth(), viewport->getHeight());
    overlay->showOver (this);   // 閉じると消える
}

void MainComponent::openDebugLog()
{
    // 設定画面と同じく、アプリの中に重ねて出す (閉じると消える)。設定画面の上に重なる
    auto* panel = new DebugLogPanel();
    auto* overlay = new AppPanelOverlay ("動作ログ・クラッシュの記録"_jp, panel, juce::jmax (700, panel->getWidth()), juce::jmax (500, panel->getHeight()));
    overlay->showOver (this);
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
    currentScreen = std::move (screen);   // 古い画面は、ここで消える

    // ギターの音は、練習画面だけで出す (作者の指示)。古い画面が消えたあとに決める
    // (練習画面のコンストラクタ・デストラクタで切り替えると、新しい画面を作ってから古い画面を消す順番のため、
    //  古い画面のデストラクタが、新しい画面で出した音を止めてしまう)
    engine.setGuitarMonitoring (dynamic_cast<SessionScreen*> (currentScreen.get()) != nullptr);

    AppLog::write (dynamic_cast<SessionScreen*> (currentScreen.get()) != nullptr      ? "画面: 練習画面"_jp
                   : dynamic_cast<SessionListScreen*> (currentScreen.get()) != nullptr ? "画面: 一覧"_jp
                                                                                       : "画面: 新規作成"_jp);

    addAndMakeVisible (*currentScreen);
    currentScreen->setBounds (getLocalBounds());

    if (currentScreen->isShowing())
        currentScreen->grabKeyboardFocus();
}
