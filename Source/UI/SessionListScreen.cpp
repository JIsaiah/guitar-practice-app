#include "SessionListScreen.h"
#include "../Text.h"

SessionListScreen::SessionListScreen (SessionStore& s, ThemeManager& t)
    : store (s), theme (t),
      appTitle (t, FixedTextId::AppTitle), heading (t, FixedTextId::Sessions),
      newButton (t), openButton (t), deleteButton (t), settingsButton (t),
      deathMetalToggle (t, FixedTextId::DeathMetalMode)
{
    appTitle.setFont (juce::FontOptions (24.0f, juce::Font::bold));
    heading.setFont (juce::FontOptions (24.0f, juce::Font::bold));
    addAndMakeVisible (appTitle);
    addAndMakeVisible (heading);

    listBox.setRowHeight (48);
    addAndMakeVisible (listBox);

    newButton.setFixedText (FixedTextId::NewSession, "+ 新しいセッション"_jp);
    openButton.setFixedText (FixedTextId::Open, "開く"_jp);
    deleteButton.setFixedText (FixedTextId::Delete, "削除"_jp);
    settingsButton.setFixedText (FixedTextId::Settings, "⚙ 設定"_jp);

    newButton.onClick = [this] { if (onNewSession != nullptr) onNewSession(); };
    openButton.onClick = [this] { openSelected(); };
    deleteButton.onClick = [this] { deleteSelected(); };
    settingsButton.onClick = [this] { if (onOpenSettings != nullptr) onOpenSettings(); };

    for (auto* button : { &newButton, &openButton, &deleteButton, &settingsButton })
        addAndMakeVisible (button);

    // デスメタルモードの切り替え (death-metal-mode.md 4.7)。状態はアプリの設定に保存される
    deathMetalToggle.setToggleState (theme.isDeathMetal(), juce::dontSendNotification);
    deathMetalToggle.onClick = [this]
    {
        theme.setMode (deathMetalToggle.getToggleState() ? ThemeMode::DeathMetal : ThemeMode::Normal);
    };
    addAndMakeVisible (deathMetalToggle);

    // テーマの切り替え (設計書 v0.7 4.7)。設定画面でも変えられる
    for (auto mode : { ColourTheme::System, ColourTheme::Light, ColourTheme::Dark })
        themeBox.addItem ("テーマ: "_jp + ThemeManager::getColourThemeName (mode), (int) mode + 1);
    themeBox.setSelectedId ((int) theme.getColourTheme() + 1, juce::dontSendNotification);
    themeBox.onChange = [this] { theme.setColourTheme ((ColourTheme) (themeBox.getSelectedId() - 1)); };
    themeBox.setTooltip ("テーマ"_jp);
    addAndMakeVisible (themeBox);
    theme.addChangeListener (this);

    refresh();
}

SessionListScreen::~SessionListScreen()
{
    theme.removeChangeListener (this);
}

void SessionListScreen::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // 設定画面でテーマを変えたときに、表示を合わせる
    themeBox.setSelectedId ((int) theme.getColourTheme() + 1, juce::dontSendNotification);
    deathMetalToggle.setToggleState (theme.isDeathMetal(), juce::dontSendNotification);
    themeBox.setEnabled (! theme.isDeathMetal());   // デスメタルモード中は、配色はデスメタルのまま
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
        g.fillAll (listBox.findColour (ThemeColourIds::listSelection));

    const auto textColour = listBox.findColour (juce::ListBox::textColourId);
    g.setColour (textColour);
    g.setFont (AppLookAndFeel::scaled (juce::FontOptions (17.0f)));
    g.drawText (entry.title.isEmpty() ? "(無題)"_jp : entry.title,
                12, 0, width - 220, height, juce::Justification::centredLeft);

    g.setColour (textColour.withAlpha (0.5f));
    g.setFont (AppLookAndFeel::scaled (juce::FontOptions (13.0f)));
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

    g.setColour (listBox.findColour (juce::ListBox::textColourId).withAlpha (0.6f));
    g.setFont (AppLookAndFeel::scaled (juce::FontOptions (16.0f)));
    g.drawFittedText ("セッションがありません。\n「+ 新しいセッション」から作成してください。"_jp,
                      listBox.getBounds(), juce::Justification::centred, 2);
}

void SessionListScreen::resized()
{
    auto area = getLocalBounds().reduced (24);

    auto top = area.removeFromTop (44);
    settingsButton.setBounds (top.removeFromRight (140).reduced (0, 6));
    top.removeFromRight (12);
    deathMetalToggle.setBounds (top.removeFromRight (190).reduced (0, 6));
    top.removeFromRight (8);
    themeBox.setBounds (top.removeFromRight (230).reduced (0, 7));
    appTitle.setBounds (top.removeFromLeft (220));
    top.removeFromLeft (16);
    heading.setBounds (top.removeFromLeft (juce::jmin (200, top.getWidth())));   // ロゴは枠の中央に描くので、幅は文字に合わせて決める

    area.removeFromTop (12);
    auto buttons = area.removeFromBottom (48);
    newButton.setBounds (buttons.removeFromLeft (220));
    buttons.removeFromLeft (12);
    openButton.setBounds (buttons.removeFromLeft (140));
    deleteButton.setBounds (buttons.removeFromRight (100));

    area.removeFromBottom (12);
    listBox.setBounds (area);
}
