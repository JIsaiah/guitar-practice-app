#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Audio/AudioEngine.h"
#include "../Model/Session.h"

// 音色の設定 (NAM)。練習画面の上に重ねて表示する (チューナーと同じ)
//   アンプのモデル (.nam)、キャビネットの IR (.wav)、入力の大きさ、入力・出力のメーター、
//   読み込みの状態、オーディオデバイスの情報 (ASIO か、遅延、CPU)
class ToneDetailPanel final : public juce::Component,
                              private juce::Timer
{
public:
    ToneDetailPanel (AudioEngine& engine, Session& session);

    std::function<void()> onClose;
    std::function<void()> onToneFilesChanged;    // モデルや IR を選んだ・外した (練習画面が読み込み直す)
    std::function<void()> onOpenAudioSettings;

    void paint (juce::Graphics& g) override;
    void resized() override;

    static constexpr int preferredWidth = 640, preferredHeight = 470;

private:
    void timerCallback() override;
    void chooseFile (bool isAmp);
    void clearFile (bool isAmp);
    void updateFileLabels();
    juce::String describeStatus() const;
    juce::String describeDevice() const;

    AudioEngine& engine;
    Session& session;
    std::unique_ptr<juce::FileChooser> fileChooser;

    juce::Label ampLabel, ampName, cabinetLabel, cabinetName, inputLabel, statusLabel, deviceLabel;
    juce::TextButton ampChooseButton, ampClearButton, cabinetChooseButton, cabinetClearButton;
    juce::Slider inputSlider;
    juce::TextButton audioSettingsButton, closeButton;

    // メーター (dB)。表示は、上がるときはすぐ、下がるときはゆっくり
    float inputLevelDb = -60.0f, outputLevelDb = -60.0f;
    bool inputClipped = false, outputClipped = false;
    int clipHoldFrames = 0;
    juce::Rectangle<int> inputMeterArea, outputMeterArea;
};
