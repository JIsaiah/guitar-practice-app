#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "Palette.h"

// アプリ共通の描き方 (通常モードとデスメタルモードの土台)
// - ボタン・スライダー・コンボボックスを、平らで角の丸い形に揃える (影は使わない。設計書 v0.7 4.9 の制約)
// - 文字の大きさを、設定の「文字の大きさ」(textScale) 倍にする
class AppLookAndFeel : public juce::LookAndFeel_V4
{
public:
    // 文字の大きさの倍率 (設定画面で変える)。すべての LookAndFeel で共通
    static inline float textScale = 1.0f;
    static juce::FontOptions scaled (juce::FontOptions font) { return font.withHeight (font.getHeight() * textScale); }

    // 一覧の選んでいる行 (・マウスを乗せた行) の塗り。ボタンと同じ角の丸みにする (四角だと浮いて見える。作者の指示)
    static void fillListHighlight (juce::Graphics& g, juce::Rectangle<int> row, juce::Colour colour)
    {
        g.setColour (colour);
        g.fillRoundedRectangle (row.toFloat().reduced (4.0f, 1.5f), 6.0f);
    }

    // Font が覚えている書体を捨てた写し (テーマを切り替えたあと、新しい書体で描かせるため)
    static juce::Font withoutCachedTypeface (const juce::Font& font);

    void applyPalette (const Palette& palette);

    // 文字
    juce::Font getLabelFont (juce::Label&) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
    juce::Font getAlertWindowMessageFont() override;
    juce::Font getAlertWindowTitleFont() override;
    juce::Font getAlertWindowFont() override;

    // 形
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool highlighted, bool down) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float minSliderPos, float maxSliderPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    int getSliderThumbRadius (juce::Slider&) override { return 8; }
    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;
    void fillTextEditorBackground (juce::Graphics&, int width, int height, juce::TextEditor&) override;
    void drawTextEditorOutline (juce::Graphics&, int width, int height, juce::TextEditor&) override;

    // ポップアップ (確認・お知らせ): アイコン (「？」「！」) を描かず、角の丸い面と細い枠だけにする (作者の指示)
    void drawAlertBox (juce::Graphics&, juce::AlertWindow&, const juce::Rectangle<int>& textArea, juce::TextLayout&) override;

    // スクロールバー: ふだんは細く、マウスを乗せる・つまむと太くなる (作者の指示: 引っ張りやすく)。
    // 太さの分の場所はいつも取っておき (ずれないように)、描く幅だけを変える
    int getDefaultScrollbarWidth() override { return 14; }
    void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int width, int height, bool vertical,
                        int thumbStart, int thumbSize, bool isMouseOver, bool isMouseDown) override;
};

// 通常モードの LookAndFeel。デザイントークンのテーマ (ライト／ダーク) の色で描く。文字は「Yu Gothic UI」
class NormalLookAndFeel final : public AppLookAndFeel
{
public:
    NormalLookAndFeel();
};

// デスメタルモードの LookAndFeel (death-metal-mode.md 4.2)
// 配色を差し替え、通常フォントで描く文字 (曲名などユーザーが入力した文字も含む) をすべて Dela Gothic One にする
class DeathMetalLookAndFeel final : public AppLookAndFeel
{
public:
    explicit DeathMetalLookAndFeel (juce::Typeface::Ptr delaGothicOne);

    juce::Typeface::Ptr getTypefaceForFont (const juce::Font& font) override;

private:
    juce::Typeface::Ptr typeface;
};
