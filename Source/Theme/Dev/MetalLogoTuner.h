#pragma once

// ロゴの生成パラメータを調整するウィンドウ (death-metal-mode.md 4.8)
// 設定画面の「デスメタルモード」から開く (作者の指定で、Debug ビルド以外でも使えるようにした)
#include <juce_gui_basics/juce_gui_basics.h>
#include "../ThemeManager.h"


class MetalLogoTunerWindow final : public juce::DocumentWindow
{
public:
    explicit MetalLogoTunerWindow (ThemeManager& theme);

    // 閉じるボタンでは隠すだけ (調整中の値を残しておくため)
    void closeButtonPressed() override { setVisible (false); }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MetalLogoTunerWindow)
};
