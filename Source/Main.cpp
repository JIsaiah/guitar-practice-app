#include <juce_gui_extra/juce_gui_extra.h>
#include "Audio/AudioEngine.h"
#include "Model/SessionStore.h"
#include "UI/MainComponent.h"

class GuitarPracticeApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override          { return false; }

    void initialise (const juce::String&) override
    {
        juce::LookAndFeel::getDefaultLookAndFeel().setDefaultSansSerifTypefaceName ("Yu Gothic UI");

        juce::PropertiesFile::Options options;
        options.applicationName = "GuitarPractice";
        options.folderName = "GuitarPractice";
        options.filenameSuffix = ".settings";
        options.osxLibrarySubFolder = "Application Support";
        properties.setStorageParameters (options);

        audioEngine = std::make_unique<AudioEngine>();
        audioEngine->initialise (properties.getUserSettings()->getXmlValue (audioDeviceKey).get());

        sessionStore = std::make_unique<SessionStore>();
        mainWindow = std::make_unique<MainWindow> (getApplicationName(), *audioEngine, *sessionStore);
    }

    void shutdown() override
    {
        mainWindow = nullptr;

        if (auto state = audioEngine->createDeviceState())
            properties.getUserSettings()->setValue (audioDeviceKey, state.get());

        properties.saveIfNeeded();
        audioEngine = nullptr;
        sessionStore = nullptr;
    }

    void systemRequestedQuit() override { quit(); }
    void anotherInstanceStarted (const juce::String&) override {}

private:
    class MainWindow final : public juce::DocumentWindow
    {
    public:
        MainWindow (const juce::String& name, AudioEngine& engine, SessionStore& store)
            : DocumentWindow (name,
                              juce::Desktop::getInstance().getDefaultLookAndFeel()
                                  .findColour (juce::ResizableWindow::backgroundColourId),
                              DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new MainComponent (engine, store), true);
            setResizable (true, true);
            setResizeLimits (1200, 700, 10000, 10000);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }

        void closeButtonPressed() override
        {
            JUCEApplication::getInstance()->systemRequestedQuit();
        }

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };

    static constexpr const char* audioDeviceKey = "audioDevice";

    juce::ApplicationProperties properties;
    std::unique_ptr<AudioEngine> audioEngine;
    std::unique_ptr<SessionStore> sessionStore;
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (GuitarPracticeApplication)
