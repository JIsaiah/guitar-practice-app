#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Model/SessionStore.h"

// セットリストに曲を追加するパネル (setlist-mode.md 4.4)
// - セッションをクリックするたびに、末尾に1件追加する (同じものを何度押しても追加される)
// - 開いたままにして続けて追加できる。「閉じる」で終わる
// - 追加した回数を「追加済み×2」のように出す
class SessionPickerPanel final : public juce::Component,
                                 private juce::ListBoxModel
{
public:
    SessionPickerPanel();

    // 一覧と、追加先のセットリストの名前・各セッションが入っている回数
    void setSessions (std::vector<SessionStore::Entry> sessions);
    void setTarget (const juce::String& setlistName, std::function<int (const juce::String& sessionId)> countInSetlist);
    void refreshCounts() { listBox.repaint(); }

    std::function<void (const juce::String& sessionId)> onAdd;
    std::function<void()> onClose;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    int getNumRows() override { return (int) sessions.size(); }
    void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;

    std::vector<SessionStore::Entry> sessions;
    juce::String targetName;
    std::function<int (const juce::String&)> countOf;

    juce::ListBox listBox { {}, this };
    juce::TextButton closeButton;
};
