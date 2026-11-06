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
      newButton (t), settingsButton (t),
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
    listBox.setTooltip ("星をクリックで難易度。右クリックで詳細の編集・フォルダに移動"_jp);
    addAndMakeVisible (listBox);

    // フォルダ (左の列)。最後に選んでいたフォルダを覚えておく
    folderFilter = settings.getSessionFolderFilter();
    folderList.onSelect = [this] (const juce::String& key)
    {
        folderFilter = key;
        settings.setSessionFolderFilter (key);
        applyFolderFilter (false);
    };
    folderList.onNewFolder = [this] { createFolder(); };
    folderList.onRename = [this] (const juce::String& name) { renameFolder (name); };
    folderList.onDelete = [this] (const juce::String& name) { deleteFolder (name); };
    addAndMakeVisible (folderList);

    newButton.setFixedText (FixedTextId::NewSession, "+ 新しいセッション"_jp);
    settingsButton.setFixedText (FixedTextId::Settings, "⚙ 設定"_jp);

    newButton.onClick = [this] { if (onNewSession != nullptr) onNewSession(); };
    settingsButton.onClick = [this] { if (onOpenSettings != nullptr) onOpenSettings(); };

    for (auto* button : { &newButton, &settingsButton })
        addAndMakeVisible (button);

    // デスメタルモードの切り替え (death-metal-mode.md 4.7)。状態はアプリの設定に保存される

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
    themeBox.setEnabled (! theme.isDeathMetal());   // デスメタルモード中は、配色はデスメタルのまま
}

void SessionListScreen::showTab (Tab tab)
{
    currentTab = tab;
    settings.setLibraryTab ((int) tab);

    const auto sessions = tab == Tab::sessions;
    sessionsTab.setToggleState (sessions, juce::dontSendNotification);
    setlistsTab.setToggleState (! sessions, juce::dontSendNotification);

    for (auto* c : std::initializer_list<juce::Component*> { &listBox, &folderList, &newButton })
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

    allEntries = store.list();
    picker.setSessions (allEntries);
    applyFolderFilter (false);
}

//==============================================================================
// フォルダ分け
juce::StringArray SessionListScreen::getFolderNames() const
{
    return SessionFolders::collectNames (allEntries, settings.getSessionFolders());
}

void SessionListScreen::updateFolderList()
{
    const auto names = getFolderNames();
    settings.setSessionFolders (names);   // セッションが使っている名前も、設定の一覧に入れておく

    // 選んでいたフォルダが無くなっていたら (アプリの外でファイルを消したなど)、「すべて」に戻す
    const auto items = SessionFolders::buildItems (allEntries, names);
    if (std::none_of (items.begin(), items.end(), [this] (const SessionFolders::Item& i) { return i.getKey() == folderFilter; }))
    {
        folderFilter = SessionFolders::allKey;
        settings.setSessionFolderFilter (folderFilter);
    }

    folderList.setItems (items, folderFilter);
}

void SessionListScreen::applyFolderFilter (bool keepSelection)
{
    updateFolderList();

    const auto row = listBox.getSelectedRow();
    const auto selectedId = keepSelection && juce::isPositiveAndBelow (row, (int) entries.size()) ? entries[(size_t) row].id
                                                                                                   : juce::String();

    entries = SessionFolders::filter (allEntries, folderFilter);
    listBox.updateContent();
    listBox.deselectAllRows();

    int toSelect = entries.empty() ? -1 : 0;
    for (int i = 0; i < (int) entries.size(); ++i)
        if (entries[(size_t) i].id == selectedId)
            toSelect = i;
    if (toSelect >= 0)
        listBox.selectRow (toSelect);

    updateButtons();
    repaint();
}

void SessionListScreen::updateEntry (const Session& updated)
{
    for (auto* list : { &allEntries, &entries })
        for (auto& e : *list)
            if (e.id == updated.id)
            {
                e.title = updated.title;
                e.artist = updated.artist;
                e.tuning = updated.tuning;
                e.bpm = updated.bpm;
                e.difficulty = updated.difficulty;
                e.folder = updated.folder;
            }
}

void SessionListScreen::askFolderName (const juce::String& title, const juce::String& initial,
                                       std::function<void (juce::String)> onOk)
{
    folderNameWindow = std::make_unique<juce::AlertWindow> (title, juce::String(), juce::MessageBoxIconType::NoIcon, this);
    folderNameWindow->addTextEditor ("name", initial, "フォルダの名前"_jp);
    folderNameWindow->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    folderNameWindow->addButton ("キャンセル"_jp, 0, juce::KeyPress (juce::KeyPress::escapeKey));

    folderNameWindow->enterModalState (true, juce::ModalCallbackFunction::create (
        [safe = juce::Component::SafePointer (this), onOk = std::move (onOk)] (int result)
    {
        if (safe == nullptr || safe->folderNameWindow == nullptr)
            return;

        const auto name = safe->folderNameWindow->getTextEditorContents ("name").trim();
        safe->folderNameWindow.reset();

        if (result == 1)
            onOk (name);
    }), false);
}

void SessionListScreen::createFolder (std::function<void (juce::String)> then)
{
    askFolderName ("新しいフォルダ"_jp, {}, [this, then] (juce::String name)
    {
        auto names = getFolderNames();
        if (const auto problem = SessionFolders::checkName (name, names); problem.isNotEmpty())
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "フォルダを作れませんでした"_jp, problem);
            return;
        }

        names.add (name);
        settings.setSessionFolders (names);

        if (then != nullptr)
        {
            then (name);   // 「新しいフォルダを作って移動」: 作ったフォルダにセッションを入れる
            return;
        }

        // 作ったフォルダを開く (空なので、右クリックの「フォルダに移動」で入れてもらう)
        folderFilter = SessionFolders::keyFor (name);
        settings.setSessionFolderFilter (folderFilter);
        applyFolderFilter (false);
    });
}

void SessionListScreen::renameFolder (const juce::String& oldName)
{
    askFolderName ("フォルダの名前の変更"_jp, oldName, [this, oldName] (juce::String newName)
    {
        if (newName == oldName)
            return;

        auto names = getFolderNames();
        names.removeString (oldName);
        if (const auto problem = SessionFolders::checkName (newName, names); problem.isNotEmpty())
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "名前を変えられませんでした"_jp, problem);
            return;
        }

        // 中のセッションのファイルを書き直す (一覧の並びは変えない)
        for (const auto& e : allEntries)
            if (e.folder == oldName)
                if (auto updated = store.update (e.id, [&newName] (Session& s) { s.folder = newName; }))
                    updateEntry (*updated);

        names.add (newName);
        settings.setSessionFolders (names);

        if (folderFilter == SessionFolders::keyFor (oldName))
        {
            folderFilter = SessionFolders::keyFor (newName);
            settings.setSessionFolderFilter (folderFilter);
        }
        applyFolderFilter (true);
    });
}

void SessionListScreen::deleteFolder (const juce::String& name)
{
    int count = 0;
    for (const auto& e : allEntries)
        if (e.folder == name)
            ++count;

    auto options = juce::MessageBoxOptions()
                       .withIconType (juce::MessageBoxIconType::QuestionIcon)
                       .withTitle ("フォルダの削除"_jp)
                       .withMessage ("フォルダ「"_jp + name + "」を削除しますか？"_jp
                                     + (count > 0 ? "\n中の "_jp + juce::String (count) + " 件のセッションは消さずに、「フォルダなし」に移します。"_jp
                                                  : juce::String()))
                       .withButton ("削除"_jp)
                       .withButton ("キャンセル"_jp)
                       .withAssociatedComponent (this);

    juce::AlertWindow::showAsync (options, [safe = juce::Component::SafePointer (this), name] (int result)
    {
        if (safe == nullptr || result != 1)
            return;

        for (const auto& e : safe->allEntries)
            if (e.folder == name)
                if (auto updated = safe->store.update (e.id, [] (Session& s) { s.folder = {}; }))
                    safe->updateEntry (*updated);

        auto names = safe->settings.getSessionFolders();
        names.removeString (name);
        safe->settings.setSessionFolders (names);

        if (safe->folderFilter == SessionFolders::keyFor (name))
        {
            safe->folderFilter = SessionFolders::allKey;
            safe->settings.setSessionFolderFilter (safe->folderFilter);
        }
        safe->applyFolderFilter (false);
    });
}

void SessionListScreen::moveToFolder (const juce::String& sessionFileId, const juce::String& folder)
{
    if (auto updated = store.update (sessionFileId, [&folder] (Session& s) { s.folder = folder; }))
    {
        updateEntry (*updated);
        applyFolderFilter (true);   // 今のフォルダから出たなら、一覧から消える
    }
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
    // 開く・削除のボタンは無くした (行のダブルクリックと右クリックのメニューで行う)。今は変えるものが無い
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

//==============================================================================
// 一覧の列: 曲名・アーティスト・BPM・難易度 (星)・チューニング・長さ・更新日時
namespace
{
    constexpr int starSize = 22;
    constexpr int headerHeight = 24;

    juce::String formatLength (double seconds)
    {
        if (seconds <= 0.0)
            return "—"_jp;   // 練習画面で一度開くと分かる

        const auto total = juce::roundToInt (seconds);
        return juce::String (total / 60) + ":" + juce::String (total % 60).paddedLeft ('0', 2);
    }

    void drawStars (juce::Graphics& g, juce::Rectangle<int> area, int filled, juce::Colour on, juce::Colour off)
    {
        for (int i = 0; i < Session::maxDifficulty; ++i)
        {
            const auto cell = juce::Rectangle<float> ((float) (area.getX() + i * starSize), (float) area.getY(),
                                                      (float) starSize, (float) area.getHeight());
            juce::Path star;
            star.addStar (cell.getCentre(), 5, starSize * 0.2f, starSize * 0.42f);

            if (i < filled)
            {
                g.setColour (on);
                g.fillPath (star);
            }
            else
            {
                g.setColour (off);
                g.strokePath (star, juce::PathStrokeType (1.2f));
            }
        }
    }
}

SessionListScreen::Columns SessionListScreen::layoutColumns (int width, int height)
{
    auto area = juce::Rectangle<int> (0, 0, width, height).reduced (12, 0);
    Columns c;

    const auto take = [&area] (int w)
    {
        auto r = area.removeFromRight (w);
        area.removeFromRight (12);
        return r;
    };

    c.modified = take (128);
    c.length = take (56);
    c.tuning = take (130);
    c.difficulty = take (starSize * Session::maxDifficulty);
    c.bpm = take (48);

    // 残りを曲名 6 : アーティスト 4 で分ける
    const auto titleWidth = area.getWidth() * 6 / 10;
    c.title = area.removeFromLeft (titleWidth);
    area.removeFromLeft (12);
    c.artist = area;
    return c;
}

int SessionListScreen::starAt (const Columns& columns, int x)
{
    const auto offset = x - columns.difficulty.getX();
    if (offset < 0 || offset >= starSize * Session::maxDifficulty)
        return 0;

    return offset / starSize + 1;
}

void SessionListScreen::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (! juce::isPositiveAndBelow (row, (int) entries.size()))
        return;

    const auto& entry = entries[(size_t) row];
    const auto columns = layoutColumns (width, height);

    if (selected)
        g.fillAll (listBox.findColour (ThemeColourIds::listSelection));

    const auto textColour = listBox.findColour (juce::ListBox::textColourId);
    const auto muted = textColour.withAlpha (0.6f);

    g.setColour (textColour);
    g.setFont (AppLookAndFeel::scaled (juce::FontOptions (17.0f)));
    g.drawText (entry.title.isEmpty() ? "(無題)"_jp : entry.title, columns.title, juce::Justification::centredLeft);

    g.setFont (AppLookAndFeel::scaled (juce::FontOptions (14.0f)));
    g.setColour (entry.artist.isEmpty() ? muted : textColour);
    g.drawText (entry.artist.isEmpty() ? "—"_jp : entry.artist, columns.artist, juce::Justification::centredLeft);

    g.setColour (textColour);
    g.drawText (entry.bpm > 0.0 ? juce::String (juce::roundToInt (entry.bpm)) : "—"_jp, columns.bpm,
                juce::Justification::centredRight);

    g.setColour (entry.tuning.isEmpty() ? muted : textColour);
    g.drawText (entry.tuning.isEmpty() ? "—"_jp : entry.tuning, columns.tuning, juce::Justification::centredLeft);

    g.setColour (textColour);
    g.drawText (formatLength (entry.lengthSeconds), columns.length, juce::Justification::centredRight);

    drawStars (g, columns.difficulty, entry.difficulty,
               listBox.findColour (ThemeColourIds::accent), textColour.withAlpha (0.35f));

    g.setColour (textColour.withAlpha (0.5f));
    g.setFont (AppLookAndFeel::scaled (juce::FontOptions (13.0f)));
    g.drawText (entry.modified.formatted ("%Y/%m/%d %H:%M"), columns.modified, juce::Justification::centredRight);
}

void SessionListScreen::listBoxItemClicked (int row, const juce::MouseEvent& e)
{
    if (! juce::isPositiveAndBelow (row, (int) entries.size()))
        return;

    if (e.mods.isPopupMenu())
    {
        listBox.selectRow (row);
        showRowMenu (row);
        return;
    }

    // 星をクリックすると、そこまで塗る。塗ってある一番右の星をもう一度押すと、0 に戻す
    const auto columns = layoutColumns (listBox.getVisibleRowWidth(), listBox.getRowHeight());
    if (const auto star = starAt (columns, e.x); star > 0)
        setDifficulty (row, star == entries[(size_t) row].difficulty ? 0 : star);
}

void SessionListScreen::listBoxItemDoubleClicked (int row, const juce::MouseEvent& e)
{
    // 星を続けて押したときに、セッションが開いてしまわないように
    const auto columns = layoutColumns (listBox.getVisibleRowWidth(), listBox.getRowHeight());
    if (starAt (columns, e.x) > 0)
        return;

    listBox.selectRow (row);
    openSelected();
}

void SessionListScreen::setDifficulty (int row, int stars)
{
    if (auto updated = store.update (entries[(size_t) row].id, [stars] (Session& s) { s.difficulty = stars; }))
    {
        updateEntry (*updated);
        listBox.repaintRow (row);
    }
}

void SessionListScreen::showRowMenu (int row)
{
    // フォルダに移動: 各フォルダ (今のフォルダには印)、フォルダなし、新しいフォルダを作って移動
    constexpr int folderBase = 100, noFolderId = 99, newFolderId = 98;
    const auto names = getFolderNames();
    const auto current = entries[(size_t) row].folder;

    juce::PopupMenu folders;
    for (int i = 0; i < names.size(); ++i)
        folders.addItem (folderBase + i, names[i], names[i] != current, names[i] == current);
    if (! names.isEmpty())
        folders.addSeparator();
    folders.addItem (noFolderId, "フォルダなし"_jp, current.isNotEmpty(), current.isEmpty());
    folders.addItem (newFolderId, "新しいフォルダを作って移動…"_jp);

    juce::PopupMenu menu;
    menu.addItem (1, "詳細を編集…"_jp);
    menu.addItem (2, "開く"_jp);
    menu.addSubMenu ("フォルダに移動"_jp, folders);
    menu.addSeparator();
    menu.addItem (3, "削除"_jp);

    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition(),
                        [safe = juce::Component::SafePointer (this), row, names] (int result)
    {
        if (safe == nullptr || ! juce::isPositiveAndBelow (row, (int) safe->entries.size()))
            return;

        const auto fileId = safe->entries[(size_t) row].id;

        if (result >= folderBase && result - folderBase < names.size())
        {
            safe->moveToFolder (fileId, names[result - folderBase]);
            return;
        }

        switch (result)
        {
            case 1: safe->editDetails (row); break;
            case 2: safe->openSelected(); break;
            case 3: safe->deleteSelected(); break;
            case noFolderId: safe->moveToFolder (fileId, {}); break;
            case newFolderId:
                safe->createFolder ([safe, fileId] (juce::String name)
                {
                    if (safe != nullptr)
                        safe->moveToFolder (fileId, name);
                });
                break;
            default: break;
        }
    });
}

// 「詳細を編集…」: 曲名・アーティスト・BPM・チューニング
void SessionListScreen::editDetails (int row)
{
    const auto entry = entries[(size_t) row];

    detailsWindow = std::make_unique<juce::AlertWindow> ("詳細を編集"_jp, juce::String(), juce::MessageBoxIconType::NoIcon, this);
    detailsWindow->addTextEditor ("title", entry.title, "曲名"_jp);
    detailsWindow->addTextEditor ("artist", entry.artist, "アーティスト"_jp);
    detailsWindow->addTextEditor ("bpm", juce::String (juce::roundToInt (entry.bpm)), "BPM (20〜400)"_jp);
    detailsWindow->addTextEditor ("tuning", entry.tuning, "チューニング"_jp);

    if (auto* bpm = detailsWindow->getTextEditor ("bpm"))
        bpm->setInputRestrictions (3, "0123456789");
    if (auto* tuning = detailsWindow->getTextEditor ("tuning"))
        tuning->setTextToShowWhenEmpty ("例: E Standard、Drop D、D A D G A D"_jp, juce::Colours::grey);

    detailsWindow->addButton ("保存"_jp, 1, juce::KeyPress (juce::KeyPress::returnKey));
    detailsWindow->addButton ("キャンセル"_jp, 0, juce::KeyPress (juce::KeyPress::escapeKey));

    detailsWindow->enterModalState (true, juce::ModalCallbackFunction::create (
        [safe = juce::Component::SafePointer (this), id = entry.id] (int result)
    {
        if (safe == nullptr || safe->detailsWindow == nullptr)
            return;

        auto& w = *safe->detailsWindow;
        const auto title = w.getTextEditorContents ("title").trim();
        const auto artist = w.getTextEditorContents ("artist").trim();
        const auto bpmText = w.getTextEditorContents ("bpm").trim();
        const auto tuning = w.getTextEditorContents ("tuning").trim();
        safe->detailsWindow.reset();

        if (result != 1)
            return;

        const auto bpm = bpmText.getDoubleValue();
        if (bpm < 20.0 || bpm > 400.0)
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "BPM の値"_jp,
                                                    "BPM は 20〜400 の数で入力してください。変更は保存していません。"_jp);
            return;
        }

        const auto updated = safe->store.update (id, [&] (Session& s)
        {
            s.title = title;
            s.artist = artist;
            s.tuning = tuning;
            if (juce::roundToInt (s.bpm) != juce::roundToInt (bpm))
            {
                s.bpm = bpm;
                s.tempoOrigin = TempoOrigin::manual;
            }
        });

        if (! updated.has_value())
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "保存できませんでした"_jp, {});
            return;
        }

        // 選んでいる行はそのまま、表示だけ直す。曲名はセットリストにも出るので、そちらも直す
        safe->library.noteSaved (*updated);
        safe->updateEntry (*updated);
        safe->listBox.repaint();
        safe->picker.setSessions (safe->allEntries);
        safe->setlistPanel.refresh();
    }), false);
}

void SessionListScreen::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));

    if (currentTab != Tab::sessions)
        return;

    g.setColour (findColour (ThemeColourIds::textMuted));
    g.setFont (AppLookAndFeel::scaled (juce::FontOptions (12.5f, juce::Font::bold)));
    g.drawText ("フォルダ"_jp, folderHeadingArea.withTrimmedLeft (10), juce::Justification::centredLeft);

    if (entries.empty())
        return;

    // 列の見出し (行と同じ位置に)
    const auto columns = layoutColumns (listBox.getVisibleRowWidth(), headerArea.getHeight());
    const auto origin = headerArea.getPosition();

    g.setColour (findColour (ThemeColourIds::textMuted));
    g.setFont (AppLookAndFeel::scaled (juce::FontOptions (12.5f, juce::Font::bold)));

    const auto heading = [&] (juce::Rectangle<int> column, const juce::String& text, juce::Justification j)
    {
        g.drawText (text, column + origin, j);
    };
    heading (columns.title, "曲名"_jp, juce::Justification::centredLeft);
    heading (columns.artist, "アーティスト"_jp, juce::Justification::centredLeft);
    heading (columns.bpm, "BPM", juce::Justification::centredRight);
    heading (columns.difficulty, "難易度"_jp, juce::Justification::centredLeft);
    heading (columns.tuning, "チューニング"_jp, juce::Justification::centredLeft);
    heading (columns.length, "長さ"_jp, juce::Justification::centredRight);
    heading (columns.modified, "更新"_jp, juce::Justification::centredRight);
}

void SessionListScreen::paintOverChildren (juce::Graphics& g)
{
    if (currentTab != Tab::sessions || ! entries.empty())
        return;

    g.setColour (listBox.findColour (juce::ListBox::textColourId).withAlpha (0.6f));
    g.setFont (AppLookAndFeel::scaled (juce::FontOptions (16.0f)));
    g.drawFittedText (allEntries.empty()
                          ? "セッションがありません。\n「+ 新しいセッション」から作成してください。"_jp
                          : "このフォルダにはセッションがありません。\n「すべて」を選び、セッションの右クリック「フォルダに移動」で入れられます。"_jp,
                      listBox.getBounds(), juce::Justification::centred, 2);
}

void SessionListScreen::resized()
{
    auto area = getLocalBounds().reduced (24);

    auto top = area.removeFromTop (44);
    settingsButton.setBounds (top.removeFromRight (140).reduced (0, 6));
    top.removeFromRight (12);   // デスメタルモードの切り替えは設定画面だけにした (作者の指示)
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

    // 下: 「+ 新しいセッション」は右下 (前の「削除」の位置。作者の指示)
    auto buttons = area.removeFromBottom (48);
    newButton.setBounds (buttons.removeFromRight (220));

    area.removeFromBottom (12);

    // 見出しの行 (左: 「フォルダ」、右: 列の見出し)。フォルダの枠と一覧の枠の上を揃えるため、両方の上に同じ高さで取る
    auto headings = area.removeFromTop (headerHeight);
    folderHeadingArea = headings.removeFromLeft (210);
    headings.removeFromLeft (14);

    // 左にフォルダの列
    folderList.setBounds (area.removeFromLeft (210));
    area.removeFromLeft (14);

    headerArea = headings;
    listBox.setBounds (area);
}
