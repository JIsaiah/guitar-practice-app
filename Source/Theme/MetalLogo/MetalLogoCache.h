#pragma once

#include <juce_graphics/juce_graphics.h>
#include "MetalLogoParams.h"

// 生成したロゴを Image にして取っておく (death-metal-mode.md 4.5)
// メッセージスレッドからだけ使う。paint() の中では呼ばない (画像を作るのは resized() とモード切替のとき)
class MetalLogoCache
{
public:
    explicit MetalLogoCache (juce::Typeface::Ptr typeface);

    // 文字色。変えると作った画像は捨てる
    void setColour (juce::Colour newColour);

    // 全部捨てる (モード切替時)
    void clear();

    // width × height (論理ピクセル) の枠に、縦横比を保って中央に収めたロゴの画像。
    // 高 DPI 用に scale 倍の解像度で作る。枠が小さすぎるときは無効な Image
    juce::Image getImage (const juce::String& text, int width, int height, float scale);

    const MetalLogoParams& getParams() const noexcept { return params; }

    // パラメータを差し替える (ロゴ調整ウィンドウの「保存して反映」)。作った形と画像は捨てる
    void setParams (const MetalLogoParams& newParams) { params = newParams; clear(); }

private:
    const juce::Path& getPath (const juce::String& text);

    juce::Typeface::Ptr typeface;
    MetalLogoParams params;
    juce::Colour colour { juce::Colours::white };

    // Path は大きさに関係ないので文字列ごと、Image は大きさごとに持つ
    std::map<juce::String, juce::Path> paths;
    std::map<juce::String, juce::Image> images;
};
