#include "SessionListScreen.h"
#include "../Text.h"

SessionListScreen::SessionListScreen (SessionStore& s)
    : store (s)
{
    heading.setText ("セッション一覧"_jp, juce::dontSendNotification);
    heading.setFont (juce::FontOptions (24.0f, juce::Font::bold));
    addAndMakeVisible (heading);

    listBox.setRowHeight (48);
    addAndMakeVisible (listBox);

    newButton.setButtonText ("+ 新しいセッション"_jp);
    openButton.setButtonText ("開く"_jp);
    deleteButton.setButtonText ("削除"_jp);
    settingsButton.setButtonText ("オーディオ設定"_jp);

    newButton.onClick = [this] { if (onNewSession != nullptr) onNewSession(); };
    openButton.onClick = [this] { openSelected(); };
    deleteButton.onClick = [this] { deleteSelected(); };
    settingsButton.onClick = [this] { if (onOpenSettings != nullptr) onOpenSettings(); };

    for (auto* button : { &newButton, &openButton, &deleteButton, &settingsButton })
        addAndMakeVisible (button);

    refresh();
}

void SessionListScreen::refresh()
{
    entries = store.list();
    listBox.updateContent();
    listBox.deselectAllRows();
    if (! entries.empty())
        listBox.selectRow (0);

    updateButtons();
    repaint();
}

void SessionListScreen::updateButtons()
{
    const bool hasSelection = listBox.getSelectedRow() >= 0;
    openButton.setEnabled (hasSelection);
    deleteButton.setEnabled (hasSelection);
}

void SessionListScreen::openSelected()
{
    const auto row = listBox.getSelectedRow();
    if (juce::isPositiveAndBelow (row, (int) entries.size()) && onOpenSession != nullptr)
        onOpenSession (entries[(size_t) row].id);
}

void SessionListScreen::deleteSelected()
{
    const auto row = listBox.getSelectedRow();
    if (! juce::isPositiveAndBelow (row, (int) entries.size()))
        return;

    const auto entry = entries[(size_t) row];
    auto options = juce::MessageBoxOptions()
                       .withIconType (juce::MessageBoxIconType::QuestionIcon)
                       .withTitle ("セッションの削除"_jp)
                       .withMessage ("「"_jp + entry.title + "」を削除しますか？"_jp)
                       .withButton ("削除"_jp)
                       .withButton ("キャンセル"_jp)
                       .withAssociatedComponent (this);

    juce::AlertWindow::showAsync (options, [safe = juce::Component::SafePointer (this), id = entry.id] (int result)
    {
        if (safe != nullptr && result == 1)
        {
            safe->store.remove (id);
            safe->refresh();
        }
    });
}

void SessionListScreen::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (! juce::isPositiveAndBelow (row, (int) entries.size()))
        return;

    const auto& entry = entries[(size_t) row];

    if (selected)
        g.fillAll (juce::Colours::skyblue.withAlpha (0.25f));

    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (17.0f));
    g.drawText (entry.title.isEmpty() ? "(無題)"_jp : entry.title,
                12, 0, width - 220, height, juce::Justification::centredLeft);

    g.setColour (juce::Colours::white.withAlpha (0.5f));
    g.setFont (juce::FontOptions (13.0f));
    g.drawText (entry.modified.formatted ("%Y/%m/%d %H:%M"),
                width - 200, 0, 188, height, juce::Justification::centredRight);
}

void SessionListScreen::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
{
    listBox.selectRow (row);
    openSelected();
}

void SessionListScreen::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
}

void SessionListScreen::paintOverChildren (juce::Graphics& g)
{
    if (! entries.empty())
        return;

    g.setColour (juce::Colours::white.withAlpha (0.6f));
    g.setFont (juce::FontOptions (16.0f));
    g.drawFittedText ("セッションがありません。\n「+ 新しいセッション」から作成してください。"_jp,
                      listBox.getBounds(), juce::Justification::centred, 2);
}

void SessionListScreen::resized()
{
    auto area = getLocalBounds().reduced (24);

    auto top = area.removeFromTop (44);
    settingsButton.setBounds (top.removeFromRight (140).reduced (0, 6));
    heading.setBounds (top);

    area.removeFromTop (12);
    auto buttons = area.removeFromBottom (48);
    newButton.setBounds (buttons.removeFromLeft (220));
    buttons.removeFromLeft (12);
    openButton.setBounds (buttons.removeFromLeft (140));
    deleteButton.setBounds (buttons.removeFromRight (100));

    area.removeFromBottom (12);
    listBox.setBounds (area);
}
