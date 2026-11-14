#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// アイコン (作者が `svg icons/` に置いた SVG。実行ファイルに埋め込む)
// 黒で描かれた SVG を、テーマの文字の色に塗り替えて描く
namespace Icons
{
    // `svg icons/` の中のファイル名
    inline constexpr const char* help         = "help-circle-svgrepo-com.svg";
    inline constexpr const char* back         = "back-arrow-direction-down-right-left-up-svgrepo-com.svg";
    inline constexpr const char* toStart      = "media-backward-end-svgrepo-com.svg";
    inline constexpr const char* backward     = "media-backward-svgrepo-com.svg";
    inline constexpr const char* forward      = "media-forward-svgrepo-com.svg";
    inline constexpr const char* play         = "media-play-svgrepo-com.svg";
    inline constexpr const char* pause        = "media-pause-svgrepo-com.svg";
    inline constexpr const char* plus         = "plus-svgrepo-com.svg";
    inline constexpr const char* settings     = "setting-2-svgrepo-com.svg";
    inline constexpr const char* tuner        = "diapason-tuner-svgrepo-com.svg";   // 音叉 (練習画面のチューナー)
    inline constexpr const char* save         = "save-01-svgrepo-com.svg";          // 保存 (練習画面)
    inline constexpr const char* appLogo      = "app-logo-1.svg";                   // アプリのアイコン (ウィンドウ・タスクバー)
    inline constexpr const char* trash        = "trash-svgrepo-com.svg";
    inline constexpr const char* sort         = "sort-sortable-anchor-table-order-desc-asc-svgrepo-com.svg";
    inline constexpr const char* sortAscending  = "sort-asc-sortable-anchor-table-order-desc-asc-svgrepo-com.svg";
    inline constexpr const char* sortDescending = "sort-desc-sortable-anchor-table-order-desc-asc-svgrepo-com.svg";

    // area の中に、縦横比を保って真ん中に描く。見つからないファイルなら何も描かない
    void draw (juce::Graphics& g, const char* fileName, juce::Rectangle<float> area, juce::Colour colour);

    // アプリのアイコン (作者の指示: app-logo-1.svg)。黒一色の形なので、暗いタスクバーでも見えるよう、
    // アクセントの色の角の丸い四角に、白で形を描く。size × size の画像。
    // 色を渡さなければ、ライトのテーマのアクセントの色。exe のアイコン (テーマで変えられない) は黒 (作者の指示。DevSnapshots で書き出す)
    juce::Image renderAppIcon (int size, juce::Colour accent = juce::Colour (0xffb45309));
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
    // 枠と背景を描かず、アイコンだけにする (練習画面の「戻る」)。マウスを乗せるとアクセントの色になる
    void setFrameless (bool shouldBeFrameless) { frameless = shouldBeFrameless; repaint(); }
    // 右上に小さな丸 (アクセントの色) を出す。練習画面の保存ボタンで「未保存の変更がある」ことを示す
    void setMarked (bool shouldBeMarked) { if (marked != shouldBeMarked) { marked = shouldBeMarked; repaint(); } }

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override;

private:
    const char* icon = nullptr;
    bool iconAfterText = false;
    float iconScale = 0.45f;
    bool frameless = false;
    bool marked = false;
};
