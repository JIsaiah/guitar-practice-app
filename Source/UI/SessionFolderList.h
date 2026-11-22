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

    // キーボード操作 (一覧画面の矢印・Enter。SessionListScreen が使う)
    int getNumItems() const noexcept { return (int) items.size(); }
    juce::String getItemKey (int index) const { return juce::isPositiveAndBelow (index, (int) items.size()) ? items[(size_t) index].getKey() : juce::String(); }
    juce::Rectangle<int> getItemBounds (int index) const;   // このリストの中の座標
    juce::Rectangle<int> getListArea() const { return list.getBounds(); }
    void scrollToItem (int index) { list.scrollToEnsureRowIsOnscreen (index); }
    void chooseItem (int index) { list.selectRow (index); }   // クリックと同じ (絞り込む)

    // キーボード操作のハイライト (作者の指示: 枠をやめ、後ろのハイライトだけで今の場所を見せる)。
    // -2: キーボードで操作していない (選んでいるフォルダを塗る)。-1: 操作中だがフォルダの上にいない (どれも塗らない)。
    // 0 以上: そのフォルダだけを塗る (選んでいるフォルダの塗りは消す)
    void setKeyboardHighlight (int index)
    {
        if (index != keyboardIndex)
        {
            keyboardIndex = index;
            list.repaint();
        }
    }
    juce::Button& getNewButton() noexcept { return newButton; }

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
    int keyboardIndex = -2;   // setKeyboardHighlight
};
