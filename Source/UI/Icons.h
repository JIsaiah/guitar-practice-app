#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// アイコン (作者が `svg icons/` に置いた SVG。実行ファイルに埋め込む)
// 黒で描かれた SVG を、テーマの文字の色に塗り替えて描く
namespace Icons
{
    // `svg icons/` の中のファイル名
    inline constexpr const char* help         = "help-circle-svgrepo-com.svg";
    inline constexpr const char* toStart      = "media-backward-end-svgrepo-com.svg";
    inline constexpr const char* backward     = "media-backward-svgrepo-com.svg";
    inline constexpr const char* forward      = "media-forward-svgrepo-com.svg";
    inline constexpr const char* play         = "media-play-svgrepo-com.svg";
    inline constexpr const char* pause        = "media-pause-svgrepo-com.svg";
    inline constexpr const char* plus         = "plus-svgrepo-com.svg";
    inline constexpr const char* settings     = "setting-2-svgrepo-com.svg";
    inline constexpr const char* trash        = "trash-svgrepo-com.svg";
    inline constexpr const char* sort         = "sort-sortable-anchor-table-order-desc-asc-svgrepo-com.svg";
    inline constexpr const char* sortAscending  = "sort-asc-sortable-anchor-table-order-desc-asc-svgrepo-com.svg";
    inline constexpr const char* sortDescending = "sort-desc-sortable-anchor-table-order-desc-asc-svgrepo-com.svg";

    // area の中に、縦横比を保って真ん中に描く。見つからないファイルなら何も描かない
    void draw (juce::Graphics& g, const char* fileName, juce::Rectangle<float> area, juce::Colour colour);
}

// アイコン (と文字) のボタン。背景・枠は普通のボタン (TextButton) と同じ見た目で、文字の代わりにアイコンを描く。
// ボタンの文字 (setButtonText) があれば、アイコンの横に並べる (「◀◀ 1小節」のような形)
class IconButton : public juce::TextButton
{
public:
    IconButton() = default;
    explicit IconButton (const char* iconFile) : icon (iconFile) {}

    void setIcon (const char* iconFile);               // nullptr: アイコンなし (文字だけ)
    void setIconAfterText (bool after) { iconAfterText = after; repaint(); }   // 「1小節 ▶▶」のように、アイコンを文字の右に
    void setIconScale (float scale) { iconScale = scale; repaint(); }          // ボタンの高さに対するアイコンの大きさ

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override;

private:
    const char* icon = nullptr;
    bool iconAfterText = false;
    float iconScale = 0.45f;
};
