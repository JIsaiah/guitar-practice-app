#pragma once

#include <juce_core/juce_core.h>

// タブ譜だけで再生するときの時計 (音源が無いとき。作者の指示)
// 音源の代わりに、この時計の位置を再生位置にする。タブ譜の針・タブ譜の音 (MIDI)・メトロノームは、この位置に付いてくる。
// 時刻 (ミリ秒) は呼ぶ側から渡す (画面に依存しないので、単体テストで確かめる)
class ScoreClock
{
public:
    double getPosition (double nowMs) const noexcept
    {
        return playing ? startSeconds + (nowMs - startedAtMs) / 1000.0 * rate : startSeconds;
    }

    bool isPlaying() const noexcept { return playing; }
    double getRate() const noexcept { return rate; }

    void play (double nowMs) noexcept
    {
        if (playing)
            return;
        startedAtMs = nowMs;
        playing = true;
    }

    void pause (double nowMs) noexcept
    {
        startSeconds = getPosition (nowMs);
        playing = false;
    }

    void seek (double seconds, double nowMs) noexcept
    {
        startSeconds = juce::jmax (0.0, seconds);
        startedAtMs = nowMs;
    }

    // 速度 (1.0 = 原速)。今の位置から、新しい速さで進める
    void setRate (double newRate, double nowMs) noexcept
    {
        startSeconds = getPosition (nowMs);
        startedAtMs = nowMs;
        rate = juce::jmax (0.01, newRate);
    }

private:
    bool playing = false;
    double startSeconds = 0.0, startedAtMs = 0.0, rate = 1.0;
};
