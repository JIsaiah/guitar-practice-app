#pragma once

#include <juce_core/juce_core.h>

// ローカルの音源 (音楽ファイル) を、メモリに全部読み込んでよいか
//
// 長さは「サンプル数 ÷ サンプルレート」で数える。ファイルに書かれたサンプルレートをそのまま信じると、
// 作り変えたファイル (サンプルレートを何十億にしたもの) で「30 分以内」の確かめを通り抜け、
// 何 GB ものメモリを取ろうとして、アプリが落ちる。サンプルレートにも上限を付ける
namespace SourceLimits
{
    inline constexpr double maxSeconds = 30.0 * 60.0;
    inline constexpr double maxSampleRate = 384000.0;   // ふつうは 44.1k〜96k。ハイレゾでも 384k まで

    inline bool isLoadable (juce::int64 lengthInSamples, double sampleRate)
    {
        if (! (sampleRate >= 1000.0 && sampleRate <= maxSampleRate))   // NaN もここで断る
            return false;
        if (lengthInSamples <= 0 || lengthInSamples > (juce::int64) std::numeric_limits<int>::max())
            return false;
        return (double) lengthInSamples <= maxSeconds * sampleRate;
    }
}
