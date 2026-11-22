#include <juce_core/juce_core.h>
#include "../Source/Model/TempoMap.h"

// テンポの変化: 拍 ⇄ 秒
class TempoMapTests final : public juce::UnitTest
{
public:
    TempoMapTests() : juce::UnitTest ("TempoMap", "Model") {}

    void runTest() override
    {
        beginTest ("No changes: constant tempo (seconds = beats x 60 / BPM)");
        {
            TempoMap map;
            expectWithinAbsoluteError (map.secondsAt (8.0, 120.0), 4.0, 1.0e-9);
            expectWithinAbsoluteError (map.beatsAt (4.0, 120.0), 8.0, 1.0e-9);
            expectWithinAbsoluteError (map.secondsAt (-2.0, 120.0), -1.0, 1.0e-9);
        }

        beginTest ("Tempo change in the middle");
        {
            TempoMap map;
            map.changes = { { 0.0, 120.0 }, { 8.0, 60.0 } };   // 8拍まで 120、そのあと 60
            map.normalise();
            expectWithinAbsoluteError (map.secondsAt (8.0, 120.0), 4.0, 1.0e-9);
            expectWithinAbsoluteError (map.secondsAt (12.0, 120.0), 8.0, 1.0e-9);   // 4拍 × 1秒
            expectWithinAbsoluteError (map.beatsAt (8.0, 120.0), 12.0, 1.0e-9);
            expectWithinAbsoluteError (map.beatsAt (2.0, 120.0), 4.0, 1.0e-9);
            expectWithinAbsoluteError (map.bpmAt (10.0, 120.0), 60.0, 1.0e-9);

            // つまみを半分 (60) にすると、途中も半分 (30)
            expectWithinAbsoluteError (map.secondsAt (12.0, 60.0), 16.0, 1.0e-9);
            expectWithinAbsoluteError (map.bpmAt (10.0, 60.0), 30.0, 1.0e-9);

            // 行って戻る
            for (double b = 0.0; b < 30.0; b += 1.7)
                expectWithinAbsoluteError (map.beatsAt (map.secondsAt (b, 97.0), 97.0), b, 1.0e-9);
        }

        beginTest ("normalise: sorted, merged, starts at beat 0");
        {
            TempoMap map;
            map.changes = { { 4.0, 140.0 }, { 2.0, 100.0 }, { 6.0, 140.0 }, { 4.0, 150.0 } };
            map.normalise();
            expectEquals ((int) map.changes.size(), 3);
            expectEquals (map.changes[0].beat, 0.0);      // 最初は拍 0
            expectEquals (map.changes[0].bpm, 100.0);
            expectEquals (map.changes[1].bpm, 150.0);     // 同じ拍 (4) は後のもの
            expectEquals (map.changes[2].bpm, 140.0);

            TempoMap same;
            same.changes = { { 0.0, 120.0 }, { 8.0, 120.0 } };   // 同じテンポが続く → 1つにまとめる (一定)
            same.normalise();
            expect (same.isConstant());
        }
    }
};

static TempoMapTests tempoMapTests;
