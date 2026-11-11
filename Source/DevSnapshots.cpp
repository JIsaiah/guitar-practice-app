#include "DevSnapshots.h"

#if JUCE_DEBUG

#include "Library/SessionLibrary.h"
#include "Library/SetlistStore.h"
#include "Theme/ThemeManager.h"
#include "Theme/MetalLogo/FixedTexts.h"
#include "Theme/MetalLogo/MetalLogoGenerator.h"
#include "UI/SessionListScreen.h"
#include "UI/SessionScreen.h"
#include "UI/SessionSetupScreen.h"
#include "UI/SettingsPanel.h"
#include "UI/RadialMenu.h"
#include "UI/DebugLogPanel.h"
#include "AppLog.h"
#include "UI/TunerPanel.h"
#include "AppSettings.h"

namespace
{
    bool savePng (const juce::Image& image, const juce::File& file)
    {
        file.deleteFile();
        juce::FileOutputStream out (file);
        return out.openedOk() && juce::PNGImageFormat().writeImageToStream (image, out);
    }

    // 全部の決まった文言を、左に「延長・トゲなし」、右に「今のパラメータ」で並べた1枚の画像
    juce::Image renderLogoSheet (ThemeManager& theme, juce::String& log)
    {
        const MetalLogoParams params;
        auto plain = params;
        plain.tipExtend = 0.0f;
        plain.spikeProbability = 0.0f;

        constexpr int rowHeight = 110, columnWidth = 760;
        juce::Image sheet (juce::Image::RGB, columnWidth * 2, rowHeight * (int) std::size (allFixedTextIds), true);
        juce::Graphics g (sheet);
        g.fillAll (Palette::deathMetal().background);

        const juce::Font font (juce::FontOptions (theme.getMetalTypeface()));
        int row = 0;

        for (auto id : allFixedTextIds)
        {
            const auto text = getFixedText (id);

            for (int column = 0; column < 2; ++column)
            {
                const auto& p = column == 0 ? plain : params;
                const auto start = juce::Time::getMillisecondCounterHiRes();
                const auto logo = MetalLogoGenerator::generate (text, font, p, MetalLogoGenerator::seedFor (text));
                const auto ms = juce::Time::getMillisecondCounterHiRes() - start;

                if (column == 1)
                    log << text << ": " << juce::String (ms, 2) << " ms\n";

                const auto cell = juce::Rectangle<int> (column * columnWidth, row * rowHeight, columnWidth, rowHeight).reduced (8);
                g.setColour (juce::Colours::white.withAlpha (0.12f));
                g.drawRect (cell);
                g.setColour (Palette::deathMetal().logoText);
                g.fillPath (logo, juce::RectanglePlacement (juce::RectanglePlacement::centred)
                                      .getTransformToFit (MetalLogoGenerator::getLogoArea (logo, p), cell.reduced (4).toFloat()));
            }
            ++row;
        }
        return sheet;
    }
}

juce::String DevSnapshots::renderAll (const juce::File& folder)
{
    folder.createDirectory();
    juce::String log;

    // 作者の設定ファイルには触らない。一時ファイルの設定で ThemeManager を作る
    const juce::TemporaryFile tempSettings (".settings");
    juce::PropertiesFile::Options options;
    juce::PropertiesFile settings (tempSettings.getFile(), options);
    {
        ThemeManager theme (settings);
        AppSettings listSettings (settings);

        // 作者のセッションには触らない。一時フォルダに見本のセッションとセットリストを作る
        const auto sampleFolder = juce::File::createTempFile ("snapshot-samples");
        struct RemoveWhenDone { juce::File folder; ~RemoveWhenDone() { folder.deleteRecursively(); } } removeSamples { sampleFolder };
        SessionStore store (sampleFolder.getChildFile ("Sessions"));
        SessionLibrary library (store);
        SetlistStore setlistStore (sampleFolder.getChildFile ("Setlists"));
        {
            const std::pair<const char*, double> songs[] { { "Hotel California (ソロ)", 391.0 }, { "Crazy Train", 296.0 },
                                                           { "Master of Puppets", 0.0 }, { "Sweet Child O' Mine", 356.0 } };
            // 一覧の列 (アーティスト・BPM・難易度・チューニング) の見本
            const char* const artists[] { "Eagles", "Ozzy Osbourne", "", "Guns N' Roses" };
            const char* const tunings[] { "E Standard", "E Standard", "", "Eb Standard" };
            const double bpms[] { 74.0, 138.0, 212.0, 125.0 };
            const int stars[] { 4, 3, 0, 5 };
            const char* const folders[] { "ライブ", "ライブ", "練習中", "" };   // フォルダ分けの見本
            listSettings.setSessionFolders ({ "あとで"_jp });                  // 中身のないフォルダ

            std::vector<Session> saved;
            int index = 0;
            for (const auto& [title, length] : songs)
            {
                Session s;
                s.title = juce::String::fromUTF8 (title);
                s.artist = artists[index];
                s.tuning = tunings[index];
                s.bpm = bpms[index];
                s.difficulty = stars[index];
                s.folder = juce::String::fromUTF8 (folders[index]);
                ++index;
                s.sourceType = SourceType::local;
                s.lengthSeconds = length;
                store.save (s);
                saved.push_back (s);
            }

            auto empty = setlistStore.createNew();   // 空のセットリスト (先に保存し、見本のほうを一覧の先頭にする)
            setlistStore.save (empty);
            juce::Thread::sleep (20);

            auto setlist = setlistStore.createNew();
            setlist.name = "11月ライブ"_jp;
            setlist.addEntry (saved[1].sessionId).overrides.speed = 0.8;
            setlist.addEntry (saved[0].sessionId);
            setlist.addEntry ("missing-session");   // リンク切れ
            setlist.addEntry (saved[2].sessionId).overrides.loopEnabled = true;
            setlist.addEntry (saved[1].sessionId);   // 同じ曲をもう一度
            setlistStore.save (setlist);
        }

        for (auto mode : { ThemeMode::Normal, ThemeMode::DeathMetal })
        {
            theme.setMode (mode);

            SessionListScreen screen (library, setlistStore, theme, listSettings);
            screen.showTab (SessionListScreen::Tab::sessions);
            screen.setSize (1280, 800);
            screen.setVisible (true);

            const auto file = folder.getChildFile (mode == ThemeMode::Normal ? "list_normal.png" : "list_deathmetal.png");
            if (savePng (screen.createComponentSnapshot (screen.getLocalBounds(), true, 1.0f), file))
                log << "wrote " << file.getFullPathName() << "\n";
        }

        // ライト・ダークで、一覧・練習画面・チューナー・設定画面
        {
            theme.setMode (ThemeMode::Normal);
            AppSettings appSettings (settings);
            AudioEngine engine;   // デバイスは開かない (音は出さない)
            YouTubePlayer youtube;
            ScoreViewer scoreViewer;

            Session session;
            session.title = "Hotel California (ソロ)"_jp;
            session.artist = "Eagles";
            session.sourceType = SourceType::local;
            session.sourcePath = "C:/Music/hotel-california.mp3";
            session.bpm = 74.0;
            session.speedPercent = 80;

            const auto snap = [&] (juce::Component& c, int w, int h, const juce::String& name)
            {
                c.setSize (w, h);
                c.setVisible (true);
                const auto file = folder.getChildFile (name);
                if (savePng (c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f), file))
                    log << "wrote " << file.getFullPathName() << "\n";
            };

            // 数字の入力の確かめ (画面ではなく、部品の動きをまねて、結果を log.txt に書く)
            {
                // ずらし: 欄に「25」と入れて Enter → onChange に 25 が来る
                OffsetControl offset;
                int received = 0;
                offset.onChange = [&received] (int ms) { received = ms; };
                offset.setSize (300, 30);
                for (auto* child : offset.getChildren())
                    if (auto* label = dynamic_cast<juce::Label*> (child))
                    {
                        label->showEditor();
                        auto* editor = label->getCurrentTextEditor();
                        log << "  editor=" << (editor != nullptr ? "yes" : "no");
                        if (editor != nullptr)
                        {
                            editor->setText ("25");
                            log << " text=" << editor->getText();
                            // Enter: TextEditor は「Enter が押された」をメッセージで後から知らせる。ここではその知らせを直接渡す
                            // (0x10003002 は JUCE の TextEditor の中の番号 returnKeyMessageId)
                            static_cast<juce::Component*> (editor)->handleCommandMessage (0x10003002);   // TextEditor では private のため、部品として呼ぶ
                        }
                        log << " label=" << label->getText() << " editing=" << (label->isBeingEdited() ? "yes" : "no") << "\n";
                    }
                log << "check offset entry: " << received << (received == 25 ? " OK" : " NG") << "\n";

                // BPM などのスライダー: 数字の欄のダブルクリックでは値を戻さない。スライダーの部分では戻す
                WheelSlider slider;
                slider.setRange (40.0, 300.0, 1.0);
                slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 44, 24);
                slider.setDoubleClickReturnValue (true, 100.0);
                slider.setSize (300, 30);
                slider.setValue (150.0);
                juce::Label* box = nullptr;
                for (auto* child : slider.getChildren())
                    if (auto* label = dynamic_cast<juce::Label*> (child))
                        box = label;

                const auto doubleClickOn = [&slider] (juce::Component* target)
                {
                    const auto now = juce::Time::getCurrentTime();
                    const juce::MouseEvent e (juce::Desktop::getInstance().getMainMouseSource(), {}, {},
                                              juce::MouseInputSource::defaultPressure, 0.0f, 0.0f, 0.0f, 0.0f,
                                              target, target, now, {}, now, 2, false);
                    slider.mouseDoubleClick (e);
                };
                doubleClickOn (box);
                const auto afterBox = slider.getValue();
                doubleClickOn (&slider);
                const auto afterSlider = slider.getValue();
                log << "check slider double-click: text box -> " << afterBox << ", slider -> " << afterSlider
                    << ((box != nullptr && juce::exactlyEqual (afterBox, 150.0) && juce::exactlyEqual (afterSlider, 100.0)) ? " OK" : " NG") << "\n";
            }

            for (auto colour : { ColourTheme::Light, ColourTheme::Dark })
            {
                theme.setColourTheme (colour);
                const juce::String suffix = colour == ColourTheme::Light ? "_light.png" : "_dark.png";

                SessionListScreen list (library, setlistStore, theme, listSettings);
                list.showTab (SessionListScreen::Tab::sessions);
                snap (list, 1280, 800, "list" + suffix);

                // セットリストのタブ: 1つ目を開き、曲の追加パネルを出した状態
                list.showTab (SessionListScreen::Tab::setlists);
                snap (list, 1280, 800, "setlists" + suffix);
                list.openFirstSetlistForSnapshot();
                snap (list, 1280, 800, "setlists_open" + suffix);

                SessionScreen practice (engine, store, theme, appSettings, youtube, scoreViewer, session);
                snap (practice, 1280, 800, "practice" + suffix);
                snap (practice, 1920, 1080, "practice_wide" + suffix);   // 大きな画面: 下の帯の余白がグループの間に分かれるか
                practice.setSize (1280, 800);

                // ラジアルメニュー: 何も選んでいない / 「再生・停止」を選んでいる / 画面の左上の端で開いた
                {
                    const auto base = practice.createComponentSnapshot (practice.getLocalBounds(), true, 1.0f);
                    const auto drawMenu = [&] (juce::Point<int> cursor, juce::Point<float> mouse, const juce::String& name)
                    {
                        std::vector<RadialMenu::Item> items {
                            { "再生・停止"_jp, {} }, { "ループ解除"_jp, {} }, { "右の音色プリセット"_jp, {} },
                            { "ギターミュート"_jp, {} }, { "メトロノームオン・オフ"_jp, {} }, { "音源ミュート"_jp, {} },
                            { "左の音色プリセット"_jp, {} }, { "トラック切り替え"_jp, {}, false } };
                        RadialMenu::Window menu (std::move (items));
                        menu.setSize (1280, 800);
                        menu.placeAround (cursor);
                        menu.updateHover (mouse);
                        menu.finishAnimationForTest();

                        auto image = base.createCopy();
                        {
                            juce::Graphics g (image);
                            g.drawImageAt (menu.createComponentSnapshot (menu.getLocalBounds(), true, 1.0f), 0, 0);
                        }
                        if (savePng (image, folder.getChildFile (name + suffix)))
                            log << "wrote " << name << suffix << "\n";
                    };
                    drawMenu ({ 620, 400 }, { 620.0f, 400.0f }, "radial_idle");
                    drawMenu ({ 620, 400 }, { 625.0f, 230.0f }, "radial_hover");
                    drawMenu ({ 60, 50 }, { 60.0f, 50.0f }, "radial_edge");
                }

                // 曲名だけで作ったセッション (音源もタブ譜も無い): タブ譜の枠の真ん中で選んでもらう
                {
                    Session empty;
                    empty.title = "New song";
                    SessionScreen emptyScreen (engine, store, theme, appSettings, youtube, scoreViewer, empty);
                    snap (emptyScreen, 1280, 800, "practice_empty" + suffix);
                }

                // 開いた直後の「読み込み中…」(YouTube の準備ができるまで。書き出しではネットに出ないので、ずっとこの状態)
                {
                    Session loadingSession;
                    loadingSession.title = "Loading";
                    loadingSession.sourceType = SourceType::youtube;
                    loadingSession.youtubeUrl = "https://www.youtube.com/watch?v=dQw4w9WgXcQ";
                    SessionScreen loadingScreen (engine, store, theme, appSettings, youtube, scoreViewer, loadingSession);
                    snap (loadingScreen, 1280, 800, "practice_loading" + suffix);
                }

                // 新しいセッションを作る画面
                {
                    SessionSetupScreen setup (scoreViewer);
                    snap (setup, 1280, 800, "setup" + suffix);
                }

                // セットリストから開いたとき (速度・ループに「セトリ専用」)
                {
                    SetlistEntry::Overrides overrides;
                    overrides.speed = 0.9;
                    PlaybackSettingsResolver resolver (overrides);
                    auto working = resolver.makeWorkingCopy (session);
                    SessionScreen fromSetlist (engine, store, theme, appSettings, youtube, scoreViewer, working,
                                               SessionOpenContext::forSetlist ("sample", "entry"), resolver);
                    SetlistBar::Info info;
                    info.setlistName = "11月ライブ"_jp;
                    info.position = 3;
                    info.count = 8;
                    info.nextTitle = "Crazy Train";
                    info.hasPrevious = info.hasNext = true;
                    fromSetlist.setSetlistInfo (info);
                    snap (fromSetlist, 1280, 800, "practice_setlist" + suffix);

                    fromSetlist.startCountdownForSnapshot();   // タブ譜が無いので、画面の部品で数字を出す
                    snap (fromSetlist, 1280, 800, "practice_countdown" + suffix);
                }

                SettingsPanel panel (appSettings, theme, engine.getDeviceManager());
                snap (panel, panel.getWidth(), panel.getHeight(), "settings" + suffix);

                TunerPanel tuner (engine);
                tuner.setTestFrequency (112.0);   // A2 (110Hz) より少し高い
                snap (tuner, TunerPanel::preferredWidth, TunerPanel::preferredHeight, "tuner_sharp" + suffix);
                tuner.setTestFrequency (110.2);   // ほぼ合っている
                snap (tuner, TunerPanel::preferredWidth, TunerPanel::preferredHeight, "tuner_ok" + suffix);

                // メトロノームのビジュアライザー: 4/4 の1拍目、6/8 の3拍目、9/8 の4拍目、12/8 の8拍目、止まっているとき (縦に並べる)。
                // 大きさは下の帯の中と同じ (116 x 46)。数が多いと何列かに折り返す
                {
                    juce::Image strip (juce::Image::ARGB, 116, 5 * 50, true);
                    {
                        juce::Graphics g (strip);
                        g.fillAll (theme.getPalette().panel);
                        MetronomeDots dots;
                        dots.setLookAndFeel (&juce::LookAndFeel::getDefaultLookAndFeel());
                        dots.setSize (116, 46);
                        const int states[][3] { { 4, 4, 0 }, { 6, 8, 2 }, { 9, 8, 3 }, { 12, 8, 7 }, { 4, 4, -1 } };
                        for (int i = 0; i < 5; ++i)
                        {
                            dots.setState (states[i][0], states[i][1], states[i][2]);
                            g.drawImageAt (dots.createComponentSnapshot (dots.getLocalBounds(), true, 1.0f), 0, i * 50);
                        }
                        dots.setLookAndFeel (nullptr);
                    }
                    if (savePng (strip, folder.getChildFile ("metronome_dots" + suffix)))
                        log << "wrote metronome_dots" << suffix << "\n";
                }

                // 大きな表示 (ドットをクリックしたとき)。7/8 の3拍目
                {
                    MetronomeBigWindow big;
                    big.getDots().setState (7, 8, 2);
                    snap (big, 260 + 7 * 110, 220, "metronome_big" + suffix);
                }
            }

            // 設定の「文字の大きさ」と「アクセントの色」を変えたとき (ダーク、120%、青)
            {
                theme.setTextScale (1.2f);
                theme.setCustomAccent (ThemeManager::parseHex ("#3B82F6"));

                SessionScreen practice (engine, store, theme, appSettings, youtube, scoreViewer, session);
                snap (practice, 1280, 800, "practice_custom.png");

                // 再生バーの針: ふだん / バーの上 / 針の近く (縦に並べて1枚にする)
                {
                    juce::Image strip (juce::Image::ARGB, 700, 5 * 50, true);
                    {
                        juce::Graphics g (strip);
                        g.fillAll (theme.getPalette().background);
                        TimelineView bar;
                        bar.setSize (700, 44);
                        bar.setState (95.0, 240.0, 60.0, 120.0, true);
                        const std::pair<bool, bool> states[] { { false, false }, { true, false }, { true, true } };
                        for (int i = 0; i < 3; ++i)
                        {
                            bar.setTestHover (states[i].first, states[i].second);
                            g.drawImageAt (bar.createComponentSnapshot (bar.getLocalBounds(), true, 1.0f), 0, i * 50);
                        }

                        // メトロノームの開始位置の目印 / 拡大モード (ダブルクリックして押し続けたとき)
                        bar.setTestHover (false, false);
                        bar.setMarker (93.25);
                        g.drawImageAt (bar.createComponentSnapshot (bar.getLocalBounds(), true, 1.0f), 0, 3 * 50);
                        bar.setTestZoom (93.875);
                        g.drawImageAt (bar.createComponentSnapshot (bar.getLocalBounds(), true, 1.0f), 0, 4 * 50);
                    }
                    if (savePng (strip, folder.getChildFile ("timeline_states.png")))
                        log << "wrote timeline_states.png\n";
                }

                // TONE3000 を選んだとき (このPCにはまだ入っていないので、ダウンロードの案内が出る)
                {
                    Session pluginSession = session;
                    pluginSession.tonePresetId = pluginTonePresetId;
                    SessionScreen pluginPractice (engine, store, theme, appSettings, youtube, scoreViewer, pluginSession);
                    snap (pluginPractice, 1280, 800, "practice_tone3000.png");
                }

                // チューナーを開いたとき (後ろが暗くなる)
                practice.perform (Command::toggleTuner);
                snap (practice, 1280, 800, "practice_tuner.png");
                practice.perform (Command::toggleTuner);

                // 音色の設定 (NAM)。ファイル名だけ入れる (ファイルは読まない)
                {
                    Session toneSession = session;
                    toneSession.tonePresetId = namTonePresetId;
                    toneSession.namModelPath = "C:/Models/Marsh 800_AgroTS9.nam";
                    toneSession.irPath = "C:/IRs/BD_RH_GatesOfHell.wav";
                    ToneDetailPanel tonePanel (engine, toneSession);
                    snap (tonePanel, ToneDetailPanel::preferredWidth, ToneDetailPanel::preferredHeight, "tone_panel.png");

                    // VST プラグインのとき: ノイズゲートは「オフ (VST)」で使えない
                    Session pluginSession = toneSession;
                    pluginSession.tonePresetId = pluginTonePresetId;
                    pluginSession.noiseGateDb = -50.0;
                    ToneDetailPanel pluginPanel (engine, pluginSession);
                    snap (pluginPanel, ToneDetailPanel::preferredWidth, ToneDetailPanel::preferredHeight, "tone_panel_vst.png");
                }

                SettingsPanel panel (appSettings, theme, engine.getDeviceManager());
                snap (panel, panel.getWidth(), panel.getHeight(), "settings_custom.png");

                // 動作ログの画面: 見本のログ (作者のログは読まない)
                {
                    const auto sampleLog = sampleFolder.getChildFile ("app.log");
                    sampleLog.replaceWithText ("2026-11-05 09:32:51 起動 (0.1.0)\n2026-11-05 09:33:37 画面: 練習画面\n"
                                               "2026-11-05 09:33:40 プラグインを読み込んだ\n"
                                               "2026-11-05 09:40:02 !! 落ちました: 例外 0xc0000005 場所 Archetype Misha Mansoor X.vst3 + 0x5c26ce\n"
                                               "2026-11-05 09:40:02 呼び出しの流れ:\n0: suguhiku: juce::SystemStats::getStackBacktrace + 0x86\n"
                                               "2026-11-05 09:41:10 !! 前回は正しく終了していません (落ちたか、強制終了された)。最後の記録: !! 落ちました: …\n"
                                               "2026-11-05 09:41:10 起動 (0.1.0)\n"_jp);
                    AppLog::fileOverride() = sampleLog;
                    DebugLogPanel debugPanel;
                    snap (debugPanel, DebugLogPanel::preferredWidth, DebugLogPanel::preferredHeight, "debug_log.png");
                    AppLog::fileOverride() = juce::File();
                }

                theme.setTextScale (1.0f);
                theme.setCustomAccent (std::nullopt);
            }
        }

        // 細部の確認用に、いくつかの文言を大きく描く
        {
            const MetalLogoParams params;
            const juce::Font font (juce::FontOptions (theme.getMetalTypeface()));
            const juce::StringArray texts { getFixedText (FixedTextId::DeathMetalMode), getFixedText (FixedTextId::Play),
                                            getFixedText (FixedTextId::Metronome), getFixedText (FixedTextId::NewSession) };
            constexpr int rowHeight = 320, width = 1800;
            juce::Image detail (juce::Image::RGB, width, rowHeight * texts.size(), true);
            {
                // Windows の JUCE 8 では Image は Direct2D の画像で、Graphics を消したときに書き込まれる
                juce::Graphics g (detail);
                g.fillAll (Palette::deathMetal().background);

                for (int i = 0; i < texts.size(); ++i)
                {
                    const auto logo = MetalLogoGenerator::generate (texts[i], font, params, MetalLogoGenerator::seedFor (texts[i]));
                    g.setColour (Palette::deathMetal().logoText);
                    g.fillPath (logo, juce::RectanglePlacement (juce::RectanglePlacement::centred)
                                          .getTransformToFit (MetalLogoGenerator::getLogoArea (logo, params),
                                                              juce::Rectangle<int> (0, i * rowHeight, width, rowHeight).reduced (10).toFloat()));
                }
            }

            const auto detailFile = folder.getChildFile ("logos_detail.png");
            if (savePng (detail, detailFile))
                log << "wrote " << detailFile.getFullPathName() << "\n";
        }

        // 延長 (Step 3) だけの確認用: 元の形を赤、延長後の輪郭を白い線で重ねる
        {
            MetalLogoParams plain, extended;
            plain.tipExtend = 0.0f;
            plain.spikeProbability = 0.0f;
            extended.spikeProbability = 0.0f;
            plain.margin = extended.margin = 0.0f;

            const juce::Font font (juce::FontOptions (theme.getMetalTypeface()));
            const juce::StringArray texts { "新再生"_jp, "セッション"_jp, "デスメタル"_jp };
            constexpr int rowHeight = 360, width = 1500;
            juce::Image overlay (juce::Image::RGB, width, rowHeight * texts.size(), true);
            {
                juce::Graphics g (overlay);
                g.fillAll (juce::Colours::black);

                for (int i = 0; i < texts.size(); ++i)
                {
                    const auto seed = MetalLogoGenerator::seedFor (texts[i]);
                    auto a = MetalLogoGenerator::generate (texts[i], font, plain, seed);
                    auto b = MetalLogoGenerator::generate (texts[i], font, extended, seed);

                    // 延長で外接矩形が広がるので、元の位置を合わせ直す (どちらも左上が原点になっている)
                    const auto cell = juce::Rectangle<int> (0, i * rowHeight, width, rowHeight).reduced (10).toFloat();
                    const auto t = juce::RectanglePlacement (juce::RectanglePlacement::centred).getTransformToFit (b.getBounds(), cell);
                    a.applyTransform (juce::AffineTransform::translation (b.getBounds().getX() - a.getBounds().getX(), 0.0f));

                    g.setColour (juce::Colours::red.withAlpha (0.6f));
                    g.fillPath (a, t);
                    g.setColour (juce::Colours::white);
                    g.strokePath (b, juce::PathStrokeType (1.5f), t);
                }
            }

            const auto overlayFile = folder.getChildFile ("logos_extend.png");
            if (savePng (overlay, overlayFile))
                log << "wrote " << overlayFile.getFullPathName() << "\n";
        }

        const auto sheetFile = folder.getChildFile ("logos.png");
        if (savePng (renderLogoSheet (theme, log), sheetFile))
            log << "wrote " << sheetFile.getFullPathName() << "\n";
    }

    folder.getChildFile ("log.txt").replaceWithText (log);
    return log;
}

#endif
