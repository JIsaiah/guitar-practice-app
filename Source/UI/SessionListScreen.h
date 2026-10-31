#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Model/SessionStore.h"
#include "../Theme/MetalLogo/FixedTextButton.h"

// 起動直後の画面。保存済みのセッションを選んで開く (設計書 4.1 の 1)
class SessionListScreen final : public juce::Component,
                                private juce::ListBoxModel,
                                private juce::ChangeListener
{
public:
    SessionListScreen (SessionStore& store, ThemeManager& theme);
    ~SessionListScreen() override;

    std::function<void()> onNewSession;
    std::function<void (const juce::String& sessionId)> onOpenSession;
    std::function<void()> onOpenSettings;

    void paint (juce::Graphics& g) override;
    void paintOverChildren (juce::Graphics& g) override;
    void resized() override;

private:
    // ListBoxModel
    int getNumRows() override { return (int) entries.size(); }
    void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
    void selectedRowsChanged (int) override { updateButtons(); }

    void changeListenerCallback (juce::ChangeBroadcaster*) override;   // テーマが変わったとき
    void refresh();
    void updateButtons();
    void openSelected();
    void deleteSelected();

    SessionStore& store;
    ThemeManager& theme;
    std::vector<SessionStore::Entry> entries;

    MetalLogoLabel appTitle, heading;
    juce::ListBox listBox { {}, this };
    FixedTextButton newButton, openButton, deleteButton, settingsButton;
    FixedTextToggle deathMetalToggle;
    juce::ComboBox themeBox;   // テーマ (Windows に合わせる／ライト／ダーク)

};
