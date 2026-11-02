#include "SetlistCard.h"
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

    addButton.setButtonText ("＋ 追加"_jp);
    addButton.setClickingTogglesState (false);
    addButton.onClick = [this] { if (onRequestAdd != nullptr) onRequestAdd(); };
    addChildComponent (addButton);

    entryList.onPlay = [this] (int index) { if (onPlay != nullptr) onPlay (index); };
    entryList.onOpenSession = [this] (const juce::String& id) { if (onOpenSession != nullptr) onOpenSession (id); };
    entryList.onChanged = [this] { changed(); };
    addChildComponent (entryList);

    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    updatePlayButton();
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
    entryList.setVisible (expanded);
    addButton.setVisible (expanded);
    repaint();

    if (onLayoutChanged != nullptr)
        onLayoutChanged();
}

int SetlistCard::getPreferredHeight() const
{
    return expanded ? headerHeight + entryList.getPreferredHeight() + addRowHeight : headerHeight;
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
    addButton.setToggleState (isAdding, juce::dontSendNotification);
}

void SetlistCard::save()
{
    if (! store.save (setlist))
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
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
    header.removeFromRight (playButton.getWidth() + 16);

    // 展開の印
    const auto arrowArea = header.removeFromLeft (20).toFloat();
    juce::Path arrow;
    arrow.addTriangle (0.0f, 0.0f, 10.0f, 0.0f, 5.0f, 6.0f);
    if (! expanded)
        arrow.applyTransform (juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi, 5.0f, 3.0f));
    g.setColour (findColour (ThemeColourIds::textMuted));
    g.fillPath (arrow, juce::AffineTransform::translation (arrowArea.getX(), arrowArea.getCentreY() - 3.0f));
    header.removeFromLeft (6);

    g.setColour (findColour (juce::Label::textColourId));
    g.setFont (AppLookAndFeel::scaled (juce::FontOptions (18.0f, juce::Font::bold)));
    g.drawText (setlist.name, header.withTrimmedBottom (headerHeight / 2 - 4).withTrimmedTop (10),
                juce::Justification::bottomLeft, true);

    g.setColour (findColour (ThemeColourIds::textMuted));
    g.setFont (AppLookAndFeel::scaled (juce::FontOptions (13.0f)));
    g.drawText (setlist.entries.empty() ? "曲がありません。開いて「＋ 追加」から追加できます"_jp : describeSummary(),
                header.withTrimmedTop (headerHeight / 2 + 4), juce::Justification::topLeft, true);
}

void SetlistCard::resized()
{
    auto area = getLocalBounds();
    auto header = area.removeFromTop (headerHeight);
    playButton.setBounds (header.removeFromRight (200).reduced (16, 12));

    entryList.setBounds (area.removeFromTop (entryList.getPreferredHeight()));
    addButton.setBounds (area.removeFromTop (addRowHeight).reduced (16, 8).removeFromLeft (160));
}

void SetlistCard::mouseUp (const juce::MouseEvent& e)
{
    if (e.y >= headerHeight || e.mouseWasDraggedSinceMouseDown())
        return;

    if (e.mods.isPopupMenu())
        showHeaderMenu();
    else if (e.getNumberOfClicks() == 1)
        setExpanded (! expanded);
}

void SetlistCard::mouseDoubleClick (const juce::MouseEvent& e)
{
    // 名前のダブルクリックで名前を変える (1回目のクリックで開閉しているので、元に戻す)
    if (e.y < headerHeight && ! e.mods.isPopupMenu())
    {
        setExpanded (! expanded);
        rename();
    }
}

void SetlistCard::showHeaderMenu()
{
    juce::PopupMenu menu;
    menu.addItem (1, "名前を変更…"_jp);
    menu.addSeparator();
    menu.addItem (2, "セットリストを削除…"_jp);

    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition(),
                        [safe = juce::Component::SafePointer (this)] (int result)
    {
        if (safe == nullptr)
            return;

        if (result == 1) safe->rename();
        if (result == 2) safe->requestDelete();
    });
}

void SetlistCard::rename()
{
    // 名前の変更だけは、キーボードでの入力を使う (4.5)
    auto* window = new juce::AlertWindow ("セットリストの名前"_jp, {}, juce::MessageBoxIconType::NoIcon, this);
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

void SetlistCard::requestDelete()
{
    auto options = juce::MessageBoxOptions()
                       .withIconType (juce::MessageBoxIconType::QuestionIcon)
                       .withTitle ("セットリストの削除"_jp)
                       .withMessage ("「"_jp + setlist.name + "」を削除しますか？\n中のセッションは削除されません。"_jp)
                       .withButton ("削除"_jp)
                       .withButton ("キャンセル"_jp)
                       .withAssociatedComponent (this);

    juce::AlertWindow::showAsync (options, [safe = juce::Component::SafePointer (this)] (int result)
    {
        if (safe == nullptr || result != 1)
            return;

        safe->store.remove (safe->setlist.setlistId);
        if (safe->onDeleted != nullptr)
            safe->onDeleted();
    });
}
