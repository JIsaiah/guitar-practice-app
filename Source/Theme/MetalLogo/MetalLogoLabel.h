#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../ThemeManager.h"
#include "FixedTexts.h"

// 部品の中の1か所にロゴの画像を描くための道具。MetalLogoLabel と FixedTextButton が使う
// - デスメタルモードのときだけ、MetalLogoCache から画像をもらって持つ
// - 画像をもらうのは refresh() のとき (持ち主の resized() とモード切替) だけ。paint() では draw() で描くだけ
class MetalLogoSlot final : private juce::ChangeListener
{
public:
    // area: 持ち主の中の、ロゴを描く範囲を返す関数
    MetalLogoSlot (ThemeManager& theme, juce::Component& owner, std::function<juce::Rectangle<int>()> area);
    ~MetalLogoSlot() override;

    void setText (const juce::String& logoText);
    void refresh();

    bool hasImage() const noexcept { return image.isValid(); }
    void draw (juce::Graphics& g) const;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    ThemeManager& theme;
    juce::Component& owner;
    std::function<juce::Rectangle<int>()> area;
    juce::String text;
    juce::Image image;
};

// 決まった文言を表示する部品 (death-metal-mode.md 4.6)
// 通常モードでは普通の文字、デスメタルモードではロゴの画像を描く
class MetalLogoLabel final : public juce::Component,
                             public juce::SettableTooltipClient
{
public:
    MetalLogoLabel (ThemeManager& theme, FixedTextId id);

    // 通常モードで使う文字の大きさと並べ方
    void setFont (juce::FontOptions newFont);
    void setJustification (juce::Justification newJustification);

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    juce::String text;
    juce::FontOptions font { 15.0f };
    juce::Justification justification = juce::Justification::centredLeft;
    MetalLogoSlot logo;
};
