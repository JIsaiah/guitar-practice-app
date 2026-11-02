#include "SessionListScreen.h"
#include "../Text.h"

namespace
{
    constexpr int tabRadioGroup = 0x5e7;
    constexpr int pickerWidth = 380;
}

SessionListScreen::SessionListScreen (SessionLibrary& l, SetlistStore& sl, ThemeManager& t, AppSettings& a)
    : library (l), store (l.getStore()), setlistStore (sl), theme (t), settings (a),
      appTitle (t, FixedTextId::AppTitle),
      sessionsTab (t), setlistsTab (t),
      newButton (t), openButton (t), deleteButton (t), settingsButton (t),
      deathMetalToggle (t, FixedTextId::DeathMetalMode),
      setlistPanel (l, sl, t, a)
{
    appTitle.setFont (juce::FontOptions (24.0f, juce::Font::bold));
    addAndMakeVisible (appTitle);

    // タブ (setlist-mode.md 4.1)。最後に開いていたタブを覚えておく
    sessionsTab.setFixedText (FixedTextId::Sessions, "セッション"_jp);
    setlistsTab.setFixedText (FixedTextId::Setlists, "セットリスト"_jp);
    for (auto* tab : { &sessionsTab, &setlistsTab })
    {
        tab->setClickingTogglesState (true);
        tab->setRadioGroupId (tabRadioGroup);
        addAndMakeVisible (tab);
    }
    sessionsTab.onClick = [this] { if (sessionsTab.getToggleState()) showTab (Tab::sessions); };
    setlistsTab.onClick = [this] { if (setlistsTab.getToggleState()) showTab (Tab::setlists); };

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

    // セットリストのタブ
    setlistPanel.onPlay = [this] (const juce::String& setlistId, int index)
    {
        if (onPlaySetlist != nullptr)
            onPlaySetlist (setlistId, index);
    };
    setlistPanel.onOpenSession = [this] (const juce::String& sessionId)
    {
        if (onOpenSessionById != nullptr)
            onOpenSessionById (sessionId);
    };
    setlistPanel.onRequestAdd = [this] (SetlistCard* card) { showPicker (card); };
    addChildComponent (setlistPanel);

    picker.onAdd = [this] (const juce::String& sessionId)
    {
        if (auto* card = setlistPanel.findCard (pickerTargetId))
            card->addSession (sessionId);
    };
    picker.onClose = [this] { showPicker (nullptr); };
    addChildComponent (picker);

    refresh();
    showTab (settings.getLibraryTab() == (int) Tab::setlists ? Tab::setlists : Tab::sessions);
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

void SessionListScreen::showTab (Tab tab)
{
    currentTab = tab;
    settings.setLibraryTab ((int) tab);

    const auto sessions = tab == Tab::sessions;
    sessionsTab.setToggleState (sessions, juce::dontSendNotification);
    setlistsTab.setToggleState (! sessions, juce::dontSendNotification);

    for (auto* c : std::initializer_list<juce::Component*> { &listBox, &newButton, &openButton, &deleteButton })
        c->setVisible (sessions);

    setlistPanel.setVisible (! sessions);
    if (sessions)
        showPicker (nullptr);

    resized();
    repaint();
}

void SessionListScreen::openFirstSetlistForSnapshot()
{
    if (auto* card = setlistPanel.getFirstCard())
    {
        card->setExpanded (true);
        showPicker (card);
    }
}

void SessionListScreen::refresh()
{
    // ファイル名の変更・古いファイルへの ID の付与・曲名の変更を、セットリストの表示にも反映する
    library.rescan();

    entries = store.list();
    listBox.updateContent();
    listBox.deselectAllRows();
    if (! entries.empty())
        listBox.selectRow (0);

    picker.setSessions (entries);
    updateButtons();
    repaint();
}

void SessionListScreen::showPicker (SetlistCard* card)
{
    if (auto* previous = setlistPanel.findCard (pickerTargetId))
        previous->setAdding (false);

    pickerTargetId = card != nullptr ? card->getSetlist().setlistId : juce::String();
    picker.setVisible (card != nullptr);

    if (card != nullptr)
    {
        card->setAdding (true);
        picker.setTarget (card->getSetlist().name, [this] (const juce::String& sessionId)
        {
            const auto* target = setlistPanel.findCard (pickerTargetId);
            return target != nullptr ? target->getSetlist().countOf (sessionId) : 0;
        });
    }

    resized();
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

    // セットリストで使われていれば、その数を伝える。消したあとの行はリンク切れとして残す (setlist-mode.md 4.6)
    auto message = "「"_jp + entry.title + "」を削除しますか？"_jp;
    const auto usedIn = entry.sessionId.isNotEmpty() ? setlistStore.countSetlistsUsing (entry.sessionId) : 0;
    if (usedIn > 0)
        message << "\n"
                << juce::String (usedIn) << "件のセットリストで使われています。"_jp
                << "\nセットリストの中では「見つかりません」と表示されます。"_jp;

    auto options = juce::MessageBoxOptions()
                       .withIconType (juce::MessageBoxIconType::QuestionIcon)
                       .withTitle ("セッションの削除"_jp)
                       .withMessage (message)
                       .withButton ("削除"_jp)
                       .withButton ("キャンセル"_jp)
                       .withAssociatedComponent (this);

    juce::AlertWindow::showAsync (options, [safe = juce::Component::SafePointer (this), entry] (int result)
    {
        if (safe != nullptr && result == 1)
        {
            safe->store.remove (entry.id);
            safe->library.noteRemoved (entry.sessionId);
            safe->refresh();
            safe->setlistPanel.refresh();   // リンク切れの表示と「通しで再生」を直す
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
    if (currentTab != Tab::sessions || ! entries.empty())
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
    sessionsTab.setBounds (top.removeFromLeft (150).reduced (0, 2));
    top.removeFromLeft (8);
    setlistsTab.setBounds (top.removeFromLeft (150).reduced (0, 2));

    area.removeFromTop (12);

    if (currentTab == Tab::setlists)
    {
        if (picker.isVisible())
        {
            picker.setBounds (area.removeFromRight (pickerWidth));
            area.removeFromRight (12);
        }
        setlistPanel.setBounds (area);
        return;
    }

    auto buttons = area.removeFromBottom (48);
    newButton.setBounds (buttons.removeFromLeft (220));
    buttons.removeFromLeft (12);
    openButton.setBounds (buttons.removeFromLeft (140));
    deleteButton.setBounds (buttons.removeFromRight (100));

    area.removeFromBottom (12);
    listBox.setBounds (area);
}
