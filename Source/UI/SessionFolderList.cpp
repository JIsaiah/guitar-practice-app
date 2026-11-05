#include "SessionFolderList.h"
#include "../Theme/DeathMetalLookAndFeel.h"

SessionFolderList::SessionFolderList()
{
    list.setRowHeight (36);
    list.setTooltip ("クリックで、そのフォルダのセッションだけを出します。フォルダの右クリックで名前の変更・削除"_jp);
    addAndMakeVisible (list);

    newButton.setButtonText ("＋ 新しいフォルダ"_jp);
    newButton.onClick = [this] { if (onNewFolder != nullptr) onNewFolder(); };
    addAndMakeVisible (newButton);
}

void SessionFolderList::setItems (std::vector<SessionFolders::Item> newItems, const juce::String& selectedKey)
{
    items = std::move (newItems);

    // 選び直しの知らせ (selectedRowsChanged) で onSelect を呼ばないよう、updating の間は無視する
    const juce::ScopedValueSetter<bool> guard (updating, true);
    list.updateContent();

    int row = 0;
    for (int i = 0; i < (int) items.size(); ++i)
        if (items[(size_t) i].getKey() == selectedKey)
            row = i;

    list.selectRow (row);
    list.repaint();
}

void SessionFolderList::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (! juce::isPositiveAndBelow (row, (int) items.size()))
        return;

    const auto& item = items[(size_t) row];
    if (selected)
        g.fillAll (list.findColour (ThemeColourIds::listSelection));

    const auto textColour = list.findColour (juce::ListBox::textColourId);
    auto area = juce::Rectangle<int> (0, 0, width, height).reduced (10, 0);

    g.setColour (textColour.withAlpha (0.6f));
    g.setFont (AppLookAndFeel::scaled (juce::FontOptions (13.0f)));
    g.drawText (juce::String (item.count), area.removeFromRight (36), juce::Justification::centredRight);

    // 「すべて」「フォルダなし」は、名前のフォルダと区別がつくよう、少し薄く・太く
    const bool special = item.kind != SessionFolders::Item::Kind::named;
    g.setColour (special ? textColour.withAlpha (0.85f) : textColour);
    g.setFont (AppLookAndFeel::scaled (juce::FontOptions (15.0f, special ? juce::Font::bold : juce::Font::plain)));
    if (! special)
    {
        // フォルダの形 (つまみのある四角)
        auto icon = area.removeFromLeft (18).withSizeKeepingCentre (16, 12).toFloat();
        juce::Path folder;
        folder.startNewSubPath (icon.getX(), icon.getBottom());
        folder.lineTo (icon.getX(), icon.getY());
        folder.lineTo (icon.getX() + 6.0f, icon.getY());
        folder.lineTo (icon.getX() + 8.0f, icon.getY() + 2.0f);
        folder.lineTo (icon.getRight(), icon.getY() + 2.0f);
        folder.lineTo (icon.getRight(), icon.getBottom());
        folder.closeSubPath();
        g.setColour (list.findColour (ThemeColourIds::accent).withAlpha (0.85f));
        g.fillPath (folder);
        area.removeFromLeft (6);
        g.setColour (textColour);
    }

    g.drawText (item.getLabel(), area, juce::Justification::centredLeft, true);
}

void SessionFolderList::selectedRowsChanged (int row)
{
    if (! updating && juce::isPositiveAndBelow (row, (int) items.size()) && onSelect != nullptr)
        onSelect (items[(size_t) row].getKey());
}

void SessionFolderList::listBoxItemClicked (int row, const juce::MouseEvent& e)
{
    if (! e.mods.isPopupMenu() || ! juce::isPositiveAndBelow (row, (int) items.size()))
        return;

    const auto item = items[(size_t) row];
    if (item.kind != SessionFolders::Item::Kind::named)
        return;   // 「すべて」「フォルダなし」は変えられない

    juce::PopupMenu menu;
    menu.addItem (1, "名前の変更…"_jp);
    menu.addItem (2, "削除 (中のセッションは消しません)"_jp);
    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition(),
                        [safe = juce::Component::SafePointer (this), name = item.name] (int result)
    {
        if (safe == nullptr)
            return;
        if (result == 1 && safe->onRename != nullptr)
            safe->onRename (name);
        if (result == 2 && safe->onDelete != nullptr)
            safe->onDelete (name);
    });
}

void SessionFolderList::paint (juce::Graphics& g)
{
    g.setColour (findColour (ThemeColourIds::border));
    g.drawRoundedRectangle (list.getBounds().toFloat().expanded (0.5f), 4.0f, 1.0f);
}

void SessionFolderList::resized()
{
    auto area = getLocalBounds();
    newButton.setBounds (area.removeFromBottom (36));
    area.removeFromBottom (8);
    list.setBounds (area);
}
