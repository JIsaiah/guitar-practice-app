#include "SetlistEntryList.h"
#include "../Text.h"
#include "../Theme/DeathMetalLookAndFeel.h"

juce::String formatDuration (double seconds)
{
    const auto total = juce::roundToInt (juce::jmax (0.0, seconds));
    const auto h = total / 3600, m = (total / 60) % 60, s = total % 60;
    const auto ss = juce::String (s).paddedLeft ('0', 2);

    return h > 0 ? juce::String (h) + ":" + juce::String (m).paddedLeft ('0', 2) + ":" + ss
                 : juce::String (m) + ":" + ss;
}

juce::String describeOverrides (const SetlistEntry::Overrides& overrides)
{
    juce::StringArray marks;
    if (overrides.speed != 1.0)
        marks.add ("速度"_jp + juce::String (juce::roundToInt (overrides.speed * 100.0)) + "%");

    return marks.joinIntoString ("・"_jp);
}

//==============================================================================
SetlistEntryList::SetlistEntryList (Setlist& s, SessionLibrary& l)
    : setlist (s), library (l)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

int SetlistEntryList::rowAt (int y) const
{
    const auto row = y / rowHeight;
    return y >= 0 && juce::isPositiveAndBelow (row, (int) setlist.entries.size()) ? row : -1;
}

juce::Rectangle<int> SetlistEntryList::playButtonArea (int row) const
{
    return { getWidth() - 24 - 120, row * rowHeight + 8, 120, rowHeight - 16 };
}

int SetlistEntryList::insertIndexAt (int y) const
{
    return juce::jlimit (0, (int) setlist.entries.size(), juce::roundToInt ((float) y / (float) rowHeight));
}

void SetlistEntryList::paint (juce::Graphics& g)
{
    const auto text = findColour (juce::Label::textColourId);
    const auto muted = findColour (ThemeColourIds::textMuted);
    const auto accent = findColour (ThemeColourIds::accent);
    const auto width = getWidth();

    for (int i = 0; i < (int) setlist.entries.size(); ++i)
    {
        const auto& entry = setlist.entries[(size_t) i];
        auto row = juce::Rectangle<int> (0, i * rowHeight, width, rowHeight);

        if (dragging && i == pressedRow)
        {
            AppLookAndFeel::fillListHighlight (g, row, findColour (ThemeColourIds::listSelection).withMultipliedAlpha (0.5f));
        }
        else if (i == hoverRow && ! dragging)
        {
            AppLookAndFeel::fillListHighlight (g, row, findColour (ThemeColourIds::listSelection));
        }

        // 区切り線 (行の間。最初の行の上は、カードが見出しとの境目を引く)
        g.setColour (findColour (ThemeColourIds::border));
        if (i > 0)
            g.fillRect (row.getX() + 24, row.getY(), row.getWidth() - 48, 1);

        row = row.reduced (24, 0);
        const auto found = library.contains (entry.sessionId);

        g.setColour (text);
        g.setFont (AppLookAndFeel::scaled (juce::FontOptions ("Segoe UI", 16.0f, juce::Font::bold)));
        g.drawText (juce::String (i + 1), row.removeFromLeft (numberWidth), juce::Justification::centredLeft);

        // 右の「▶ ここから再生」(作者のレイアウト。見つからない曲には出さない)
        if (found)
        {
            const auto button = playButtonArea (i).toFloat();
            const bool hot = i == hoverRow && hoverOnPlay && ! dragging;
            g.setColour (findColour (juce::TextButton::buttonColourId));
            g.fillRoundedRectangle (button, 6.0f);
            g.setColour (hot ? accent : findColour (ThemeColourIds::border));
            g.drawRoundedRectangle (button.reduced (0.5f), 6.0f, hot ? 1.5f : 1.0f);
            g.setColour (findColour (juce::TextButton::textColourOffId));
            g.setFont (AppLookAndFeel::scaled (juce::FontOptions (13.0f, juce::Font::bold)));
            g.drawText ("▶ ここから再生"_jp, button, juce::Justification::centred);
        }
        row.removeFromRight (120 + 12);

        if (! found)
        {
            g.setColour (muted.withMultipliedAlpha (0.7f));
            g.setFont (AppLookAndFeel::scaled (juce::FontOptions (15.0f)));
            g.drawText ("見つかりません (右クリックで外せます)"_jp, row, juce::Justification::centredLeft);
            continue;
        }

        const auto length = library.getLengthSeconds (entry.sessionId);
        g.setColour (muted);
        g.setFont (AppLookAndFeel::scaled (juce::FontOptions ("Segoe UI", 14.0f, juce::Font::plain)));
        g.drawText (length > 0.0 ? formatDuration (length) : juce::String ("--:--"),
                    row.removeFromRight (64), juce::Justification::centredRight);

        const auto marks = describeOverrides (entry.overrides);
        if (marks.isNotEmpty())
        {
            g.setColour (accent);
            g.setFont (AppLookAndFeel::scaled (juce::FontOptions (13.0f)));
            g.drawText (marks, row.removeFromRight (140), juce::Justification::centredRight);
        }

        const auto title = library.getTitle (entry.sessionId);
        g.setColour (text);
        g.setFont (AppLookAndFeel::scaled (juce::FontOptions (16.0f, juce::Font::bold)));
        g.drawText (title.isEmpty() ? "(無題)"_jp : title, row.withTrimmedRight (8), juce::Justification::centredLeft, true);
    }

    // ドラッグ中: 離したら入る位置に線を引く
    if (dragging && insertIndex >= 0)
    {
        g.setColour (accent);
        g.fillRect (8, juce::jlimit (0, getHeight() - 3, insertIndex * rowHeight - 1), width - 16, 3);
    }
}

void SetlistEntryList::mouseMove (const juce::MouseEvent& e)
{
    const auto row = rowAt (e.y);
    const auto onPlayButton = row >= 0 && playButtonArea (row).contains (e.getPosition());
    if (row != hoverRow || onPlayButton != hoverOnPlay)
    {
        hoverRow = row;
        hoverOnPlay = onPlayButton;
        repaint();
    }
}

void SetlistEntryList::mouseExit (const juce::MouseEvent&)
{
    hoverRow = -1;
    repaint();
}

void SetlistEntryList::mouseDown (const juce::MouseEvent& e)
{
    pressedRow = rowAt (e.y);
    dragging = false;
    insertIndex = -1;

    if (pressedRow >= 0 && e.mods.isPopupMenu())
    {
        showRowMenu (pressedRow);
        pressedRow = -1;
    }
}

void SetlistEntryList::mouseDrag (const juce::MouseEvent& e)
{
    if (pressedRow < 0 || setlist.entries.size() < 2)
        return;

    if (! dragging && e.getDistanceFromDragStart() < 5)
        return;

    dragging = true;
    insertIndex = insertIndexAt (e.y);
    repaint();
}

void SetlistEntryList::mouseUp (const juce::MouseEvent& e)
{
    const auto row = pressedRow;
    const auto wasDragging = dragging;
    const auto target = insertIndex;
    pressedRow = -1;
    dragging = false;
    insertIndex = -1;
    repaint();

    if (row < 0)
        return;

    if (wasDragging)
    {
        // 境目の番号 → 移動後の位置 (自分より下に入れるときは、自分が抜けた分だけ1つ前になる)
        const auto newIndex = target > row ? target - 1 : target;
        if (newIndex != row && setlist.moveEntry (setlist.entries[(size_t) row].entryId, newIndex) && onChanged != nullptr)
            onChanged();
        return;
    }

    if (rowAt (e.y) != row || e.mods.isPopupMenu() || ! library.contains (setlist.entries[(size_t) row].sessionId))
        return;

    // 「▶ ここから再生」: この曲から通しで再生。それ以外の場所: この曲のセッションを単体で開く
    if (playButtonArea (row).contains (e.getPosition()))
    {
        if (onPlay != nullptr)
            onPlay (row);
    }
    else if (onOpenSession != nullptr)
    {
        onOpenSession (setlist.entries[(size_t) row].sessionId);
    }
}

void SetlistEntryList::showRowMenu (int row)
{
    const auto entry = setlist.entries[(size_t) row];
    const auto found = library.contains (entry.sessionId);

    juce::PopupMenu menu;
    menu.addItem (3, "この曲から通しで再生"_jp, found);
    menu.addItem (2, "元のセッションを単体で開く"_jp, found);
    menu.addSeparator();
    menu.addItem (1, "セットリストから外す"_jp);

    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition(),
                        [safe = juce::Component::SafePointer (this), row, entryId = entry.entryId, sessionId = entry.sessionId] (int result)
    {
        if (safe == nullptr)
            return;

        // セッションのファイルは消さない。セットリストの中の参照だけを外す
        if (result == 1 && safe->setlist.removeEntry (entryId) && safe->onChanged != nullptr)
            safe->onChanged();

        if (result == 2 && safe->onOpenSession != nullptr)
            safe->onOpenSession (sessionId);

        if (result == 3 && safe->onPlay != nullptr && safe->setlist.indexOf (entryId) == row)
            safe->onPlay (row);
    });
}
