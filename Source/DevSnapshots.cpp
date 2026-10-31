#include "DevSnapshots.h"

#if JUCE_DEBUG

#include "Model/SessionStore.h"
#include "Theme/ThemeManager.h"
#include "Theme/MetalLogo/FixedTexts.h"
#include "Theme/MetalLogo/MetalLogoGenerator.h"
#include "UI/SessionListScreen.h"
#include "UI/SessionScreen.h"
#include "UI/SettingsPanel.h"
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
        SessionStore store;   // 一覧を読むだけ

        for (auto mode : { ThemeMode::Normal, ThemeMode::DeathMetal })
        {
            theme.setMode (mode);

            SessionListScreen screen (store, theme);
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

            for (auto colour : { ColourTheme::Light, ColourTheme::Dark })
            {
                theme.setColourTheme (colour);
                const juce::String suffix = colour == ColourTheme::Light ? "_light.png" : "_dark.png";

                SessionListScreen list (store, theme);
                snap (list, 1280, 800, "list" + suffix);

                SessionScreen practice (engine, store, theme, appSettings, youtube, scoreViewer, session);
                snap (practice, 1280, 800, "practice" + suffix);

                SettingsPanel panel (appSettings, theme, engine.getDeviceManager());
                snap (panel, panel.getWidth(), panel.getHeight(), "settings" + suffix);

                TunerPanel tuner (engine);
                tuner.setTestFrequency (112.0);   // A2 (110Hz) より少し高い
                snap (tuner, TunerPanel::preferredWidth, TunerPanel::preferredHeight, "tuner_sharp" + suffix);
                tuner.setTestFrequency (110.2);   // ほぼ合っている
                snap (tuner, TunerPanel::preferredWidth, TunerPanel::preferredHeight, "tuner_ok" + suffix);
            }

            // 設定の「文字の大きさ」と「アクセントの色」を変えたとき (ダーク、120%、青)
            {
                theme.setTextScale (1.2f);
                theme.setCustomAccent (ThemeManager::parseHex ("#3B82F6"));

                SessionScreen practice (engine, store, theme, appSettings, youtube, scoreViewer, session);
                snap (practice, 1280, 800, "practice_custom.png");

                // 再生バーの針: ふだん / バーの上 / 針の近く (縦に並べて1枚にする)
                {
                    juce::Image strip (juce::Image::ARGB, 700, 3 * 50, true);
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
                    }
                    if (savePng (strip, folder.getChildFile ("timeline_states.png")))
                        log << "wrote timeline_states.png\n";
                }

                // チューナーを開いたとき (後ろが暗くなる)
                practice.perform (Command::toggleTuner);
                snap (practice, 1280, 800, "practice_tuner.png");
                practice.perform (Command::toggleTuner);

                SettingsPanel panel (appSettings, theme, engine.getDeviceManager());
                snap (panel, panel.getWidth(), panel.getHeight(), "settings_custom.png");

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
