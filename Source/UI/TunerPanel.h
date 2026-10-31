#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Audio/AudioEngine.h"

// チューナー。セッション画面の上に重ねて表示する
// 見た目は、最近のチューナーアプリでよくある「左右のゲージ」: 中央が合っている位置で、
// 低ければ左、高ければ右に目盛りが点く。音名を大きく出す
// 合っているかは、色だけでなく、目盛りの位置・中央の枠・「OK」の文字でも示す (設計書 v0.7 4.7)
class TunerPanel final : public juce::Component,
                         private juce::Timer
{
public:
    explicit TunerPanel (AudioEngine& engine);
    ~TunerPanel() override;

    std::function<void()> onClose;

    void paint (juce::Graphics& g) override;
    void resized() override;

    static constexpr int preferredWidth = 480, preferredHeight = 300;

   #if JUCE_DEBUG
    // 開発用 (DevSnapshots): ギターを鳴らさずに、音が入ったときの表示を確かめる
    void setTestFrequency (double hz);
   #endif

private:
    void timerCallback() override;
    void analyse();

    static constexpr int analysisSize = 4096;

    AudioEngine& engine;
    std::vector<float> history = std::vector<float> ((size_t) analysisSize, 0.0f);
    std::vector<float> incoming = std::vector<float> ((size_t) analysisSize, 0.0f);
    double frequency = 0.0;
    int silentFrames = 0, analyseCountdown = 0;

    // 表示する位置は、目標の値へ少しずつ近づける (数字がちらつかないように)
    double targetCents = 0.0, shownCents = 0.0;

    juce::TextButton closeButton;
};
