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

// 再生ボタン (と Space キー) の押し方 (作者の指示)。「すぐに始める」は数字のクリックではなく、ボタンの2回押しにまとめた
//   止まっている     → 1回: カウントダウン。続けてもう1回: すぐに始める
//   カウントダウン中 → 間をあけて1回: 中止。続けてもう1回: すぐに始める (セットリストの自動のカウントダウンも同じ)
//   再生中           → 1回: 止める (ただし、待ち時間 0 秒で始めた直後の2回目は無視する)
class PlayButtonPresses
{
public:
    enum class Action { none, startCountdown, startNow, cancel, pause };

    Action press (double nowMs, bool countingDown, bool playing);

    static constexpr double quickPressMs = 500.0;   // 【仮決め】「続けて」とみなす間隔 (Windows のダブルクリックの既定に近い値)

private:
    double lastPressMs = -1.0e9;
    Action lastAction = Action::none;
};
