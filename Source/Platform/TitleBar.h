#pragma once

#include <cstdint>

// ウィンドウの題名の帯 (Windows が描く、いちばん上の「suguhiku  ─ □ ×」の帯) の色を、アプリの色に合わせる (Windows のみ)
//   Windows 11: 帯の色・題名の文字の色・枠の色を、指定した色にする
//   Windows 10: 色は指定できないので、暗いテーマのときだけ、帯を暗い色 (ダークモード) にする
// Windows が描く帯をそのまま使うので、ドラッグで動かす・画面の端に寄せる・最大化のメニューは、ふだんどおり使える。
// windows.h を読むと JUCE の名前と衝突することがあるので、別のファイル (TitleBar.cpp) にしている
namespace TitleBar
{
    // window は juce::Component::getWindowHandle() の値 (nullptr なら何もしない)。色は 0xAARRGGBB (juce::Colour::getARGB())
    void setColours (void* window, std::uint32_t captionArgb, std::uint32_t textArgb, bool dark);

    // 帯にアプリのアイコンと題名 (suguhiku) を描かない (作者の指示)。タスクバーには、今までどおり名前とアイコンが出る
    void hideIconAndTitle (void* window);
}
