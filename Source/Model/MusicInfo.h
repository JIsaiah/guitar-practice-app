#pragma once

#include <algorithm>
#include <vector>
#include <juce_core/juce_core.h>
#include "../Text.h"

// 曲の情報の計算 (画面に依存しない。単体テストあり: Tests/MusicInfoTests.cpp)
namespace MusicInfo
{
    //==========================================================================
    // チューニングの名前
    //   strings: 各弦の音の高さ (MIDI のノート番号。60 = 真ん中の C)。alphaTab と同じく **1弦 (高い弦) から** 並べる
    //   例: {64, 59, 55, 50, 45, 40} → "E Standard"、{64, 59, 55, 50, 45, 38} → "Drop D"
    //   どちらにも当てはまらなければ、低い弦から音名を並べる (例: "D A D G A D")
    inline juce::String noteName (int midi)
    {
        static const char* const names[] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
        return names[((midi % 12) + 12) % 12];
    }

    inline juce::String describeTuning (const std::vector<int>& strings)
    {
        if (strings.empty())
            return {};

        // 低い弦から並べ直し、隣の弦との音の差 (半音の数) を見る
        const std::vector<int> low (strings.rbegin(), strings.rend());
        std::vector<int> steps;
        for (size_t i = 1; i < low.size(); ++i)
            steps.push_back (low[i] - low[i - 1]);

        // レギュラーチューニングの音の差 (低い弦から)。ギターは2弦と3弦の間だけ 4 (長3度)、ほかは 5 (完全4度)
        // 4弦・5弦 (ベース) はすべて 5
        std::vector<int> standard (steps.size(), 5);
        if (low.size() >= 6)
            standard[standard.size() - 2] = 4;

        const auto count = (int) low.size();
        const auto suffix = count == 6 ? juce::String() : " ("_jp + juce::String (count) + "弦)"_jp;

        if (steps == standard)
            return noteName (low[0]) + " Standard" + suffix;

        // ドロップ: 一番低い弦だけ、さらに全音 (2半音) 下げたもの
        auto drop = standard;
        if (! drop.empty())
            drop[0] += 2;
        if (steps == drop)
            return "Drop " + noteName (low[0]) + suffix;

        juce::StringArray names;
        for (auto note : low)
            names.add (noteName (note));
        return names.joinIntoString (" ");
    }

    //==========================================================================
    // 拍子と、今が小節の何拍目か (メトロノームのビジュアライザー用)
    struct TimeSignature
    {
        int numerator = 4, denominator = 4;
    };

    struct BeatPosition
    {
        int bar = 0;          // 0 から
        int beatInBar = 0;    // 0 から (0 = 小節の頭)
        TimeSignature signature;
    };

    // quarterBeats: 曲の頭からの4分音符の数 (= 秒 × BPM ÷ 60)
    // barStarts:    各小節が何拍目 (4分音符の数) から始まるか。空なら 4/4 が続くとみなす
    // signatures:   各小節の拍子 (barStarts と同じ数。足りない分は 4/4)
    // 拍の単位は拍子の分母 (6/8 なら8分音符ごとに1つ進む)
    inline BeatPosition locateBeat (double quarterBeats,
                                    const std::vector<double>& barStarts,
                                    const std::vector<TimeSignature>& signatures)
    {
        quarterBeats = juce::jmax (0.0, quarterBeats);

        const auto signatureOf = [&] (size_t index)
        {
            auto sig = index < signatures.size() ? signatures[index] : TimeSignature {};
            if (sig.numerator <= 0 || sig.denominator <= 0)
                sig = {};
            return sig;
        };

        BeatPosition result;

        if (barStarts.empty())
        {
            result.bar = (int) std::floor (quarterBeats / 4.0);
            result.beatInBar = juce::jlimit (0, 3, (int) std::floor (quarterBeats - result.bar * 4.0));
            return result;
        }

        // 小節の頭がその位置以前にある、最後の小節
        const auto it = std::upper_bound (barStarts.begin(), barStarts.end(), quarterBeats + 1.0e-6);
        const auto index = (size_t) juce::jmax (0, (int) std::distance (barStarts.begin(), it) - 1);

        auto sig = signatureOf (index);
        const auto barLength = sig.numerator * 4.0 / sig.denominator;
        auto barStart = barStarts[index];
        auto bar = (int) index;

        // タブ譜の最後の小節より後は、最後の小節と同じ拍子が続くとみなす
        if (index == barStarts.size() - 1 && quarterBeats >= barStart + barLength)
        {
            const auto extra = (int) std::floor ((quarterBeats - barStart) / barLength);
            barStart += extra * barLength;
            bar += extra;
        }

        const auto unit = 4.0 / sig.denominator;   // 1拍 = 4分音符いくつ分か
        result.bar = bar;
        result.beatInBar = juce::jlimit (0, sig.numerator - 1, (int) std::floor ((quarterBeats - barStart) / unit + 1.0e-6));
        result.signature = sig;
        return result;
    }
}
