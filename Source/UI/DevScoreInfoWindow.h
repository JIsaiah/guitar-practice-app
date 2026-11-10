#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Text.h"

// 開発用 (Debug ビルドのみ。作者の指示で、後で消す):
// 練習画面を開くと、別の小さいウィンドウに、今流れているタブ譜の小節の番号と拍子を出す
// (タブ譜のカーソル・メトロノームの拍の数え方を確かめるため)
class DevScoreInfoWindow final : public juce::DocumentWindow
{
public:
    DevScoreInfoWindow()
        : DocumentWindow ("タブ譜の小節・拍子 (開発用)"_jp, juce::Colours::black, DocumentWindow::closeButton)
    {
        setUsingNativeTitleBar (true);
        label.setFont (juce::FontOptions ("Segoe UI", 20.0f, juce::Font::bold));
        label.setColour (juce::Label::textColourId, juce::Colours::white);
        label.setJustificationType (juce::Justification::centredLeft);
        label.setBorderSize ({ 8, 14, 8, 14 });
        label.setSize (300, 120);
        setContentNonOwned (&label, true);
        setAlwaysOnTop (true);
        setResizable (false, false);
    }

    void setInfo (const juce::String& text)
    {
        if (label.getText() != text)
            label.setText (text, juce::dontSendNotification);
    }

    // 閉じるボタン: 隠すだけ (練習画面を開き直すと、また出る)
    void closeButtonPressed() override { setVisible (false); }

private:
    juce::Label label;
};
