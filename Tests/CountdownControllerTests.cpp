#include "../Source/Setlist/CountdownController.h"

// setlist-mode.md 10章のテスト 8 (フェーズ3)
class CountdownControllerTests final : public juce::UnitTest
{
public:
    CountdownControllerTests() : juce::UnitTest ("CountdownController", "Setlist") {}

    void runTest() override
    {
        beginTest ("8. Zero seconds starts immediately");
        {
            CountdownController c;
            int finished = 0;
            c.onFinished = [&] { ++finished; };

            c.start (0.0, 1000.0);
            expectEquals (finished, 1);
            expect (c.getState() == CountdownController::State::stopped);
        }

        beginTest ("8. Counts down and then starts");
        {
            CountdownController c;
            int finished = 0;
            c.onFinished = [&] { ++finished; };

            c.start (3.0, 1000.0);
            expect (c.isCountingDown());
            expectEquals (c.getDisplayNumber (1000.0), 3);
            expectEquals (c.getDisplayNumber (2500.0), 2);
            expectEquals (c.getDisplayNumber (3999.0), 1);

            c.update (3999.0);
            expectEquals (finished, 0);
            c.update (4000.0);
            expectEquals (finished, 1);
            expect (! c.isCountingDown());
            expectEquals (c.getDisplayNumber (4000.0), 0);

            c.update (5000.0);   // 2回は始めない
            expectEquals (finished, 1);
        }

        beginTest ("8. Skip starts now, cancel never starts");
        {
            CountdownController c;
            int finished = 0;
            c.onFinished = [&] { ++finished; };

            c.start (3.0, 0.0);
            c.skip();
            expectEquals (finished, 1);
            expect (! c.isCountingDown());
            c.skip();   // 数えていなければ何もしない
            expectEquals (finished, 1);

            c.start (3.0, 0.0);
            c.cancel();
            expect (c.getState() == CountdownController::State::stopped);
            c.update (10000.0);
            c.skip();
            expectEquals (finished, 1);
        }

        beginTest ("Play button: press twice to start now, a late second press cancels");
        {
            using A = PlayButtonPresses::Action;

            // 止まっている → 1回目でカウントダウン、続けて2回目ですぐに始める
            {
                PlayButtonPresses p;
                expect (p.press (1000.0, false, false) == A::startCountdown);
                expect (p.press (1300.0, true, false) == A::startNow);
            }

            // カウントダウン中に間をあけて押すと中止
            {
                PlayButtonPresses p;
                expect (p.press (1000.0, false, false) == A::startCountdown);
                expect (p.press (2500.0, true, false) == A::cancel);
                expect (p.press (5000.0, false, false) == A::startCountdown);   // 間をあければ、またカウントダウン
            }

            // 自動のカウントダウン中に2回押す → 中止のあと、すぐに始める
            {
                PlayButtonPresses p;
                expect (p.press (10000.0, true, false) == A::cancel);
                expect (p.press (10300.0, false, false) == A::startNow);
            }

            // 再生中は止める。待ち時間 0 秒で始めた直後の2回目は無視する
            {
                PlayButtonPresses p;
                expect (p.press (1000.0, false, true) == A::pause);

                PlayButtonPresses zero;
                expect (zero.press (1000.0, false, false) == A::startCountdown);
                expect (zero.press (1200.0, false, true) == A::none);
                expect (zero.press (3000.0, false, true) == A::pause);
            }
        }

        beginTest ("8. Can restart from onFinished (next song)");
        {
            CountdownController c;
            int finished = 0;
            c.onFinished = [&]
            {
                if (++finished == 1)
                    c.start (2.0, 1000.0);
            };

            c.start (1.0, 0.0);
            c.update (1000.0);
            expectEquals (finished, 1);
            expect (c.isCountingDown());
            c.update (3000.0);
            expectEquals (finished, 2);
        }
    }
};

static CountdownControllerTests countdownControllerTests;
