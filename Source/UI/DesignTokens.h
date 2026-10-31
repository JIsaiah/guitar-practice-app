#pragma once

// このファイルは design/generate_tokens.py が design/tokens.json から作ります。直接編集しないでください
// 色・文字・余白などのデザイントークン (設計書 v0.7 4.7〜4.9)。画面の数値は、ここからだけ取る

#include <juce_graphics/juce_graphics.h>

namespace DesignTokens
{
    // テーマごとに変わる値。テーマは「この組」を1つ足せば増やせる
    struct Theme
    {
        const char* name;
        juce::Colour background;
        juce::Colour surface;
        juce::Colour text;
        juce::Colour textMuted;
        juce::Colour accent;
        juce::Colour onAccent;
        juce::Colour border;
        juce::Colour shadowColour;
        int shadowOffsetY, shadowBlur;
    };

    inline const Theme light { "light",
        juce::Colour (0xffeef2f6),  // background
        juce::Colour (0xfff8fafc),  // surface
        juce::Colour (0xff1e2630),  // text
        juce::Colour (0xff5a6573),  // textMuted
        juce::Colour (0xffb45309),  // accent
        juce::Colour (0xfff8fafc),  // onAccent
        juce::Colour (0xffd3dae2),  // border
        juce::Colour (0x241e2630),
        2,
        8 };

    inline const Theme dark { "dark",
        juce::Colour (0xff121315),  // background
        juce::Colour (0xff1b1c1f),  // surface
        juce::Colour (0xffe2e4e7),  // text
        juce::Colour (0xff9a9fa6),  // textMuted
        juce::Colour (0xfff0a43a),  // accent
        juce::Colour (0xff121315),  // onAccent
        juce::Colour (0xff2b2d31),  // border
        juce::Colour (0x800a0b0c),
        2,
        10 };

    inline const Theme* const allThemes[] { &light, &dark };

    // 名前からテーマを探す。見つからなければ最初のテーマ
    inline const Theme& findTheme (const juce::String& name)
    {
        for (auto* theme : allThemes)
            if (name == theme->name)
                return *theme;
        return *allThemes[0];
    }

    namespace Font
    {
        inline constexpr const char* ui = "Yu Gothic UI";
        inline constexpr const char* numeric = "Segoe UI";  // 等幅の数字で使う
    }

    namespace FontSize
    {
        inline constexpr int caption = 12;
        inline constexpr int body = 15;
        inline constexpr int title = 20;
        inline constexpr int heading = 28;
        inline constexpr int display = 56;
    }

    namespace Space
    {
        inline constexpr int xs = 4;
        inline constexpr int sm = 8;
        inline constexpr int md = 12;
        inline constexpr int lg = 16;
        inline constexpr int xl = 24;
        inline constexpr int xxl = 32;
    }

    namespace Radius
    {
        inline constexpr int sm = 6;
        inline constexpr int md = 10;
        inline constexpr int lg = 14;
    }

    namespace Size
    {
        inline constexpr int buttonHeight = 40;
        inline constexpr int largeButtonHeight = 56;
        inline constexpr int cardMinWidth = 260;
    }

    namespace Motion
    {
        inline constexpr int durationMs = 180;
        inline constexpr int staggerMs = 50;
        inline constexpr int distancePx = 8;
        inline constexpr int maxTotalMs = 300;
    }
}
