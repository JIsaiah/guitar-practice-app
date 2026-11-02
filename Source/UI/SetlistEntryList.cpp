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
    if (overrides.loopEnabled)
        marks.add ("ループ"_jp);

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
            g.setColour (findColour (ThemeColourIds::listSelection).withMultipliedAlpha (0.5f));
            g.fillRect (row);
        }
        else if (i == hoverRow && ! dragging)
        {
            g.setColour (findColour (ThemeColourIds::listSelection));
            g.fillRect (row);
        }

        // 区切り線 (最初の行の上は、カードの見出しとの境目として引く)
        g.setColour (findColour (ThemeColourIds::border));
        g.fillRect (row.getX() + 12, row.getY(), row.getWidth() - 24, 1);

        row = row.reduced (12, 0);
        const auto found = library.contains (entry.sessionId);

        g.setColour (muted);
        g.setFont (AppLookAndFeel::scaled (juce::FontOptions ("Segoe UI", 14.0f, juce::Font::plain)));
        g.drawText (juce::String (i + 1), row.removeFromLeft (32), juce::Justification::centredLeft);

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
        g.setFont (AppLookAndFeel::scaled (juce::FontOptions (15.0f)));
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
    if (row != hoverRow)
    {
        hoverRow = row;
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

    if (rowAt (e.y) == row && ! e.mods.isPopupMenu() && onPlay != nullptr)
        onPlay (row);
}

void SetlistEntryList::showRowMenu (int row)
{
    const auto entry = setlist.entries[(size_t) row];
    const auto found = library.contains (entry.sessionId);

    juce::PopupMenu menu;
    menu.addItem (1, "セットリストから外す"_jp);
    menu.addItem (2, "元のセッションを単体で開く"_jp, found);

    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition(),
                        [safe = juce::Component::SafePointer (this), entryId = entry.entryId, sessionId = entry.sessionId] (int result)
    {
        if (safe == nullptr)
            return;

        // セッションのファイルは消さない。セットリストの中の参照だけを外す
        if (result == 1 && safe->setlist.removeEntry (entryId) && safe->onChanged != nullptr)
            safe->onChanged();

        if (result == 2 && safe->onOpenSession != nullptr)
            safe->onOpenSession (sessionId);
    });
}
