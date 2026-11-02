#include "SessionPickerPanel.h"
#include "../Text.h"
#include "../Theme/DeathMetalLookAndFeel.h"

SessionPickerPanel::SessionPickerPanel()
{
    listBox.setRowHeight (48);
    listBox.setMouseCursor (juce::MouseCursor::PointingHandCursor);
    addAndMakeVisible (listBox);

    closeButton.setButtonText ("閉じる"_jp);
    closeButton.onClick = [this] { if (onClose != nullptr) onClose(); };
    addAndMakeVisible (closeButton);
}

void SessionPickerPanel::setSessions (std::vector<SessionStore::Entry> newSessions)
{
    sessions = std::move (newSessions);
    listBox.updateContent();
    listBox.repaint();
}

void SessionPickerPanel::setTarget (const juce::String& setlistName, std::function<int (const juce::String&)> countInSetlist)
{
    targetName = setlistName;
    countOf = std::move (countInSetlist);
    listBox.repaint();
    repaint();
}

void SessionPickerPanel::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool)
{
    if (! juce::isPositiveAndBelow (row, (int) sessions.size()))
        return;

    const auto& entry = sessions[(size_t) row];

    auto area = juce::Rectangle<int> (12, 0, width - 24, height);

    const auto count = countOf != nullptr ? countOf (entry.sessionId) : 0;
    if (count > 0)
    {
        g.setColour (listBox.findColour (ThemeColourIds::accent));
        g.setFont (AppLookAndFeel::scaled (juce::FontOptions (13.0f)));
        g.drawText (count == 1 ? "追加済み"_jp : "追加済み×"_jp + juce::String (count),
                    area.removeFromRight (90), juce::Justification::centredRight);
    }

    g.setColour (listBox.findColour (juce::ListBox::textColourId));
    g.setFont (AppLookAndFeel::scaled (juce::FontOptions (16.0f)));
    g.drawText (entry.title.isEmpty() ? "(無題)"_jp : entry.title, area, juce::Justification::centredLeft, true);
}

void SessionPickerPanel::listBoxItemClicked (int row, const juce::MouseEvent&)
{
    if (! juce::isPositiveAndBelow (row, (int) sessions.size()))
        return;

    listBox.deselectAllRows();   // 選んだ状態は使わない (押すたびに追加する)

    if (onAdd != nullptr)
        onAdd (sessions[(size_t) row].sessionId);

    listBox.repaintRow (row);
}

void SessionPickerPanel::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (findColour (ThemeColourIds::panel));
    g.fillRoundedRectangle (bounds, 10.0f);
    g.setColour (findColour (ThemeColourIds::border));
    g.drawRoundedRectangle (bounds, 10.0f, 1.0f);

    auto header = getLocalBounds().reduced (16, 0).removeFromTop (72);
    g.setColour (findColour (juce::Label::textColourId));
    g.setFont (AppLookAndFeel::scaled (juce::FontOptions (17.0f, juce::Font::bold)));
    g.drawText ("「"_jp + targetName + "」に追加"_jp, header.removeFromTop (44), juce::Justification::bottomLeft, true);

    g.setColour (findColour (ThemeColourIds::textMuted));
    g.setFont (AppLookAndFeel::scaled (juce::FontOptions (13.0f)));
    g.drawText ("クリックするたびに、末尾に1曲追加します"_jp, header, juce::Justification::centredLeft, true);

    if (sessions.empty())
        g.drawFittedText ("セッションがありません。\n先に「セッション」のタブで作成してください。"_jp,
                          listBox.getBounds(), juce::Justification::centred, 2);
}

void SessionPickerPanel::resized()
{
    auto area = getLocalBounds().reduced (8);
    area.removeFromTop (72);
    closeButton.setBounds (area.removeFromBottom (48).reduced (8, 4));
    area.removeFromBottom (4);
    listBox.setBounds (area);
}
