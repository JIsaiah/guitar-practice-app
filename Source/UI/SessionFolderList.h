#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Library/SessionFolders.h"
#include "Icons.h"

// 一覧画面の左の列: フォルダ (すべて／各フォルダ／フォルダなし) と件数。
// クリックで絞り込み、フォルダの右クリックで「名前の変更」「削除」。
// 枠の中の上に見出し「フォルダー」、右下に「＋」(新しいフォルダ)
class SessionFolderList final : public juce::Component,
                                private juce::ListBoxModel
{
public:
    SessionFolderList();

    void setItems (std::vector<SessionFolders::Item> newItems, const juce::String& selectedKey);

    std::function<void (const juce::String& key)> onSelect;
    std::function<void()> onNewFolder;
    std::function<void (const juce::String& name)> onRename;
    std::function<void (const juce::String& name)> onDelete;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    int getNumRows() override { return (int) items.size(); }
    void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent& e) override;
    void selectedRowsChanged (int row) override;

    juce::Rectangle<int> getHeadingArea() const;
    static constexpr int headingHeight = 32;   // 一覧の列の見出しと同じ高さ (SessionListScreen)

    std::vector<SessionFolders::Item> items;
    juce::ListBox list { {}, this };
    IconButton newButton { Icons::plus };   // 「＋」(新しいフォルダ)
    bool updating = false;
};
