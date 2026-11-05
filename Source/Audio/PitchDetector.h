#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

// YIN 法による簡易な音程検出。UI スレッドで呼ぶ
// 戻り値は周波数 (Hz)。判定できないときは 0
inline double detectPitchHz (const float* samples, int numSamples, double sampleRate)
{
    double energy = 0.0;
    for (int i = 0; i < numSamples; ++i)
        energy += (double) samples[i] * samples[i];

    if (std::sqrt (energy / numSamples) < 0.005)
        return 0.0;  // 音が小さすぎる

    // 低いほうは 38Hz まで (多弦ギター: 7弦の B1 = 61.7Hz、ドロップ A = 55Hz、8弦の F#1 = 46.2Hz、ドロップ E = 41.2Hz)。
    // 以前は 60Hz までで、7弦・8弦の低い弦が測れなかった。
    // 1周期の2倍以上のサンプルが要るので、48kHz・4096 サンプルなら 38Hz まで測れる (それより少なければ、測れる範囲が狭まる)
    const int minLag = std::max (2, (int) (sampleRate / 1000.0));  // 1000Hz まで
    const int maxLag = std::min (numSamples / 2, (int) (sampleRate / 38.0));
    const int window = numSamples - maxLag;

    if (maxLag <= minLag + 1)
        return 0.0;

    std::vector<double> normalised ((size_t) maxLag + 1, 1.0);
    double runningSum = 0.0;

    for (int lag = 1; lag <= maxLag; ++lag)
    {
        double difference = 0.0;
        for (int i = 0; i < window; ++i)
        {
            const double delta = samples[i] - samples[i + lag];
            difference += delta * delta;
        }

        runningSum += difference;
        normalised[(size_t) lag] = runningSum > 0.0 ? difference * lag / runningSum : 1.0;
    }

    int best = -1;
    for (int lag = minLag; lag <= maxLag; ++lag)
    {
        if (normalised[(size_t) lag] < 0.15)
        {
            while (lag + 1 <= maxLag && normalised[(size_t) lag + 1] < normalised[(size_t) lag])
                ++lag;

            best = lag;
            break;
        }
    }

    if (best < 0)
        return 0.0;

    double shift = 0.0;
    if (best > 1 && best < maxLag)
    {
        const auto a = normalised[(size_t) best - 1];
        const auto b = normalised[(size_t) best];
        const auto c = normalised[(size_t) best + 1];
        const auto denominator = a - 2.0 * b + c;
        if (denominator != 0.0)
            shift = 0.5 * (a - c) / denominator;
    }

    return sampleRate / (best + shift);
}
