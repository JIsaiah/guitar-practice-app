#include "../Source/Audio/ScoreClock.h"

// タブ譜だけで再生するときの時計
class ScoreClockTests final : public juce::UnitTest
{
public:
    ScoreClockTests() : juce::UnitTest ("ScoreClock", "Score") {}

    void runTest() override
    {
        beginTest ("Stopped clock stays where it is; playing clock advances with time");
        {
            ScoreClock c;
            expect (! c.isPlaying());
            expectEquals (c.getPosition (5000.0), 0.0);

            c.play (1000.0);
            expect (c.isPlaying());
            expectWithinAbsoluteError (c.getPosition (3500.0), 2.5, 1.0e-9);

            c.pause (4000.0);
            expect (! c.isPlaying());
            expectWithinAbsoluteError (c.getPosition (9000.0), 3.0, 1.0e-9);   // 止めたところのまま

            c.play (10000.0);
            expectWithinAbsoluteError (c.getPosition (11000.0), 4.0, 1.0e-9);   // 止めたところから続ける
        }

        beginTest ("Seek moves the position (while playing and while stopped), never below 0");
        {
            ScoreClock c;
            c.seek (10.0, 0.0);
            expectEquals (c.getPosition (5000.0), 10.0);

            c.play (0.0);
            c.seek (20.0, 1000.0);
            expectWithinAbsoluteError (c.getPosition (2000.0), 21.0, 1.0e-9);

            c.seek (-5.0, 3000.0);
            expectWithinAbsoluteError (c.getPosition (3000.0), 0.0, 1.0e-9);
        }

        beginTest ("Speed changes continue from the current position");
        {
            ScoreClock c;
            c.play (0.0);
            c.setRate (0.5, 2000.0);   // 2秒進んだところから半分の速さ
            expectWithinAbsoluteError (c.getPosition (4000.0), 3.0, 1.0e-9);
            expectEquals (c.getRate(), 0.5);

            c.pause (4000.0);
            c.setRate (2.0, 5000.0);   // 止まっている間に変えても、位置は動かない
            expectWithinAbsoluteError (c.getPosition (6000.0), 3.0, 1.0e-9);
        }
    }
};

static ScoreClockTests scoreClockTests;
