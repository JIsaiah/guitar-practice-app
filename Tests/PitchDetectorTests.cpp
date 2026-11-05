#include <juce_core/juce_core.h>
#include "../Source/Audio/PitchDetector.h"
#include "../Source/Text.h"

// チューナーの音程検出。多弦ギターの低い弦も測れること
class PitchDetectorTests final : public juce::UnitTest
{
public:
    PitchDetectorTests() : juce::UnitTest ("PitchDetector", "Tuner") {}

    void runTest() override
    {
        beginTest ("ギターの低い弦 (6弦〜8弦、ドロップ) と高い弦"_jp);

        // E2 (6弦)、B1 (7弦)、A1 (7弦のドロップ A)、F#1 (8弦)、E1 (8弦のドロップ E)、E4 (1弦)
        for (const auto hz : { 82.41, 61.74, 55.0, 46.25, 41.2, 329.63 })
        {
            const auto detected = detectPitchHz (makeString (hz).data(), size, sampleRate);
            expectWithinAbsoluteError (detected, hz, hz * 0.005,
                                       "expected " + juce::String (hz, 2) + " Hz, got " + juce::String (detected, 2));
        }
    }

private:
    static constexpr int size = 4096;   // チューナーと同じ (TunerPanel::analysisSize)
    static constexpr double sampleRate = 48000.0;

    // 弦の音の代わり: 基音と倍音 (倍音のほうが大きいこともある。低い弦はそうなりやすい)
    static std::vector<float> makeString (double hz)
    {
        std::vector<float> samples ((size_t) size);
        for (int i = 0; i < size; ++i)
        {
            const auto phase = juce::MathConstants<double>::twoPi * hz * i / sampleRate;
            samples[(size_t) i] = (float) (0.25 * std::sin (phase) + 0.35 * std::sin (2.0 * phase)
                                           + 0.15 * std::sin (3.0 * phase) + 0.08 * std::sin (4.0 * phase));
        }
        return samples;
    }
};

static PitchDetectorTests pitchDetectorTests;
