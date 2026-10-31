#include "MetalLogoLabel.h"

MetalLogoSlot::MetalLogoSlot (ThemeManager& t, juce::Component& o, std::function<juce::Rectangle<int>()> a)
    : theme (t), owner (o), area (std::move (a))
{
    theme.addChangeListener (this);
}

MetalLogoSlot::~MetalLogoSlot()
{
    theme.removeChangeListener (this);
}

void MetalLogoSlot::setText (const juce::String& logoText)
{
    if (text != logoText)
    {
        text = logoText;
        refresh();
    }
}

void MetalLogoSlot::refresh()
{
    const auto bounds = area();

    image = theme.isDeathMetal() && text.isNotEmpty()
                ? theme.getLogoCache().getImage (text, bounds.getWidth(), bounds.getHeight(),
                                                 juce::Component::getApproximateScaleFactorForComponent (&owner))
                : juce::Image();

    // ロゴは読めなくてもよいので、何のボタンかはツールチップで分かるようにする (通常モードでは出さない)
    if (auto* tooltipClient = dynamic_cast<juce::SettableTooltipClient*> (&owner))
        tooltipClient->setTooltip (image.isValid() ? text : juce::String());

    owner.repaint();
}

void MetalLogoSlot::draw (juce::Graphics& g) const
{
    // 画像は枠と同じ大きさ (の scale 倍) で作ってあるので、枠に合わせて描くだけ
    if (image.isValid())
        g.drawImage (image, area().toFloat());
}

void MetalLogoSlot::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refresh();
}

//==============================================================================
MetalLogoLabel::MetalLogoLabel (ThemeManager& theme, FixedTextId id)
    : text (getFixedText (id)),
      logo (theme, *this, [this] { return getLocalBounds(); })
{
    setTitle (text);   // スクリーンリーダー向けの名前
    logo.setText (text);
}

void MetalLogoLabel::setFont (juce::FontOptions newFont)
{
    font = std::move (newFont);
    repaint();
}

void MetalLogoLabel::setJustification (juce::Justification newJustification)
{
    justification = newJustification;
    repaint();
}

void MetalLogoLabel::paint (juce::Graphics& g)
{
    if (logo.hasImage())
    {
        logo.draw (g);
        return;
    }

    g.setColour (findColour (juce::Label::textColourId));
    g.setFont (font);
    g.drawFittedText (text, getLocalBounds(), justification, 1);
}

void MetalLogoLabel::resized()
{
    logo.refresh();
}
