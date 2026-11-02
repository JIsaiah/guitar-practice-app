#pragma once

#include <juce_core/juce_core.h>

// 再生開始前のカウントダウン (setlist-mode.md 7.1、7.2)
//   Stopped → CountingDown → (終わる・飛ばす) → onFinished で再生を始める
//                         └→ (中止) → Stopped
// 画面に依存しないよう、時刻は呼ぶ側から渡す (画面ではメッセージスレッドのタイマーから update() を呼ぶ)。
// 秒単位で足りるので、サンプル単位の精度は持たない。音源とタブ譜のずらし (オフセット) とは関係しない
class CountdownController
{
public:
    enum class State { stopped, countingDown };

    // seconds が 0 以下なら、すぐに onFinished を呼ぶ
    void start (double seconds, double nowMs);
    void update (double nowMs);   // 時間が来ていれば onFinished を呼ぶ
    void skip();                  // 待たずにすぐ始める
    void cancel();                // 始めずに止める

    State getState() const noexcept { return state; }
    bool isCountingDown() const noexcept { return state == State::countingDown; }

    // 画面に出す数字 (残り 2.3 秒なら 3)。数えていなければ 0
    int getDisplayNumber (double nowMs) const;

    std::function<void()> onFinished;

private:
    void finish();

    State state = State::stopped;
    double endMs = 0.0;
};
