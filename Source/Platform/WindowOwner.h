#pragma once

#include <string>

// ウィンドウの「持ち主」を決める (Windows のみ)
// 持ち主を決めたウィンドウ (所有ウィンドウ) は、いつも持ち主より手前に出て、持ち主を最小化すると一緒に隠れる。
// プラグインの画面のウィンドウを、アプリのウィンドウの後ろに隠れないようにするために使う。
// windows.h を読むと JUCE の名前と衝突することがあるので、この関数だけを別のファイル (WindowOwner.cpp) にしている
namespace WindowOwner
{
    // window・owner は juce::Component::getWindowHandle() の値。どちらかが nullptr なら何もしない
    void setOwner (void* window, void* owner);

    // 確認用: ウィンドウが本当に表示されているか、位置、子ウィンドウ (プラグインが描画に使う窓) の数と表示の状態を文字にする
    // (画面を撮影せずに、プラグインの画面が出ているかを確かめるため)
    std::string describe (void* window);

    // 確認用: ウィンドウの閉じるボタンを押したのと同じ知らせ (WM_CLOSE) を送る
    void postCloseRequest (void* window);
}
