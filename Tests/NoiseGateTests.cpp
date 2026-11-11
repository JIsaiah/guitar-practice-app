#include <juce_core/juce_core.h>
#include "../Source/Audio/NoiseGate.h"

// ノイズゲート (Audio/NoiseGate.h): 小さい音を消し、大きい音はそのまま通す
class NoiseGateTests final : public juce::UnitTest
{
public:
    NoiseGateTests() : juce::UnitTest ("NoiseGate", "Audio") {}

    void runTest() override
    {
        constexpr double rate = 48000.0;

        // 大きさ amplitude の正弦波 (440Hz) を seconds 秒ぶん作る
        const auto sine = [] (float amplitude, double seconds)
        {
            std::vector<float> s ((size_t) (seconds * rate));
            for (size_t i = 0; i < s.size(); ++i)
                s[i] = amplitude * (float) std::sin (juce::MathConstants<double>::twoPi * 440.0 * (double) i / rate);
            return s;
        };
        const auto peakOf = [] (const std::vector<float>& s, size_t from, size_t to)
        {
            float peak = 0.0f;
            for (auto i = from; i < juce::jmin (to, s.size()); ++i)
                peak = juce::jmax (peak, std::abs (s[i]));
            return peak;
        };
        const auto db = [] (float d) { return std::pow (10.0f, d / 20.0f); };

        beginTest ("Off: the sound is not changed");
        {
            NoiseGate gate;
            auto s = sine (db (-70.0f), 0.2);
            const auto original = s;
            gate.process (s.data(), (int) s.size(), NoiseGate::offDb, rate);
            expect (s == original);
        }

        beginTest ("Quiet noise below the threshold is removed");
        {
            NoiseGate gate;
            auto s = sine (db (-70.0f), 0.5);
            gate.process (s.data(), (int) s.size(), -50.0f, rate);
            // 待ち (50ms) と閉じる時間 (80ms) のあとは、ほとんど音が無い (-100dB 以下)
            expectLessThan (peakOf (s, (size_t) (0.4 * rate), s.size()), db (-100.0f));
        }

        beginTest ("Playing above the threshold passes through");
        {
            NoiseGate gate;
            auto s = sine (db (-20.0f), 0.3);
            gate.process (s.data(), (int) s.size(), -50.0f, rate);
            expectWithinAbsoluteError (peakOf (s, (size_t) (0.1 * rate), s.size()), db (-20.0f), db (-20.0f) * 0.01f);
        }

        beginTest ("Opens quickly when the guitar is played after silence");
        {
            NoiseGate gate;
            auto silence = sine (db (-80.0f), 0.3);
            gate.process (silence.data(), (int) silence.size(), -50.0f, rate);
            expect (! gate.isOpen());

            auto note = sine (db (-20.0f), 0.05);
            gate.process (note.data(), (int) note.size(), -50.0f, rate);
            expect (gate.isOpen());
            // 弾き始めから 10ms 後には、ほぼ元の大きさ
            expectGreaterThan (peakOf (note, (size_t) (0.010 * rate), note.size()), db (-20.0f) * 0.95f);
        }
    }
};

static NoiseGateTests noiseGateTests;
