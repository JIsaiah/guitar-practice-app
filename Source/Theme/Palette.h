#pragma once

#include <juce_graphics/juce_graphics.h>
#include "../UI/DesignTokens.h"

// JUCE の部品に無い、このアプリ独自の色の ID (findColour で取り出す)
// すべての LookAndFeel に必ず設定する (未設定の ID を findColour すると assert で止まる)
namespace ThemeColourIds
{
    enum
    {
        listSelection    = 0x7a00001,  // 一覧の選択行
        timelineProgress = 0x7a00002,  // 再生位置バーの再生済み部分
        logoText         = 0x7a00003,  // ロゴ・決まった文言の文字色
        panel            = 0x7a00004,  // 面 (カード、パネル)
        textMuted        = 0x7a00005,  // 補助の文字
        accent           = 0x7a00006,  // アクセント (琥珀色)
        onAccent         = 0x7a00007,  // アクセントの塗りの上の文字
        border           = 0x7a00008,  // 枠線
        good             = 0x7a00009   // 合っている (チューナー)
    };
}

// 配色。通常モードはデザイントークン (DesignTokens.h) から作り、デスメタルモードだけ独自の値を持つ
struct Palette
{
    juce::Colour background, panel, button, buttonDown, text, textMuted, accent, onAccent, border, sliderThumb;
    juce::Colour listSelection, timelineProgress, logoText, good;

    // コントラスト比 (WCAG)。アクセントの上の文字色を選ぶのに使う
    static float contrast (juce::Colour a, juce::Colour b)
    {
        const auto lum = [] (juce::Colour c)
        {
            const auto ch = [] (float v) { return v <= 0.03928f ? v / 12.92f : std::pow ((v + 0.055f) / 1.055f, 2.4f); };
            return 0.2126f * ch (c.getFloatRed()) + 0.7152f * ch (c.getFloatGreen()) + 0.0722f * ch (c.getFloatBlue());
        };
        const auto la = lum (a), lb = lum (b);
        return (juce::jmax (la, lb) + 0.05f) / (juce::jmin (la, lb) + 0.05f);
    }

    // accentOverride / goodOverride: 設定画面で HEX で指定した色 (無ければトークンの色)
    static Palette fromTokens (const DesignTokens::Theme& t,
                               std::optional<juce::Colour> accentOverride = std::nullopt,
                               std::optional<juce::Colour> goodOverride = std::nullopt)
    {
        Palette p;
        p.background       = t.background;
        p.panel            = t.surface;
        p.button           = t.surface;
        p.buttonDown       = t.accent;
        p.text             = t.text;
        p.textMuted        = t.textMuted;
        p.accent           = t.accent;
        p.onAccent         = t.onAccent;
        p.border           = t.border;
        p.sliderThumb      = t.accent;
        p.listSelection    = t.accent.withAlpha (0.22f);
        p.timelineProgress = t.accent.withAlpha (0.7f);
        p.logoText         = t.text;
        p.good             = juce::Colour (0xff3fa86b);   // チューナーの「合っている」。形でも示す (設計書 4.7)

        if (accentOverride.has_value())
        {
            const auto a = accentOverride->withAlpha (1.0f);
            p.accent = p.buttonDown = p.sliderThumb = a;
            p.listSelection = a.withAlpha (0.22f);
            p.timelineProgress = a.withAlpha (0.7f);

            // アクセントの塗りの上の文字は、背景色と文字色のうち、読みやすいほうにする
            p.onAccent = contrast (a, t.background) >= contrast (a, t.text) ? t.background : t.text;
        }

        if (goodOverride.has_value())
            p.good = goodOverride->withAlpha (1.0f);

        return p;
    }

    // デスメタルモード: 黒・暗赤・灰 (death-metal-mode.md 4.2)
    static Palette deathMetal()
    {
        Palette p;
        p.background       = juce::Colour (0xff0a0a0a);
        p.panel            = juce::Colour (0xff141414);
        p.button           = juce::Colour (0xff1a0000);
        p.buttonDown       = juce::Colour (0xff3a0000);
        p.text             = juce::Colour (0xffb0b0b0);
        p.textMuted        = juce::Colour (0xff7a7a7a);
        p.accent           = juce::Colour (0xff8b0000);
        p.onAccent         = juce::Colour (0xffe0e0e0);
        p.border           = juce::Colour (0xff3a0000);
        p.sliderThumb      = juce::Colour (0xff8b0000);
        p.listSelection    = p.accent.withAlpha (0.45f);
        p.timelineProgress = p.accent;
        p.logoText         = juce::Colour (0xffd0d0d0);
        p.good             = juce::Colour (0xffb0b0b0);
        return p;
    }
};
