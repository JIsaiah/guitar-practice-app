#include "FixedTextButton.h"

FixedTextButton::FixedTextButton (ThemeManager& theme)
    : logo (theme, *this, [this] { return getLogoArea(); })
{
}

void FixedTextButton::setFixedText (FixedTextId id, const juce::String& normalText)
{
    setButtonText (normalText);   // アクセシビリティにも、この文字列が使われる
    logo.setText (getFixedText (id));
}

void FixedTextButton::setMarked (bool shouldBeMarked)
{
    if (marked != shouldBeMarked)
    {
        marked = shouldBeMarked;
        repaint();
    }
}

juce::Rectangle<int> FixedTextButton::getLogoArea() const
{
    return getLocalBounds().reduced (6, 4);
}

void FixedTextButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    if (! logo.hasImage())
    {
        TextButton::paintButton (g, highlighted, down);
        return;
    }

    // 背景は普通のボタンと同じものを描き、文字の代わりにロゴを描く
    auto& lf = getLookAndFeel();
    lf.drawButtonBackground (g, *this,
                             findColour (getToggleState() ? buttonOnColourId : buttonColourId),
                             highlighted, down);

    g.setOpacity (isEnabled() ? 1.0f : 0.35f);
    logo.draw (g);

    if (marked)
    {
        g.setColour (findColour (juce::Slider::thumbColourId).brighter (0.5f));
        g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withPosition ((float) getWidth() - 11.0f, 4.0f));
    }
}

void FixedTextButton::resized()
{
    TextButton::resized();
    logo.refresh();
}

//==============================================================================
FixedTextToggle::FixedTextToggle (ThemeManager& theme, FixedTextId id)
    : logo (theme, *this, [this] { return getLogoArea(); })
{
    setButtonText (getFixedText (id));
    logo.setText (getFixedText (id));
}

// LookAndFeel_V4::drawToggleButton と同じ大きさ・位置にチェックの枠を描く
float FixedTextToggle::getTickSize() const
{
    return juce::jmin (15.0f, (float) getHeight() * 0.75f) * 1.1f;
}

juce::Rectangle<int> FixedTextToggle::getLogoArea() const
{
    return getLocalBounds().withTrimmedLeft (juce::roundToInt (getTickSize()) + 10).reduced (0, 2);
}

void FixedTextToggle::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    if (! logo.hasImage())
    {
        ToggleButton::paintButton (g, highlighted, down);
        return;
    }

    const auto tickSize = getTickSize();
    getLookAndFeel().drawTickBox (g, *this, 4.0f, ((float) getHeight() - tickSize) * 0.5f, tickSize, tickSize,
                                  getToggleState(), isEnabled(), highlighted, down);
    logo.draw (g);
}

void FixedTextToggle::resized()
{
    ToggleButton::resized();
    logo.refresh();
}
