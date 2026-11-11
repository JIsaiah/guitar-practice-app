#pragma once

#include <cmath>
#include <juce_core/juce_core.h>

// ノイズゲート (作者の指示: 音色の設定で、スライダーで調整する)
//
// 弾いていないときの「サー」「ジー」という小さな雑音を消す。音が境目 (しきい値) より小さくなったら音を消し、
// 大きくなったらすぐに戻す。歪みの音色は小さな雑音も大きくするので、音色 (アンプ) の前にかける。
//
// - しきい値 (dB) が offDb 以下なら何もしない (オフ)
// - 開く: 音の大きさ (ピーク) がしきい値を超えたら、すぐ (約 1ms で) 開く
// - 閉じる: しきい値の半分 (-6dB) より小さくなってから、50ms 待って、ゆっくり (約 80ms で) 閉じる。
//   開く境目と閉じる境目をずらし (ヒステリシス)、待ってから閉じるのは、音の消え際で開いたり閉じたりを
//   くり返して「ブツブツ」いわないようにするため
// - オーディオスレッドで使う。メモリ確保もロックもしない
class NoiseGate
{
public:
    static constexpr float offDb = -90.0f;

    // samples をその場で書き換える
    void process (float* samples, int numSamples, float thresholdDb, double sampleRate) noexcept
    {
        if (thresholdDb <= offDb || sampleRate <= 0.0)
        {
            // オフ: 次にオンにしたとき、開いた状態から始める (いきなり音が消えないように)
            gain = 1.0f;
            open = true;
            return;
        }

        if (sampleRate != preparedRate)
            prepare (sampleRate);

        const auto openLevel = std::pow (10.0f, thresholdDb / 20.0f);   // dB を大きさ (倍率) に
        const auto closeLevel = openLevel * 0.5f;

        for (int i = 0; i < numSamples; ++i)
        {
            const auto level = std::abs (samples[i]);
            envelope = level > envelope ? level : envelope * envelopeRelease;

            if (envelope >= openLevel)
            {
                open = true;
                holdLeft = holdSamples;
            }
            else if (envelope < closeLevel)
            {
                if (holdLeft > 0)
                    --holdLeft;
                else
                    open = false;
            }

            const auto target = open ? 1.0f : 0.0f;
            gain += (target - gain) * (target > gain ? attackCoef : releaseCoef);
            samples[i] *= gain;
        }
    }

    bool isOpen() const noexcept { return open; }

private:
    void prepare (double sampleRate)
    {
        preparedRate = sampleRate;
        const auto coefFor = [sampleRate] (double seconds) { return (float) (1.0 - std::exp (-1.0 / (seconds * sampleRate))); };
        attackCoef = coefFor (0.001);
        releaseCoef = coefFor (0.080);
        envelopeRelease = (float) std::exp (-1.0 / (0.020 * sampleRate));   // 音の大きさの読み取り: 下がるときは 20ms で
        holdSamples = (int) (0.050 * sampleRate);
    }

    double preparedRate = 0.0;
    float attackCoef = 1.0f, releaseCoef = 1.0f, envelopeRelease = 0.0f;
    int holdSamples = 0;

    float envelope = 0.0f, gain = 1.0f;
    int holdLeft = 0;
    bool open = true;
};
