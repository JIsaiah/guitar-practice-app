#pragma once

#include <juce_core/juce_core.h>
#include <vector>

// リード・バッキング (1つのセッションで2つのタブ譜を切り替える。作者の指示)
// タブ譜 (.gp) のギターのトラックから、リードとバッキングを決める。画面に依存しないので、単体テストで確かめる
//   ギターが 2つ → 1つ目がリード、2つ目がバッキング
//   ギターが 3つ以上 → とりあえず先頭の2つにし、作者に選んでもらう (needsChoice)
//   ギターが 1つ → リードだけ
//   ギターが無い (ギターと見分けられない) → 最初のトラックをリードに
// 最大は 2トラック
namespace ScoreParts
{
    struct Track
    {
        juce::String name;
        bool guitar = false;   // ギターのトラックか (タブ譜のページが、弦の数と楽器から判断する)
    };

    struct Assignment
    {
        int lead = -1, backing = -1;   // トラックの番号。-1 = なし
        bool needsChoice = false;      // ギターが 3つ以上あるので、選んでもらう
    };

    inline std::vector<int> guitarTracks (const std::vector<Track>& tracks)
    {
        std::vector<int> result;
        for (int i = 0; i < (int) tracks.size(); ++i)
            if (tracks[(size_t) i].guitar)
                result.push_back (i);
        return result;
    }

    inline Assignment assign (const std::vector<Track>& tracks)
    {
        Assignment a;
        const auto guitars = guitarTracks (tracks);

        if (guitars.empty())
        {
            if (! tracks.empty())
                a.lead = 0;
            return a;
        }

        a.lead = guitars[0];
        if (guitars.size() >= 2)
            a.backing = guitars[1];
        a.needsChoice = guitars.size() >= 3;
        return a;
    }

    // 選択の画面などで使うトラックの名前 (名前が無ければ「トラック 3」)
    inline juce::String displayName (const std::vector<Track>& tracks, int index)
    {
        if (! juce::isPositiveAndBelow (index, (int) tracks.size()))
            return {};

        const auto& name = tracks[(size_t) index].name;
        return juce::String (index + 1) + ". " + (name.isNotEmpty() ? name : juce::String::fromUTF8 ("トラック"));
    }
}
