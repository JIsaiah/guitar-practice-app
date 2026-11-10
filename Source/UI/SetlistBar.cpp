#include "SetlistBar.h"
#include "../Text.h"
#include "../Theme/DeathMetalLookAndFeel.h"

SetlistBar::SetlistBar()
{
    previousButton.setButtonText ("◀ 前の曲"_jp);
    nextButton.setButtonText ("次の曲 ▶"_jp);
    previousButton.onClick = [this] { if (onPrevious != nullptr) onPrevious(); };
    nextButton.onClick = [this] { if (onNext != nullptr) onNext(); };
    stopButton.onClick = [this] { if (onAutoAdvanceToggled != nullptr) onAutoAdvanceToggled (! info.autoAdvance); };

    // スペースキーなどは練習画面で受け取る (ボタンにキーボードを取らせない)
    for (auto* b : { &previousButton, &nextButton, &stopButton })
    {
        b->setMouseClickGrabsKeyboardFocus (false);
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (b);
    }

    setInfo ({});
}

void SetlistBar::setInfo (const Info& newInfo)
{
    info = newInfo;
    previousButton.setEnabled (info.hasPrevious);
    nextButton.setEnabled (info.hasNext);
    stopButton.setButtonText (info.autoAdvance ? "■ 通し再生を止める"_jp : "▶ 通し再生を再開"_jp);
    repaint();
}

void SetlistBar::setProgress (double newProgress)
{
    newProgress = juce::jlimit (0.0, 1.0, newProgress);
    if (std::abs (newProgress - progress) < 0.0005)
        return;

    progress = newProgress;
    repaint (progressArea);
}

void SetlistBar::setNotice (const juce::String& newNotice)
{
    if (newNotice != notice)
    {
        notice = newNotice;
        repaint();
    }
}

void SetlistBar::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour (findColour (ThemeColourIds::panel));
    g.fillRoundedRectangle (bounds, 10.0f);
    g.setColour (findColour (ThemeColourIds::border));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 10.0f, 1.0f);

    const auto text = findColour (juce::Label::textColourId);
    const auto muted = findColour (ThemeColourIds::textMuted);
    const auto accent = findColour (ThemeColourIds::accent);

    // 左: セットリスト名と「3 / 8」
    auto area = textArea;
    auto left = area.removeFromLeft (juce::jmin (260, area.getWidth() / 3));
    g.setColour (muted);
    g.setFont (AppLookAndFeel::scaled (juce::FontOptions (12.5f, juce::Font::bold)));
    g.drawText ("セットリスト"_jp, left.removeFromTop (headingRowHeight).withTrimmedTop (4), juce::Justification::bottomLeft, true);
    g.setColour (text);
    g.setFont (AppLookAndFeel::scaled (juce::FontOptions (16.0f, juce::Font::bold)));
    g.drawText (info.setlistName + "   " + juce::String (info.position) + " / " + juce::String (info.count),
                left, juce::Justification::centredLeft, true);

    // 中央: 上の行に「次の曲」の見出しと注意 (ループ中、通し再生を止めた)、下の行に次の曲名を大きく
    // (作者の指示: 演奏中に離れた位置からでも読めるように、曲名を大きくした)
    area.removeFromLeft (16);
    auto upper = area.removeFromTop (headingRowHeight).withTrimmedTop (4);
    const auto headingFont = juce::Font (AppLookAndFeel::scaled (juce::FontOptions (12.5f, juce::Font::bold)));
    const auto heading = info.nextTitle.isNotEmpty() ? "次の曲"_jp : "最後の曲です"_jp;
    g.setColour (muted);
    g.setFont (headingFont);
    g.drawText (heading, upper, juce::Justification::bottomLeft, true);

    const auto message = ! info.autoAdvance ? "通し再生を止めています。曲が終わっても次に進みません"_jp : notice;
    if (message.isNotEmpty())
    {
        juce::GlyphArrangement glyphs;
        glyphs.addLineOfText (headingFont, heading, 0.0f, 0.0f);
        auto messageArea = upper.withTrimmedLeft ((int) std::ceil (glyphs.getBoundingBox (0, -1, true).getWidth()) + 16);
        g.setColour (accent);
        g.setFont (AppLookAndFeel::scaled (juce::FontOptions (13.0f)));
        g.drawText (message, messageArea, juce::Justification::bottomLeft, true);
    }

    if (info.nextTitle.isNotEmpty())
    {
        g.setColour (text);
        g.setFont (AppLookAndFeel::scaled (juce::FontOptions (26.0f, juce::Font::bold)));
        g.drawText (info.nextTitle, area, juce::Justification::centredLeft, true);
    }

    // 下: セットリスト全体の進み具合
    const auto bar = progressArea.toFloat();
    g.setColour (findColour (ThemeColourIds::border));
    g.fillRoundedRectangle (bar, 2.0f);
    g.setColour (accent);
    g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * (float) progress), 2.0f);
}

void SetlistBar::resized()
{
    auto area = getLocalBounds().reduced (14, 0);
    progressArea = area.removeFromBottom (10).removeFromTop (4);

    auto buttons = area.withSizeKeepingCentre (area.getWidth(), juce::jmin (40, area.getHeight() - 16));   // 帯を高くしても、ボタンは前の高さ
    stopButton.setBounds (buttons.removeFromRight (170));
    buttons.removeFromRight (12);
    nextButton.setBounds (buttons.removeFromRight (120));
    buttons.removeFromRight (6);
    previousButton.setBounds (buttons.removeFromRight (120));

    textArea = area.withRight (previousButton.getX() - 16);
}
