#include "CountdownOverlay.h"
#include "../Text.h"
#include "../Theme/DeathMetalLookAndFeel.h"

CountdownOverlay::CountdownOverlay()
{
    setInterceptsMouseClicks (false, false);
}

void CountdownOverlay::setCountdown (int newNumber, const juce::String& newCaption)
{
    if (newNumber == number && newCaption == caption)
        return;

    number = newNumber;
    caption = newCaption;
    repaint();
}

void CountdownOverlay::paint (juce::Graphics& g)
{
    // ページの中に出すとき (Resources/Web の #countdown) と同じ見た目。
    // ただし後ろは「小節・拍」の表示だけなので、タブ譜が透けて見えるページの中より暗くする
    const auto bounds = getLocalBounds().toFloat();
    g.setColour (juce::Colours::black.withAlpha (0.8f));
    g.fillRoundedRectangle (bounds, 8.0f);

    // 数字が 0 なら、文字 (「読み込み中…」) だけを出す
    const auto numberHeight = number > 0 ? juce::jmin (280.0f, bounds.getHeight() * 0.55f) : 0.0f;
    auto area = getLocalBounds();
    const auto block = area.withSizeKeepingCentre (area.getWidth(), (int) numberHeight + 80);

    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions ("Segoe UI", numberHeight, juce::Font::bold));
    if (number > 0)
        g.drawText (juce::String (number), block.withHeight ((int) numberHeight), juce::Justification::centred);

    g.setFont (AppLookAndFeel::scaled (juce::FontOptions (26.0f, juce::Font::bold)));
    g.drawText (caption, block.withTrimmedTop ((int) numberHeight + 8).withHeight (36), juce::Justification::centred, true);

    if (number <= 0)
        return;

    g.setColour (juce::Colours::white.withAlpha (0.85f));
    g.setFont (AppLookAndFeel::scaled (juce::FontOptions (15.0f)));
    g.drawText ("▶ をもう一度押すと、すぐに始めます"_jp, block.withTrimmedTop ((int) numberHeight + 46).withHeight (24),
                juce::Justification::centred, true);
}
