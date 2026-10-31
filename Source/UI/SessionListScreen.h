#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Model/SessionStore.h"

// 起動直後の画面。保存済みのセッションを選んで開く (設計書 4.1 の 1)
class SessionListScreen final : public juce::Component,
                                private juce::ListBoxModel
{
public:
    explicit SessionListScreen (SessionStore& store);

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

    void refresh();
    void updateButtons();
    void openSelected();
    void deleteSelected();

    SessionStore& store;
    std::vector<SessionStore::Entry> entries;

    juce::Label heading;
    juce::ListBox listBox { {}, this };
    juce::TextButton newButton, openButton, deleteButton, settingsButton;
};
