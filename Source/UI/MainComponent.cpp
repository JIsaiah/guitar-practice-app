#include "MainComponent.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include "../Text.h"
#include "SessionListScreen.h"
#include "SessionScreen.h"
#include "SessionSetupScreen.h"

MainComponent::MainComponent (AudioEngine& e, SessionStore& s)
    : engine (e), store (s)
{
    setSize (1280, 800);
    showList();
}

void MainComponent::resized()
{
    if (currentScreen != nullptr)
        currentScreen->setBounds (getLocalBounds());
}

//==============================================================================
void MainComponent::showList()
{
    auto screen = std::make_unique<SessionListScreen> (store);

    screen->onNewSession = [this] { navigate ([] (MainComponent& m) { m.showSetup(); }); };
    screen->onOpenSettings = [this] { openAudioSettings(); };
    screen->onOpenSession = [this] (const juce::String& id)
    {
        navigate ([id] (MainComponent& m)
        {
            if (auto session = m.store.load (id))
                m.showSession (std::move (*session));
            else
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                        "開けませんでした"_jp,
                                                        "セッションファイルが壊れているか、見つかりません。"_jp);
        });
    };

    setScreen (std::move (screen));
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
    auto screen = std::make_unique<SessionScreen> (engine, store, std::move (session));

    screen->onBackToList = [this] { navigate ([] (MainComponent& m) { m.showList(); }); };
    screen->onOpenSettings = [this] { openAudioSettings(); };

    setScreen (std::move (screen));
}

void MainComponent::openAudioSettings()
{
    auto* selector = new juce::AudioDeviceSelectorComponent (engine.getDeviceManager(),
                                                             0, 2,     // 入力チャンネル数 (最小, 最大)
                                                             2, 2,     // 出力チャンネル数
                                                             false, false,
                                                             true, false);
    selector->setSize (560, 440);

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (selector);
    options.dialogTitle = "オーディオ設定"_jp;
    options.dialogBackgroundColour = getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId);
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.componentToCentreAround = this;
    options.launchAsync();
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
