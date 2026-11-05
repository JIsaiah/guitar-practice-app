#include <juce_gui_extra/juce_gui_extra.h>
#include "Audio/AudioEngine.h"
#include "AppLog.h"
#include "Platform/CrashLog.h"
#include "AppSettings.h"
#include "DevAudioCheck.h"
#include "DevSnapshots.h"
#include "Library/SessionLibrary.h"
#include "Library/SetlistStore.h"
#include "Theme/ThemeManager.h"
#include "UI/MainComponent.h"

class GuitarPracticeApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override          { return false; }

    void initialise (const juce::String& commandLine) override
    {
       #if JUCE_DEBUG
        // 開発用の起動では、作者の動作ログに書かない
        if (commandLine.contains (DevSnapshots::commandLineFlag) || commandLine.contains (DevAudioCheck::pluginWindowTestFlag)
            || commandLine.contains (DevAudioCheck::commandLineFlag))
            AppLog::setEnabled (false);

        // 開発用: わざと落ちて、落ちた場所が動作ログに書かれるかを確かめる。ログは指定のフォルダに書く
        if (commandLine.contains ("--crash-test"))
        {
            AppLog::fileOverride() = juce::File (commandLine.fromFirstOccurrenceOf ("--crash-test", false, false).trim().unquoted())
                                         .getChildFile ("app.log");
            AppLog::setEnabled (true);
            AppLog::noteStart (getApplicationVersion());
            CrashLog::install();
            juce::MessageManager::callAsync ([]
            {
                AppLog::write ("わざと落ちます"_jp);
                volatile int* nowhere = nullptr;
                *nowhere = 1;
            });
            return;
        }

        // 開発用: 画面とロゴを PNG に書き出して終わる (DevSnapshots.h)
        if (commandLine.contains (DevSnapshots::commandLineFlag))
        {
            DevSnapshots::renderAll (juce::File (commandLine.fromFirstOccurrenceOf (DevSnapshots::commandLineFlag, false, false)
                                                            .trim().unquoted()));
            quit();
            return;
        }

        // 開発用: プラグインの画面のウィンドウが開くかを確かめて終わる (DevAudioCheck.h)
        if (commandLine.contains (DevAudioCheck::pluginWindowTestFlag))
        {
            devTest = DevAudioCheck::startPluginWindowTest (commandLine, [this] { quit(); });
            return;
        }

        // 開発用: メトロノームのクリックがいつ鳴るかを数えて終わる (DevAudioCheck.h)
        if (commandLine.contains (DevAudioCheck::metronomeCheckFlag))
        {
            AppLog::setEnabled (false);
            DevAudioCheck::runMetronomeCheck (commandLine);
            quit();
            return;
        }

        // 開発用: オーディオデバイスの一覧と、NAM の処理時間を確かめて終わる (DevAudioCheck.h)
        if (commandLine.contains (DevAudioCheck::commandLineFlag))
        {
            DevAudioCheck::run (commandLine);
            quit();
            return;
        }
       #else
        juce::ignoreUnused (commandLine);
       #endif

        // 動作ログ: 前回が正しく終わったかを記録し、落ちたときに場所を書くようにする
        AppLog::noteStart (getApplicationVersion());
        CrashLog::install();

        juce::PropertiesFile::Options options;
        options.applicationName = "GuitarPractice";
        options.folderName = "GuitarPractice";
        options.filenameSuffix = ".settings";
        options.osxLibrarySubFolder = "Application Support";
        properties.setStorageParameters (options);

        // 見た目 (通常／デスメタル)。既定のフォント「Yu Gothic UI」の設定も、通常モードの LookAndFeel が持つ
        themeManager = std::make_unique<ThemeManager> (*properties.getUserSettings());
        appSettings = std::make_unique<AppSettings> (*properties.getUserSettings());

        audioEngine = std::make_unique<AudioEngine>();
        audioEngine->initialise (properties.getUserSettings()->getXmlValue (audioDeviceKey).get());

        sessionStore = std::make_unique<SessionStore>();
        sessionLibrary = std::make_unique<SessionLibrary> (*sessionStore);   // 走査は一覧画面を出すときに行う
        setlistStore = std::make_unique<SetlistStore>();
        mainWindow = std::make_unique<MainWindow> (getApplicationName(), *audioEngine, *sessionLibrary, *setlistStore,
                                                   *themeManager, *appSettings);
    }

    void shutdown() override
    {
        devTest = nullptr;   // 開発用のテスト (--plugin-window-test) で起動したとき
        mainWindow = nullptr;

        if (audioEngine == nullptr)   // --render-logos で起動したとき
            return;

        if (auto state = audioEngine->createDeviceState())
            properties.getUserSettings()->setValue (audioDeviceKey, state.get());

        properties.saveIfNeeded();
        audioEngine = nullptr;   // プラグインもここで消える (落ちるならここ。だから「終了」はこの後に書く)
        setlistStore = nullptr;
        sessionLibrary = nullptr;
        sessionStore = nullptr;
        appSettings = nullptr;
        themeManager = nullptr;   // 画面がすべて消えてから、LookAndFeel を消す

        AppLog::write (AppLog::endMarker());   // 最後まで来た印。次の起動で「正しく終了したか」を見る
    }

    void systemRequestedQuit() override
    {
        if (audioEngine != nullptr)
            AppLog::write ("終了の要求"_jp);
        quit();
    }
    void anotherInstanceStarted (const juce::String&) override {}

private:
    class MainWindow final : public juce::DocumentWindow
    {
    public:
        MainWindow (const juce::String& name, AudioEngine& engine, SessionLibrary& library, SetlistStore& setlists,
                    ThemeManager& theme, AppSettings& settings)
            : DocumentWindow (name,
                              juce::Desktop::getInstance().getDefaultLookAndFeel()
                                  .findColour (juce::ResizableWindow::backgroundColourId),
                              DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new MainComponent (engine, library, setlists, theme, settings), true);
            setResizable (true, true);
            setResizeLimits (1200, 700, 10000, 10000);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }

        void closeButtonPressed() override
        {
            AppLog::write ("アプリのウィンドウの × が押された"_jp);
            JUCEApplication::getInstance()->systemRequestedQuit();
        }

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };

    static constexpr const char* audioDeviceKey = "audioDevice";

    juce::ApplicationProperties properties;
    std::unique_ptr<AudioEngine> audioEngine;
    std::unique_ptr<SessionStore> sessionStore;
    std::unique_ptr<SessionLibrary> sessionLibrary;
    std::unique_ptr<SetlistStore> setlistStore;
    std::unique_ptr<ThemeManager> themeManager;
    std::unique_ptr<AppSettings> appSettings;
    std::unique_ptr<MainWindow> mainWindow;
    std::unique_ptr<juce::Timer> devTest;   // 開発用のテスト (Debug のみで使う)
};

START_JUCE_APPLICATION (GuitarPracticeApplication)
