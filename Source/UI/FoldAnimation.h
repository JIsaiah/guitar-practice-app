#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// たたむ・広げるアニメーション (作者の指示: セットリストのカード、練習画面の音源のカードを上へたたむ)。
// 開き具合 (0 = たたんだ、1 = 開いた) を、画面の書き換えの合図 (VBlank) ごとに、押してからの経過時間で決める
// (一覧画面のアニメーションと同じ方法。画面が 60 Hz でも 144 Hz でも同じ速さになる)。
// 使い方: setTarget で目標を決め、onChange で並べ直す (getValue が今の開き具合)
class FoldAnimation
{
public:
    explicit FoldAnimation (juce::Component& owner, double durationMs = 200.0) : component (owner), duration (durationMs) {}

    std::function<void()> onChange;   // 開き具合が変わった (並べ直す)
    std::function<void()> onFinished; // 目標に着いた

    // animate が false なら、すぐに目標の値にする (画面を開いたとき)
    void setTarget (bool open, bool animate = true)
    {
        const auto target = open ? 1.0 : 0.0;
        if (! animate || ! component.isShowing())
        {
            stop();
            value = from = to = target;
            if (onChange != nullptr) onChange();
            return;
        }

        if (juce::approximatelyEqual (to, target) && (vblank != nullptr || juce::approximatelyEqual (value, target)))
            return;   // もうその向きに動いている・もう着いている

        from = value;
        to = target;
        startMs = juce::Time::getMillisecondCounterHiRes();
        finished = false;
        if (vblank == nullptr)
            vblank = std::make_unique<juce::VBlankAttachment> (&component, [this] (double) { step(); });
    }

    // 今の開き具合 (0〜1。動きの始めと終わりがゆっくりになるように曲げたもの)
    double getValue() const noexcept { return value; }
    bool isAnimating() const noexcept { return vblank != nullptr && ! finished; }

private:
    void step()
    {
        if (finished)
            return;   // 着いたあと、外すまでの間
        const auto t = juce::jlimit (0.0, 1.0, (juce::Time::getMillisecondCounterHiRes() - startMs) / duration);
        const auto eased = t * t * (3.0 - 2.0 * t);
        value = from + (to - from) * eased;

        const bool done = t >= 1.0;
        if (done)
        {
            value = to;
            finished = true;
            // VBlankAttachment の呼び出しの中で自分を消さないよう、後で外す (その間に次の動きが始まっていたら外さない)
            const auto finishedAt = startMs;
            juce::MessageManager::callAsync ([this, finishedAt, alive = std::weak_ptr<bool> (aliveFlag)]
            {
                if (! alive.expired() && juce::approximatelyEqual (startMs, finishedAt))
                    vblank = nullptr;
            });
        }

        if (onChange != nullptr) onChange();
        if (done && onFinished != nullptr) onFinished();
    }

    void stop() { vblank = nullptr; }

    juce::Component& component;
    double duration;
    double value = 1.0, from = 1.0, to = 1.0, startMs = 0.0;
    bool finished = false;
    std::unique_ptr<juce::VBlankAttachment> vblank;
    std::shared_ptr<bool> aliveFlag = std::make_shared<bool> (true);
};
