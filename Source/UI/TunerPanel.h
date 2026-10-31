#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Audio/AudioEngine.h"

// チューナー。セッション画面の上に重ねて表示する
class TunerPanel final : public juce::Component,
                         private juce::Timer
{
public:
    explicit TunerPanel (AudioEngine& engine);
    ~TunerPanel() override;

    std::function<void()> onClose;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;

    static constexpr int analysisSize = 4096;

    AudioEngine& engine;
    std::vector<float> history = std::vector<float> ((size_t) analysisSize, 0.0f);
    std::vector<float> incoming = std::vector<float> ((size_t) analysisSize, 0.0f);
    double frequency = 0.0;
    int silentFrames = 0;

    juce::TextButton closeButton;
};
