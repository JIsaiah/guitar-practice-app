#include "SetlistCard.h"
#include "AppDialog.h"
#include "../Text.h"

SetlistCard::SetlistCard (Setlist s, SessionLibrary& l, SetlistStore& st, ThemeManager& theme)
    : setlist (std::move (s)), library (l), store (st), playButton (theme), entryList (setlist, l)
{
    // 一覧のタブ → このボタンの2クリックで再生が始まる (4.2)
    playButton.setFixedText (FixedTextId::Play, "▶ 通しで再生"_jp);
    playButton.setTooltip ("1曲目から通しで再生します"_jp);
    playButton.onClick = [this]
    {
        // リンク切れの曲は飛ばす。最初に開ける曲から
        for (int i = 0; i < (int) setlist.entries.size(); ++i)
            if (library.contains (setlist.entries[(size_t) i].sessionId))
            {
                if (onPlay != nullptr)
                    onPlay (i);
                return;
            }
    };
    addAndMakeVisible (playButton);

    // 「通しで再生」の右に、右クリックと同じメニューを出すボタン (作者の指示: 枠なし)
    menuButton.setFrameless (true);
    menuButton.setIconScale (0.6f);
    menuButton.setTooltip ("名前・ライブの日の変更、ライブ終わった、削除"_jp);
    menuButton.setMouseCursor (juce::MouseCursor::PointingHandCursor);
    menuButton.onClick = [this] { showHeaderMenu(); };
    addAndMakeVisible (menuButton);
    updateAlpha();   // ライブが終わったセットリストは、たたんでいる間は薄く (作者の指示)

    entryList.onPlay = [this] (int index) { if (onPlay != nullptr) onPlay (index); };
    entryList.onOpenSession = [this] (const juce::String& id) { if (onOpenSession != nullptr) onOpenSession (id); };
    entryList.onChanged = [this] { changed(); };
    addChildComponent (entryList);

    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    updatePlayButton();

    // 開く・たたむ動き: 動いている間、カードの高さが変わる (一覧が並べ直す)。たたみ終わったら曲順を隠す
    openFold.setTarget (false, false);
    openFold.onChange = [this]
    {
        repaint();
        if (onLayoutChanged != nullptr)
            onLayoutChanged();
    };
    openFold.onFinished = [this]
    {
        entryList.setVisible (expanded);
        updateAlpha();   // たたみ終わってから薄くする (作者の指示)
    };
}

void SetlistCard::updatePlayButton()
{
    playButton.setEnabled (std::any_of (setlist.entries.begin(), setlist.entries.end(),
                                        [this] (const SetlistEntry& e) { return library.contains (e.sessionId); }));
}

void SetlistCard::setExpanded (bool shouldBeExpanded)
{
    if (expanded == shouldBeExpanded)
        return;

    expanded = shouldBeExpanded;
    // ライブが終わったものも、開いている間は濃く (開くときはすぐ)。たたむときは、たたみ終わってから薄く (作者の指示: openFold.onFinished)
    if (expanded)
        updateAlpha();
    if (expanded)
        entryList.setVisible (true);   // たたむときは、たたみ終わってから隠す (openFold.onFinished)
    openFold.setTarget (expanded);     // 画面に出ていないとき (作ったとき) は、すぐにその高さ
    if (! openFold.isAnimating())
    {
        entryList.setVisible (expanded);
        updateAlpha();
    }
    repaint();

    if (onLayoutChanged != nullptr)
        onLayoutChanged();
}

int SetlistCard::getPreferredHeight() const
{
    // 開き具合の分だけ、曲順と「＋ 曲を追加」の高さを足す (たたむ・開く途中は、その途中の高さ)
    return headerHeight + juce::roundToInt ((entryList.getPreferredHeight() + addRowHeight) * openFold.getValue());
}

void SetlistCard::addSession (const juce::String& sessionId)
{
    // ループ範囲を写すため、元のセッションを読む (読めなくても追加はする)
    const auto source = library.load (sessionId);
    setlist.addEntry (sessionId, source.has_value() ? &*source : nullptr);
    changed();
}

void SetlistCard::setAdding (bool isAdding)
{
    adding = isAdding;
    repaint();
}

juce::Rectangle<int> SetlistCard::getAddRowArea() const
{
    if (! expanded)
        return {};
    return { 0, headerHeight + entryList.getPreferredHeight(), getWidth(), addRowHeight };
}

void SetlistCard::save()
{
    if (! store.save (setlist))
        AppDialog::showMessageBoxAsync (juce::MessageBoxIconType::NoIcon,
                                                "保存できませんでした"_jp,
                                                store.getDirectory().getFullPathName());
}

void SetlistCard::changed()
{
    save();
    updatePlayButton();
    repaint();
    if (onLayoutChanged != nullptr)
        onLayoutChanged();
}

juce::String SetlistCard::describeSummary()
{
    int songs = 0;
    double total = 0.0;
    bool unknown = false;

    for (const auto& entry : setlist.entries)
    {
        if (! library.contains (entry.sessionId))
            continue;   // リンク切れは数えない

        ++songs;
        const auto length = library.getLengthSeconds (entry.sessionId);
        if (length > 0.0)
            total += length;
        else
            unknown = true;
    }

    auto text = juce::String (songs) + "曲"_jp;
    if (songs > 0 && (total > 0.0 || ! unknown))
        text << "・"_jp << (unknown ? "約 "_jp : juce::String()) << formatDuration (total);

    const auto missing = (int) setlist.entries.size() - songs;
    if (missing > 0)
        text << "・見つからない曲 "_jp << missing;

    return text;
}

//==============================================================================
void SetlistCard::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (findColour (ThemeColourIds::panel));
    g.fillRoundedRectangle (bounds, 10.0f);
    g.setColour (findColour (ThemeColourIds::border));
    g.drawRoundedRectangle (bounds, 10.0f, 1.0f);

    auto header = getLocalBounds().removeFromTop (headerHeight).reduced (16, 0);
    header.setRight (playButton.getX() - 16);

    // 開閉の印 (作者のレイアウト): 開いているとき ▲、閉じているとき ▼
    const auto arrowArea = header.removeFromLeft (24).toFloat();
    juce::Path arrow;
    if (expanded)
        arrow.addTriangle (0.0f, 9.0f, 14.0f, 9.0f, 7.0f, 0.0f);
    else
        arrow.addTriangle (0.0f, 0.0f, 14.0f, 0.0f, 7.0f, 9.0f);
    g.setColour (findColour (juce::Label::textColourId));
    g.fillPath (arrow, juce::AffineTransform::translation (arrowArea.getX(), arrowArea.getCentreY() - 4.5f));
    header.removeFromLeft (24);

    g.setColour (findColour (juce::Label::textColourId));
    g.setFont (AppLookAndFeel::scaled (juce::FontOptions (18.0f, juce::Font::bold)));
    // 名前と、決めていればライブの日 (作者のレイアウト: 「セットリスト2　—　2026/11/21」)
    const auto title = setlist.liveDate.isEmpty() ? setlist.name
                                                  : setlist.name + "　—　"_jp + Setlist::formatLiveDate (setlist.liveDate);
    g.drawText (title, header.withTrimmedBottom (headerHeight / 2 - 4).withTrimmedTop (10),
                juce::Justification::bottomLeft, true);

    g.setColour (findColour (ThemeColourIds::textMuted));
    g.setFont (AppLookAndFeel::scaled (juce::FontOptions (13.0f)));
    g.drawText (setlist.entries.empty() ? "曲がありません。開いて「＋ 曲を追加」から追加できます"_jp : describeSummary(),
                header.withTrimmedTop (headerHeight / 2 + 4), juce::Justification::topLeft, true);

    if (openFold.getValue() <= 0.0)
        return;

    // 見出しと曲順の境目 (カードの幅いっぱいに近い線)
    g.setColour (findColour (ThemeColourIds::border));
    g.fillRect (16, headerHeight - 1, getWidth() - 32, 1);

    // 「＋ 曲を追加」の行。曲の行と同じ列 (番号の列に「＋」、曲名の列に文字) に置く。
    // たたむ・開く途中も、開いたときの場所に描く (カードの外にはみ出す分は描かれない)
    auto row = juce::Rectangle<int> (0, headerHeight + entryList.getPreferredHeight(), getWidth(), addRowHeight);
    if (addHover)
        AppLookAndFeel::fillListHighlight (g, SetlistEntryList::highlightArea (row), findColour (ThemeColourIds::listSelection));
    g.setColour (findColour (ThemeColourIds::border));
    g.fillRect (row.getX() + 24, row.getY(), row.getWidth() - 48, 1);

    row = row.reduced (SetlistEntryList::contentInset, 0);
    g.setColour (adding ? findColour (ThemeColourIds::accent) : findColour (juce::Label::textColourId));
    g.setFont (AppLookAndFeel::scaled (juce::FontOptions (18.0f, juce::Font::bold)));
    g.drawText ("+", row.removeFromLeft (SetlistEntryList::numberWidth), juce::Justification::centredLeft);
    g.setFont (AppLookAndFeel::scaled (juce::FontOptions (15.0f, juce::Font::bold)));
    g.drawText ("曲を追加"_jp, row, juce::Justification::centredLeft);
}

void SetlistCard::resized()
{
    auto area = getLocalBounds();
    auto header = area.removeFromTop (headerHeight);
    // 右から「…」(枠なし)、少し空けて「通しで再生」
    header.removeFromRight (12);
    menuButton.setBounds (header.removeFromRight (36).withSizeKeepingCentre (36, 36));
    header.removeFromRight (10);
    playButton.setBounds (header.removeFromRight (168).reduced (0, 12));

    // 曲順は、開いたときの高さのまま置く (たたむ途中は、カードの外にはみ出す分が見えないだけ)
    // 高さはカードの中に見えている分まで (行の場所は変わらないので、たたむ途中は下から隠れていく)。
    // 開いたときの高さのまま置くと、薄くしたカード (setAlpha) では、はみ出した分が描かれてしまう
    // (JUCE は、透明度のある部品の子を、その部品の外でも描くため)
    entryList.setBounds (area.getX(), area.getY(), area.getWidth(), juce::jmin (entryList.getPreferredHeight(), area.getHeight()));
}

void SetlistCard::mouseMove (const juce::MouseEvent& e)
{
    const auto hover = getAddRowArea().contains (e.getPosition());
    if (hover != addHover)
    {
        addHover = hover;
        repaint();
    }
}

void SetlistCard::mouseExit (const juce::MouseEvent&)
{
    if (addHover)
    {
        addHover = false;
        repaint();
    }
}

void SetlistCard::mouseUp (const juce::MouseEvent& e)
{
    if (e.mouseWasDraggedSinceMouseDown())
        return;

    // 「＋ 曲を追加」の行
    if (getAddRowArea().contains (e.getPosition()))
    {
        if (! e.mods.isPopupMenu() && onRequestAdd != nullptr)
            onRequestAdd();
        return;
    }

    if (e.y >= headerHeight)
        return;

    // 名前の場所の外 (開閉の印など) は、続けて押しても毎回開閉する (ダブルクリックで名前の変更にしない)
    if (e.mods.isPopupMenu())
        showHeaderMenu();
    else if (e.getNumberOfClicks() == 1 || ! getNameArea().contains (e.getPosition()))
        setExpanded (! expanded);
}

void SetlistCard::mouseDoubleClick (const juce::MouseEvent& e)
{
    // 名前のダブルクリックで名前を変える (1回目のクリックで開閉しているので、元に戻す)。
    // 名前を変えられるのは、名前の左端から「通しで再生」の少し左までだけ (作者の指示: 開閉の印を連打しても出ないように)
    if (getNameArea().contains (e.getPosition()) && ! e.mods.isPopupMenu())
    {
        setExpanded (! expanded);
        rename();
    }
}

juce::Rectangle<int> SetlistCard::getNameArea() const
{
    // paint と同じ計算: 見出しの左右の余白 16、右は「通しで再生」の 16 手前まで、左は開閉の印 (24) とその後ろの間 (24)
    auto header = getLocalBounds().removeFromTop (headerHeight).reduced (16, 0);
    header.setRight (playButton.getX() - 16);
    header.removeFromLeft (24 + 24);
    return header;
}

void SetlistCard::showHeaderMenu()
{
    // 作者の指示: 語尾の「…」は付けない。ライブの日の変更と「ライブ終わった」を足す
    juce::PopupMenu menu;
    menu.addItem (1, "名前を変更"_jp);
    menu.addItem (3, "ライブの日を変更"_jp);
    menu.addItem (4, setlist.finished ? "ライブ前に戻す"_jp : "ライブ終わった"_jp);
    menu.addSeparator();
    menu.addItem (2, "セットリストを削除"_jp);

    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition(),
                        [safe = juce::Component::SafePointer (this)] (int result)
    {
        if (safe == nullptr)
            return;

        if (result == 1) safe->rename();
        if (result == 2) safe->requestDelete();
        if (result == 3) safe->editLiveDate();
        if (result == 4) safe->setFinished (! safe->setlist.finished);
    });
}

void SetlistCard::rename()
{
    // 名前の変更だけは、キーボードでの入力を使う (4.5)
    auto* window = new AppDialog ("セットリストの名前"_jp, {}, juce::MessageBoxIconType::NoIcon, this);
    window->addTextEditor ("name", setlist.name);
    window->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("キャンセル"_jp, 0, juce::KeyPress (juce::KeyPress::escapeKey));

    window->enterModalState (true, juce::ModalCallbackFunction::create (
        [safe = juce::Component::SafePointer (this), window] (int result)
        {
            if (safe == nullptr || result != 1)
                return;

            const auto name = window->getTextEditorContents ("name").trim();
            if (name.isNotEmpty() && name != safe->setlist.name)
            {
                safe->setlist.name = name;
                safe->save();
                safe->repaint();
            }
        }), true);
}

namespace
{
    // ライブの日の入力画面 (作者の指示)。アプリのウィンドウの中に重ねて出す (別のウィンドウを作らないので、すぐに出る)。
    // OK で日付として読めなければ閉じずに、入力欄の下に知らせて、入れ直してもらう
    class LiveDateDialog final : public juce::Component
    {
    public:
        LiveDateDialog (const juce::String& initial, std::function<void (const juce::String& isoDate)> done)
            : onDone (std::move (done))
        {
            title.setText ("ライブの日"_jp, juce::dontSendNotification);
            title.setFont (AppLookAndFeel::scaled (juce::FontOptions (18.0f, juce::Font::bold)));
            hint.setText ("YYYY/MM/DD・MM/DDのように入力してください"_jp, juce::dontSendNotification);
            hint.setFont (AppLookAndFeel::scaled (juce::FontOptions (14.0f)));
            error.setFont (AppLookAndFeel::scaled (juce::FontOptions (13.0f, juce::Font::bold)));

            editor.setText (initial, false);
            editor.setFont (AppLookAndFeel::scaled (juce::FontOptions (17.0f)));
            editor.onReturnKey = [this] { submit(); };
            editor.onEscapeKey = [this] { close(); };
            editor.onTextChange = [this] { error.setText ({}, juce::dontSendNotification); };

            okButton.setButtonText ("OK");
            cancelButton.setButtonText ("キャンセル"_jp);
            okButton.onClick = [this] { submit(); };
            cancelButton.onClick = [this] { close(); };

            for (auto* c : std::initializer_list<juce::Component*> { &title, &hint, &editor, &error, &okButton, &cancelButton })
                addAndMakeVisible (c);
        }

        // owner のウィンドウいっぱいに重ねて出す (後ろは薄く暗くし、押せないようにする)
        void showOver (juce::Component& owner)
        {
            auto* top = owner.getTopLevelComponent();
            top->addAndMakeVisible (this);
            setBounds (top->getLocalBounds());
            error.setColour (juce::Label::textColourId, findColour (ThemeColourIds::accent));
            enterModalState (true, nullptr, true);   // 閉じると消える (deleteWhenDismissed)
            editor.grabKeyboardFocus();
            editor.selectAll();
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (juce::Colours::black.withAlpha (0.35f));
            const auto box = getBox().toFloat();
            g.setColour (findColour (ThemeColourIds::panel));
            g.fillRoundedRectangle (box, 10.0f);
            g.setColour (findColour (ThemeColourIds::border));
            g.drawRoundedRectangle (box.reduced (0.5f), 10.0f, 1.0f);
        }

        void resized() override
        {
            auto area = getBox().reduced (20, 16);
            title.setBounds (area.removeFromTop (28));
            area.removeFromTop (4);
            hint.setBounds (area.removeFromTop (22));
            area.removeFromTop (8);
            editor.setBounds (area.removeFromTop (36));
            area.removeFromTop (4);
            error.setBounds (area.removeFromTop (22));   // 入力欄の下に、読めなかったことを出す
            auto buttons = area.removeFromBottom (36);
            cancelButton.setBounds (buttons.removeFromRight (120));
            buttons.removeFromRight (8);
            okButton.setBounds (buttons.removeFromRight (120));
        }

        void inputAttemptWhenModal() override { editor.grabKeyboardFocus(); }

    private:
        juce::Rectangle<int> getBox() const { return getLocalBounds().withSizeKeepingCentre (480, 230); }

        void submit()
        {
            const auto text = editor.getText().trim();
            const auto date = Setlist::parseLiveDate (text);
            if (text.isNotEmpty() && date.isEmpty())
            {
                // 閉じずに、入力欄の下に知らせて入れ直してもらう (作者の指示)
                error.setText ("「"_jp + text + "」は日付として読めません。YYYY/MM/DD か MM/DD で入れてください"_jp,
                               juce::dontSendNotification);
                editor.grabKeyboardFocus();
                editor.selectAll();
                return;
            }

            auto done = onDone;
            close();   // このあと消えるので、メンバーは使わない
            if (done != nullptr)
                done (date);
        }

        void close() { exitModalState (0); }   // deleteWhenDismissed なので、次のメッセージで消える

        std::function<void (const juce::String&)> onDone;
        juce::Label title, hint, error;
        juce::TextEditor editor;
        juce::TextButton okButton, cancelButton;
    };
}

void SetlistCard::editLiveDate()
{
    auto* dialog = new LiveDateDialog (Setlist::formatLiveDate (setlist.liveDate),
                                       [safe = juce::Component::SafePointer (this)] (const juce::String& date)
    {
        if (safe == nullptr || date == safe->setlist.liveDate)
            return;
        safe->setlist.liveDate = date;
        safe->save();
        safe->repaint();
    });
    dialog->showOver (*this);
}

void SetlistCard::setFinished (bool shouldBeFinished)
{
    setlist.finished = shouldBeFinished;
    save();
    updateAlpha();
    if (onFinishedChanged != nullptr)
        onFinishedChanged();
}

void SetlistCard::requestDelete()
{
    auto options = juce::MessageBoxOptions()
                       .withIconType (juce::MessageBoxIconType::NoIcon)
                       .withTitle ("セットリストの削除"_jp)
                       .withMessage ("「"_jp + setlist.name + "」を削除しますか？\n中のセッションは削除されません。"_jp)
                       .withButton ("削除"_jp)
                       .withButton ("キャンセル"_jp)
                       .withAssociatedComponent (this);

    AppDialog::showAsync (options, [safe = juce::Component::SafePointer (this)] (int result)
    {
        if (safe == nullptr || result != 1)
            return;

        safe->store.remove (safe->setlist.setlistId);
        if (safe->onDeleted != nullptr)
            safe->onDeleted();
    });
}

void SetlistCard::updateAlpha()
{
    // ライブが終わったセットリストは薄く。ただし開いている間は、曲を見やすいように濃く (作者の指示)
    setAlpha (setlist.finished && ! expanded ? 0.5f : 1.0f);
}
