#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Library/SessionFolders.h"

// 一覧画面の左の列: フォルダ (すべて／各フォルダ／フォルダなし) と件数。
// クリックで絞り込み、フォルダの右クリックで「名前の変更」「削除」。下に「＋ 新しいフォルダ」
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

    std::vector<SessionFolders::Item> items;
    juce::ListBox list { {}, this };
    juce::TextButton newButton;
    bool updating = false;
};
