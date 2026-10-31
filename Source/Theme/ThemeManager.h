#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_data_structures/juce_data_structures.h>
#include "DeathMetalLookAndFeel.h"
#include "Palette.h"
#include "MetalLogo/MetalLogoCache.h"

enum class ThemeMode { Normal, DeathMetal };

// 通常モードのテーマ (設計書 v0.7 4.7: 既定は Windows のテーマに従い、手動でも切り替えられる)
enum class ColourTheme { System, Light, Dark };

// 見た目を管理する (death-metal-mode.md 4.1、設計書 v0.7 4.7)
// - テーマ (Windows に合わせる／ライト／ダーク) と、デスメタルモードの有無を、アプリの設定ファイルに保存する
// - 切り替えると、既定の LookAndFeel を差し替え、ロゴのキャッシュを捨て、変更を知らせる
// - フォントの読み込みと、ロゴのパラメータの保存もここで行う
class ThemeManager final : public juce::ChangeBroadcaster,
                           private juce::DarkModeSettingListener
{
public:
    explicit ThemeManager (juce::PropertiesFile& settings);
    ~ThemeManager() override;

    ThemeMode getMode() const noexcept { return mode; }
    bool isDeathMetal() const noexcept { return mode == ThemeMode::DeathMetal; }
    void setMode (ThemeMode newMode);

    ColourTheme getColourTheme() const noexcept { return colourTheme; }
    void setColourTheme (ColourTheme newTheme);
    bool isDarkActive() const;   // 実際に使っているのがダークか (System のときは Windows の設定)
    static juce::String getColourThemeName (ColourTheme theme);

    const Palette& getPalette() const noexcept { return palette; }
    juce::Typeface::Ptr getMetalTypeface() const noexcept { return metalTypeface; }
    MetalLogoCache& getLogoCache() noexcept { return logoCache; }

    // 文字の大きさの倍率 (0.85〜1.3)。アプリ全体の文字に効く
    float getTextScale() const noexcept { return textScale; }
    void setTextScale (float scale);

    // 色の調整 (設定画面の詳細設定)。背景と文字の色は変えられない
    std::optional<juce::Colour> getCustomAccent() const noexcept { return customAccent; }
    std::optional<juce::Colour> getCustomGood() const noexcept { return customGood; }
    void setCustomAccent (std::optional<juce::Colour> colour);
    void setCustomGood (std::optional<juce::Colour> colour);

    // "#RRGGBB" を色にする。形が違えば nullopt
    static std::optional<juce::Colour> parseHex (const juce::String& text);
    static juce::String toHex (juce::Colour colour);

    // ロゴ調整ウィンドウで決めた値を保存し、表示中のロゴに反映する
    void setLogoParams (const MetalLogoParams& params);

private:
    void apply();
    void changed();
    void darkModeSettingChanged() override;

    juce::PropertiesFile& settings;
    ThemeMode mode = ThemeMode::Normal;
    ColourTheme colourTheme = ColourTheme::System;
    float textScale = 1.0f;
    std::optional<juce::Colour> customAccent, customGood;
    Palette palette;

    juce::Typeface::Ptr metalTypeface;
    NormalLookAndFeel normalLookAndFeel;
    std::unique_ptr<DeathMetalLookAndFeel> deathMetalLookAndFeel;
    MetalLogoCache logoCache;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ThemeManager)
};
