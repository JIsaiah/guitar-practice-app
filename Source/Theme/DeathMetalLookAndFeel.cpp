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
    // メニューの面は角を丸く描くので、窓を「不透明ではない」にする (色の透明度で JUCE が決める。塗りは drawPopupMenuBackground)
    setColour (juce::PopupMenu::backgroundColourId, p.panel.withAlpha (0.99f));
    setColour (juce::PopupMenu::textColourId, p.text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, p.listSelection);
    setColour (juce::PopupMenu::highlightedTextColourId, p.text);
    setColour (juce::PopupMenu::headerTextColourId, p.textMuted);

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

juce::Font AppLookAndFeel::getTextButtonFont (juce::TextButton& button, int buttonHeight)
{
    // 一覧のタブ (「セッション｜セットリスト」) は大きく太く (作者の指示)
    if (button.getProperties()["segmentedTab"])
        return scaledFont (juce::jmin (18.0f, (float) buttonHeight * 0.5f), juce::Font::bold);

    // 文字の大きさを決めてあるボタン (幅の狭いボタンを3つ並べるところなど)
    if (const auto* height = button.getProperties().getVarPointer ("fontHeight"))
        return scaledFont ((float) *height, button.getProperties()["fontBold"] ? juce::Font::bold : juce::Font::plain);

    return scaledFont (juce::jmin (15.0f, (float) buttonHeight * 0.6f));
}

juce::Font AppLookAndFeel::getComboBoxFont (juce::ComboBox& box)
{
    return scaledFont (juce::jmin (15.0f, (float) box.getHeight() * 0.6f));
}

juce::Font AppLookAndFeel::getPopupMenuFont()          { return scaledFont (17.0f); }   // 前は 15 (作者の指示: 小さくて見づらい)
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

    // つながったタブ (一覧の「セッション｜セットリスト」)。帯・区切りの線・選んでいるほうの塗り (左右に動く) は
    // 画面 (SessionListScreen) が描くので、ここでは、選んでいないほうにマウスを乗せたときだけ薄く塗る
    if (button.getProperties()["segmentedTab"])
    {
        if (! button.getToggleState() && (highlighted || down))
        {
            g.setColour (findColour (ThemeColourIds::listSelection));
            g.fillRoundedRectangle (bounds, 8.0f);
        }
        return;
    }

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

void AppLookAndFeel::drawScrollbar (juce::Graphics& g, juce::ScrollBar& bar, int x, int y, int width, int height, bool vertical,
                                    int thumbStart, int thumbSize, bool isMouseOver, bool isMouseDown)
{
    // マウスが乗っている・つまんでいるときは太さいっぱい、ふだんは右 (下) 寄せの細い棒
    const bool wide = isMouseOver || isMouseDown;
    const auto thickness = wide ? (float) (vertical ? width : height) - 2.0f : 6.0f;

    juce::Rectangle<float> thumb;
    if (vertical)
        thumb = { (float) (x + width) - 1.0f - thickness, (float) thumbStart, thickness, (float) thumbSize };
    else
        thumb = { (float) thumbStart, (float) (y + height) - 1.0f - thickness, (float) thumbSize, thickness };

    const auto colour = bar.findColour (juce::ScrollBar::thumbColourId);
    g.setColour (wide ? colour.brighter (0.25f) : colour);
    g.fillRoundedRectangle (thumb.reduced (vertical ? 0.0f : 1.0f, vertical ? 1.0f : 0.0f), thickness / 2.0f);
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
// ポップアップ: JUCE の標準は、左上に大きな「？」(確認) や「！」(注意) を薄く描く。それを描かず、
// 面と細い枠だけにする。文字の位置は標準と同じ (アイコンなしのときの位置)
void AppLookAndFeel::drawAlertBox (juce::Graphics& g, juce::AlertWindow& alert,
                                   const juce::Rectangle<int>&, juce::TextLayout& textLayout)
{
    const auto bounds = alert.getLocalBounds().toFloat();

    g.setColour (alert.findColour (juce::AlertWindow::backgroundColourId));
    g.fillRoundedRectangle (bounds, cornerRadius);

    g.setColour (alert.findColour (ThemeColourIds::border));
    g.drawRoundedRectangle (bounds.reduced (0.5f), cornerRadius, 1.0f);

    g.setColour (alert.findColour (juce::AlertWindow::textColourId));
    const auto inner = alert.getLocalBounds().reduced (1);
    textLayout.draw (g, juce::Rectangle<int> (inner.getX(), 30, inner.getWidth(),
                                              inner.getHeight() - getAlertWindowButtonHeight() - 20).toFloat());
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

//==============================================================================
// 右クリックのメニュー (作者の指示: 大きくして見やすく、見た目をアプリに揃える)
namespace
{
    constexpr int menuItemHeight = 36;      // 前は 15 × 1.3 ≒ 20
    constexpr int menuSeparatorHeight = 11;
    constexpr int menuSidePadding = 14;     // 文字の左右の余白
    constexpr int menuTickWidth = 22;       // チェックの印の場所 (印が無い行も同じだけ空けて、文字の頭を揃える)
}

void AppLookAndFeel::getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int standardMenuItemHeight,
                                                int& idealWidth, int& idealHeight)
{
    if (isSeparator)
    {
        idealWidth = 50;
        idealHeight = menuSeparatorHeight;
        return;
    }

    const auto font = getPopupMenuFont();
    idealHeight = juce::jmax (standardMenuItemHeight, juce::roundToInt ((float) menuItemHeight * juce::jmax (1.0f, textScale)));
    idealWidth = juce::GlyphArrangement::getStringWidthInt (font, text) + menuSidePadding * 2 + menuTickWidth + 24;
    idealWidth = juce::jmax (idealWidth, 180);
}

void AppLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    const auto bounds = juce::Rectangle<int> (width, height).toFloat().reduced (0.5f);
    g.setColour (findColour (ThemeColourIds::panel));
    g.fillRoundedRectangle (bounds, 8.0f);
    g.setColour (findColour (ThemeColourIds::border));
    g.drawRoundedRectangle (bounds, 8.0f, 1.0f);
}

void AppLookAndFeel::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                                        bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& text,
                                        const juce::String& shortcutKeyText, const juce::Drawable* icon,
                                        const juce::Colour* textColourToUse)
{
    if (isSeparator)
    {
        g.setColour (findColour (ThemeColourIds::border));
        g.fillRect (area.reduced (menuSidePadding, 0).withSizeKeepingCentre (area.getWidth() - menuSidePadding * 2, 1));
        return;
    }

    auto textColour = textColourToUse != nullptr ? *textColourToUse : findColour (juce::PopupMenu::textColourId);

    // マウスを乗せた行: 一覧の行と同じ、角の丸い塗り
    if (isHighlighted && isActive)
        fillListHighlight (g, area.reduced (2, 0), findColour (ThemeColourIds::listSelection));

    g.setColour (textColour.withMultipliedAlpha (isActive ? 1.0f : 0.4f));

    auto r = area.reduced (menuSidePadding, 0);
    auto tickArea = r.removeFromLeft (menuTickWidth).toFloat();

    if (icon != nullptr)
    {
        icon->drawWithin (g, tickArea.reduced (2.0f), juce::RectanglePlacement::centred | juce::RectanglePlacement::onlyReduceInSize, 1.0f);
    }
    else if (isTicked)
    {
        // 選んでいるもの: アクセントの色のチェック
        g.setColour (findColour (ThemeColourIds::accent).withMultipliedAlpha (isActive ? 1.0f : 0.4f));
        auto tick = getTickShape (1.0f);
        g.fillPath (tick, tick.getTransformToScaleToFit (tickArea.withSizeKeepingCentre (13.0f, 13.0f), true));
        g.setColour (textColour.withMultipliedAlpha (isActive ? 1.0f : 0.4f));
    }

    if (hasSubMenu)
    {
        // 右向きの「›」
        const auto arrow = r.removeFromRight (14).toFloat();
        const auto cx = arrow.getCentreX(), cy = arrow.getCentreY();
        juce::Path path;
        path.startNewSubPath (cx - 2.5f, cy - 5.0f);
        path.lineTo (cx + 2.5f, cy);
        path.lineTo (cx - 2.5f, cy + 5.0f);
        g.strokePath (path, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    auto font = getPopupMenuFont();
    g.setFont (font);
    g.drawFittedText (text, r, juce::Justification::centredLeft, 1);

    if (shortcutKeyText.isNotEmpty())
    {
        g.setColour (findColour (ThemeColourIds::textMuted));
        g.setFont (font.withHeight (font.getHeight() * 0.8f));
        g.drawText (shortcutKeyText, r, juce::Justification::centredRight, true);
    }
}

void AppLookAndFeel::drawPopupMenuSectionHeader (juce::Graphics& g, const juce::Rectangle<int>& area, const juce::String& sectionName)
{
    g.setColour (findColour (ThemeColourIds::textMuted));
    g.setFont (scaledFont (13.0f, juce::Font::bold));
    g.drawFittedText (sectionName, area.reduced (menuSidePadding, 0).withTrimmedTop (4), juce::Justification::bottomLeft, 1);
}
