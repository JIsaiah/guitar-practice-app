#pragma once

#include <juce_graphics/juce_graphics.h>
#include "MetalLogoParams.h"

// 文字列から、デスメタルロゴ風の Path を作る (death-metal-mode.md 4.4)
// 画面にもスレッドにも依存しない処理。同じ text / params / seed なら必ず同じ結果を返す
class MetalLogoGenerator
{
public:
    // 返す Path は、左上が (margin, margin) になるよう移動してある。
    // 余白込みの範囲は getLogoArea() で取れる。空文字・空白だけのときは空の Path
    static juce::Path generate (const juce::String& text,
                                const juce::Font& baseFont,
                                const MetalLogoParams& params,
                                juce::uint32 seed);

    // generate() の結果を描くときの範囲 (余白込み)
    static juce::Rectangle<float> getLogoArea (const juce::Path& logo, const MetalLogoParams& params);

    // 呼び出し側が使う seed (文字列ごとに決まる)
    static juce::uint32 seedFor (const juce::String& text) { return (juce::uint32) text.hashCode(); }
};
