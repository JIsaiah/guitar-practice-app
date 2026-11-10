#include "DeathMetalLookAndFeel.h"

namespace
{
    constexpr float cornerRadius = 6.0f;

    juce::Font scaledFont (float height, int style = juce::Font::plain)
    {
        return juce::Font (AppLookAndFeel::scaled (juce::FontOptions (height, style)));
    }
}

//==============================================================================
void AppLookAndFeel::applyPalette (const Palette& p)
{
    // LookAndFeel_V4 の配色の仕組み (ColourScheme) で、部品の色をまとめて変える
    setColourScheme ({ p.background,   // windowBackground
                       p.panel,        // widgetBackground
                       p.panel,        // menuBackground
                       p.border,       // outline
                       p.text,         // defaultText
                       p.accent,       // defaultFill
                       p.onAccent,     // highlightedText
                       p.accent,       // highlightedFill
                       p.text });      // menuText

    // ColourScheme で決まらない部分を個別に上書きする
    setColour (juce::TextButton::buttonColourId, p.button);
    setColour (juce::TextButton::buttonOnColourId, p.buttonDown);
    setColour (juce::TextButton::textColourOffId, p.text);
    setColour (juce::TextButton::textColourOnId, juce::Colours::white);   // オンのボタン (アクセント色の塗り) の文字は白 (作者の指示)
    setColour (juce::Slider::thumbColourId, p.sliderThumb);
    setColour (juce::Slider::trackColourId, p.accent);
    setColour (juce::Slider::backgroundColourId, p.border);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxTextColourId, p.text);
    setColour (juce::ToggleButton::tickColourId, p.accent);
    setColour (juce::ListBox::backgroundColourId, p.panel);
    setColour (juce::TextEditor::backgroundColourId, p.background);
    setColour (juce::TextEditor::outlineColourId, p.border);
    setColour (juce::TextEditor::focusedOutlineColourId, p.accent);
    setColour (juce::ComboBox::backgroundColourId, p.button);
    setColour (juce::ComboBox::outlineColourId, p.border);
    setColour (juce::ComboBox::arrowColourId, p.textMuted);
    setColour (juce::ScrollBar::thumbColourId, p.border.brighter (0.2f));

    setColour (ThemeColourIds::listSelection, p.listSelection);
    setColour (ThemeColourIds::timelineProgress, p.timelineProgress);
    setColour (ThemeColourIds::logoText, p.logoText);
    setColour (ThemeColourIds::panel, p.panel);
    setColour (ThemeColourIds::textMuted, p.textMuted);
    setColour (ThemeColourIds::accent, p.accent);
    setColour (ThemeColourIds::onAccent, p.onAccent);
    setColour (ThemeColourIds::border, p.border);
    setColour (ThemeColourIds::good, p.good);
}

//==============================================================================
// 文字: 部品が持っている大きさに、文字の大きさの倍率を掛ける
juce::Font AppLookAndFeel::getLabelFont (juce::Label& label)
{
    // ラベルが持っている Font は、一度使った書体を中に覚えている。
    // そのまま使うと、デスメタルモードを解除しても Dela Gothic One のままになるので、覚えた書体を捨てた写しを返す
    const auto font = withoutCachedTypeface (label.getFont());
    return font.withHeight (font.getHeight() * textScale);
}

juce::Font AppLookAndFeel::withoutCachedTypeface (const juce::Font& font)
{
    // JUCE の Font は、横の倍率を設定し直すと、覚えていた書体を捨てる (値は同じでも)。
    // 次に描くときに、そのときの LookAndFeel の書体を引き直す
    return font.withHorizontalScale (font.getHorizontalScale());
}

juce::Font AppLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return scaledFont (juce::jmin (15.0f, (float) buttonHeight * 0.6f));
}

juce::Font AppLookAndFeel::getComboBoxFont (juce::ComboBox& box)
{
    return scaledFont (juce::jmin (15.0f, (float) box.getHeight() * 0.6f));
}

juce::Font AppLookAndFeel::getPopupMenuFont()          { return scaledFont (15.0f); }
juce::Font AppLookAndFeel::getAlertWindowMessageFont() { return scaledFont (15.0f); }
juce::Font AppLookAndFeel::getAlertWindowTitleFont()   { return scaledFont (18.0f, juce::Font::bold); }
juce::Font AppLookAndFeel::getAlertWindowFont()        { return scaledFont (14.0f); }

//==============================================================================
// ボタン: 平らで角の丸い形。押している状態 (トグル) はアクセントで塗る
void AppLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour,
                                           bool highlighted, bool down)
{
    auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    auto fill = backgroundColour;

    if (! button.isEnabled())
        fill = fill.withMultipliedAlpha (0.5f);
    else if (down)
        fill = fill.contrasting (0.12f);
    else if (highlighted)
        fill = fill.contrasting (0.06f);

    g.setColour (fill);
    g.fillRoundedRectangle (bounds, cornerRadius);

    const bool on = button.getToggleState();
    g.setColour (on ? findColour (ThemeColourIds::accent) : findColour (ThemeColourIds::border));
    g.drawRoundedRectangle (bounds, cornerRadius, highlighted && ! on ? 1.5f : 1.0f);
}

void AppLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button, bool highlighted, bool down)
{
    const auto fontSize = juce::jmin (15.0f, (float) button.getHeight() * 0.75f);
    const auto tickWidth = fontSize * 1.1f;

    drawTickBox (g, button, 4.0f, ((float) button.getHeight() - tickWidth) * 0.5f, tickWidth, tickWidth,
                 button.getToggleState(), button.isEnabled(), highlighted, down);

    g.setColour (button.findColour (juce::ToggleButton::textColourId).withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.5f));
    g.setFont (scaledFont (fontSize));
    g.drawFittedText (button.getButtonText(),
                      button.getLocalBounds().withTrimmedLeft (juce::roundToInt (tickWidth) + 10).withTrimmedRight (2),
                      juce::Justification::centredLeft, 2);
}

// スライダー: 細い溝と、丸いつまみ。値までの部分をアクセントで塗る
void AppLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                                       float sliderPos, float minSliderPos, float maxSliderPos,
                                       juce::Slider::SliderStyle style, juce::Slider& slider)
{
    if (style != juce::Slider::LinearHorizontal)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);
        return;
    }

    const auto alpha = slider.isEnabled() ? 1.0f : 0.4f;
    const auto centreY = (float) y + (float) height * 0.5f;
    const auto track = juce::Rectangle<float> ((float) x, centreY - 2.0f, (float) width, 4.0f);

    g.setColour (slider.findColour (juce::Slider::backgroundColourId).withMultipliedAlpha (alpha));
    g.fillRoundedRectangle (track, 2.0f);

    g.setColour (slider.findColour (juce::Slider::trackColourId).withMultipliedAlpha (alpha));
    g.fillRoundedRectangle (track.withRight (sliderPos), 2.0f);

    const auto thumb = juce::Rectangle<float> (16.0f, 16.0f).withCentre ({ sliderPos, centreY });
    g.setColour (findColour (ThemeColourIds::panel));
    g.fillEllipse (thumb);
    g.setColour (slider.findColour (juce::Slider::thumbColourId).withMultipliedAlpha (alpha));
    g.drawEllipse (thumb.reduced (1.0f), 2.5f);
}

void AppLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool,
                                   int, int, int, int, juce::ComboBox& box)
{
    const auto bounds = juce::Rectangle<int> (width, height).toFloat().reduced (0.5f);
    g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle (bounds, cornerRadius);
    g.setColour (box.hasKeyboardFocus (true) ? findColour (ThemeColourIds::accent) : box.findColour (juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle (bounds, cornerRadius, 1.0f);

    // 下向きの山形 (シェブロン)
    const auto arrow = juce::Rectangle<float> ((float) width - 26.0f, (float) height * 0.5f - 3.0f, 10.0f, 6.0f);
    juce::Path chevron;
    chevron.startNewSubPath (arrow.getX(), arrow.getY());
    chevron.lineTo (arrow.getCentreX(), arrow.getBottom());
    chevron.lineTo (arrow.getRight(), arrow.getY());
    g.setColour (box.findColour (juce::ComboBox::arrowColourId).withMultipliedAlpha (box.isEnabled() ? 1.0f : 0.4f));
    g.strokePath (chevron, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void AppLookAndFeel::fillTextEditorBackground (juce::Graphics& g, int width, int height, juce::TextEditor& editor)
{
    g.setColour (editor.findColour (juce::TextEditor::backgroundColourId));
    g.fillRoundedRectangle (juce::Rectangle<int> (width, height).toFloat(), cornerRadius);
}

void AppLookAndFeel::drawTextEditorOutline (juce::Graphics& g, int width, int height, juce::TextEditor& editor)
{
    if (! editor.isEnabled())
        return;

    g.setColour (editor.hasKeyboardFocus (true) && ! editor.isReadOnly()
                     ? editor.findColour (juce::TextEditor::focusedOutlineColourId)
                     : editor.findColour (juce::TextEditor::outlineColourId));
    g.drawRoundedRectangle (juce::Rectangle<int> (width, height).toFloat().reduced (0.5f), cornerRadius, 1.0f);
}

//==============================================================================
NormalLookAndFeel::NormalLookAndFeel()
{
    setDefaultSansSerifTypefaceName (DesignTokens::Font::ui);
    applyPalette (Palette::fromTokens (DesignTokens::dark));
}

DeathMetalLookAndFeel::DeathMetalLookAndFeel (juce::Typeface::Ptr delaGothicOne)
    : typeface (std::move (delaGothicOne))
{
    applyPalette (Palette::deathMetal());
    setColour (juce::TextButton::textColourOnId, Palette::deathMetal().text.brighter (0.4f));
}

juce::Typeface::Ptr DeathMetalLookAndFeel::getTypefaceForFont (const juce::Font& font)
{
    // 名前を指定しない文字 (= 既定のフォント) は、すべて Dela Gothic One にする
    // Dela に無い文字 (絵文字や記号など) は、JUCE が自動でシステムのフォントに切り替えて描く
    if (typeface != nullptr && font.getTypefaceName() == juce::Font::getDefaultSansSerifFontName())
        return typeface;

    return LookAndFeel_V4::getTypefaceForFont (font);
}
