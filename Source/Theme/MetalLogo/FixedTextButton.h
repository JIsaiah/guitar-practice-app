#pragma once

#include "MetalLogoLabel.h"

// 決まった文言のボタン。デスメタルモードでは文字の代わりにロゴを描く
// 位置・大きさ・onClick は普通の TextButton と同じ (ヒットボックスは変えない)
class FixedTextButton final : public juce::TextButton
{
public:
    explicit FixedTextButton (ThemeManager& theme);

    // id: デスメタルモードで描くロゴ、normalText: 通常モードで描く文字 (記号付きなど)
    void setFixedText (FixedTextId id, const juce::String& normalText);

    // デスメタルモードで、右上に小さな印を付ける (保存ボタンの「未保存」の * の代わり)
    void setMarked (bool shouldBeMarked);

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override;
    void resized() override;

private:
    juce::Rectangle<int> getLogoArea() const;

    MetalLogoSlot logo;
    bool marked = false;
};

// 決まった文言のチェックボックス (「デスメタルモード」の切り替え用)
class FixedTextToggle final : public juce::ToggleButton
{
public:
    FixedTextToggle (ThemeManager& theme, FixedTextId id);

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override;
    void resized() override;

private:
    float getTickSize() const;
    juce::Rectangle<int> getLogoArea() const;

    MetalLogoSlot logo;
};
