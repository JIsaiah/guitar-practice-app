#include "ThemeManager.h"
#include <BinaryData.h>
#include "../Text.h"

namespace
{
    constexpr const char* themeModeKey = "themeMode";
    constexpr const char* colourThemeKey = "colourTheme";
    constexpr const char* logoParamsKey = "metalLogoParams";
    constexpr const char* textScaleKey = "textScale";
    constexpr const char* accentKey = "accentColour";
    constexpr const char* goodKey = "goodColour";

    juce::Typeface::Ptr loadMetalTypeface()
    {
        return juce::Typeface::createSystemTypefaceFor (BinaryData::DelaGothicOneRegular_ttf,
                                                        (size_t) BinaryData::DelaGothicOneRegular_ttfSize);
    }

    ColourTheme parseColourTheme (const juce::String& text)
    {
        if (text == "light") return ColourTheme::Light;
        if (text == "dark")  return ColourTheme::Dark;
        return ColourTheme::System;
    }
}

ThemeManager::ThemeManager (juce::PropertiesFile& s)
    : settings (s),
      metalTypeface (loadMetalTypeface()),
      deathMetalLookAndFeel (std::make_unique<DeathMetalLookAndFeel> (metalTypeface)),
      logoCache (metalTypeface)
{
    mode = settings.getValue (themeModeKey) == "deathMetal" ? ThemeMode::DeathMetal : ThemeMode::Normal;
    colourTheme = parseColourTheme (settings.getValue (colourThemeKey));
    textScale = juce::jlimit (0.85f, 1.3f, (float) settings.getDoubleValue (textScaleKey, 1.0));
    customAccent = parseHex (settings.getValue (accentKey));
    customGood = parseHex (settings.getValue (goodKey));

    // ロゴ調整ウィンドウで保存した値があれば使う
    if (const auto saved = juce::JSON::parse (settings.getValue (logoParamsKey)); saved.isObject())
        logoCache.setParams (MetalLogoParams::fromVar (saved));

    juce::Desktop::getInstance().addDarkModeSettingListener (this);
    apply();
}

ThemeManager::~ThemeManager()
{
    juce::Desktop::getInstance().removeDarkModeSettingListener (this);

    // LookAndFeel を消す前に、既定の LookAndFeel から外す (外さないと消えたものを参照してしまう)
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    juce::Typeface::clearTypefaceCache();
}

void ThemeManager::setMode (ThemeMode newMode)
{
    if (mode == newMode)
        return;

    mode = newMode;
    settings.setValue (themeModeKey, isDeathMetal() ? "deathMetal" : "normal");
    changed();
}

void ThemeManager::setColourTheme (ColourTheme newTheme)
{
    if (colourTheme == newTheme)
        return;

    colourTheme = newTheme;
    settings.setValue (colourThemeKey, colourTheme == ColourTheme::Light ? "light"
                                     : colourTheme == ColourTheme::Dark  ? "dark" : "system");
    changed();
}

juce::String ThemeManager::getColourThemeName (ColourTheme theme)
{
    switch (theme)
    {
        case ColourTheme::System: return "Windows に合わせる"_jp;
        case ColourTheme::Light:  return "ライト (昼)"_jp;
        case ColourTheme::Dark:   return "ダーク (夜)"_jp;
    }
    return {};
}

bool ThemeManager::isDarkActive() const
{
    if (colourTheme == ColourTheme::System)
        return juce::Desktop::getInstance().isDarkModeActive();

    return colourTheme == ColourTheme::Dark;
}

void ThemeManager::darkModeSettingChanged()
{
    // Windows のテーマが変わった。「Windows に合わせる」のときだけ切り替える
    if (colourTheme == ColourTheme::System)
        changed();
}

void ThemeManager::setTextScale (float scale)
{
    scale = juce::jlimit (0.85f, 1.3f, scale);
    if (juce::approximatelyEqual (scale, textScale))
        return;

    textScale = scale;
    settings.setValue (textScaleKey, (double) textScale);
    changed();
}

void ThemeManager::setCustomAccent (std::optional<juce::Colour> colour)
{
    customAccent = colour;
    settings.setValue (accentKey, colour.has_value() ? toHex (*colour) : juce::String());
    changed();
}

void ThemeManager::setCustomGood (std::optional<juce::Colour> colour)
{
    customGood = colour;
    settings.setValue (goodKey, colour.has_value() ? toHex (*colour) : juce::String());
    changed();
}

std::optional<juce::Colour> ThemeManager::parseHex (const juce::String& text)
{
    auto hex = text.trim();
    if (hex.startsWithChar ('#'))
        hex = hex.substring (1);

    if (hex.length() != 6 || ! hex.containsOnly ("0123456789abcdefABCDEF"))
        return std::nullopt;

    return juce::Colour ((juce::uint32) (0xff000000u | (juce::uint32) hex.getHexValue32()));
}

juce::String ThemeManager::toHex (juce::Colour colour)
{
    return "#" + colour.toDisplayString (false).toUpperCase();
}

void ThemeManager::setLogoParams (const MetalLogoParams& params)
{
    settings.setValue (logoParamsKey, juce::JSON::toString (params.toVar(), true));
    logoCache.setParams (params);
    changed();   // 表示中のロゴを作り直させる
}

void ThemeManager::changed()
{
    apply();

    // 同期的に知らせる: 次の再描画までに、ロゴの画像を作り直してもらうため
    sendSynchronousChangeMessage();
}

void ThemeManager::apply()
{
    if (isDeathMetal())
    {
        palette = Palette::deathMetal();
    }
    else
    {
        palette = Palette::fromTokens (isDarkActive() ? DesignTokens::dark : DesignTokens::light, customAccent, customGood);
        normalLookAndFeel.applyPalette (palette);
    }

    AppLookAndFeel::textScale = textScale;

    juce::LookAndFeel& lookAndFeel = isDeathMetal() ? static_cast<juce::LookAndFeel&> (*deathMetalLookAndFeel)
                                                    : normalLookAndFeel;

    // 画面ごとに setLookAndFeel() するのではなく、アプリ全体の「既定」を差し替える。
    // 確認ダイアログや設定画面のように、あとから開くウィンドウにも同じ見た目が効くため
    juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);

    // フォントは「既定のフォント名 → 実際のフォント」の対応がキャッシュされているので、捨てて引き直させる
    juce::Typeface::clearTypefaceCache();

    logoCache.clear();
    logoCache.setColour (lookAndFeel.findColour (ThemeColourIds::logoText));

    // 開いているウィンドウすべてに、見た目が変わったことを伝える
    auto& desktop = juce::Desktop::getInstance();
    for (int i = 0; i < desktop.getNumComponents(); ++i)
    {
        auto* window = desktop.getComponent (i);

        if (auto* resizable = dynamic_cast<juce::ResizableWindow*> (window))
            resizable->setBackgroundColour (lookAndFeel.findColour (juce::ResizableWindow::backgroundColourId));

        window->sendLookAndFeelChange();
        window->repaint();
    }
}
