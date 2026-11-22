#pragma once

#include <vector>
#include <algorithm>
#include <cmath>

// テンポの変化 (作者の指示: .gp に書かれた、曲の途中のテンポの変化に合わせる)。
//
// 拍 (4分音符の数) と、曲の頭からの時間 (秒) を行き来する。テンポが一定なら「秒 = 拍 × 60 ÷ BPM」と同じ。
// 変化は「この拍から、このテンポ」の並び (タブ譜のページが、繰り返しを展開した演奏の順で数えて送ってくる)。
// 練習画面の BPM のつまみは「曲の最初のテンポ」として扱い、途中のテンポも同じ割合で変える
// (例: 最初 120・途中 150 のタブ譜で、つまみを 60 にすると、途中は 75)
struct TempoMap
{
    struct Change
    {
        double beat;   // この拍から
        double bpm;    // タブ譜に書かれたテンポ
    };

    std::vector<Change> changes;   // 拍の順。空なら、テンポは一定 (つまみの BPM)

    bool isConstant() const noexcept { return changes.size() < 2; }
    double firstBpm() const noexcept { return changes.empty() ? 0.0 : changes.front().bpm; }

    // 拍の順に並べ、同じテンポが続くところはまとめる。最初の変化は拍 0 にする
    void normalise()
    {
        std::stable_sort (changes.begin(), changes.end(), [] (const Change& a, const Change& b) { return a.beat < b.beat; });
        std::vector<Change> merged;
        for (const auto& c : changes)
        {
            if (! (c.bpm > 0.0))
                continue;
            if (! merged.empty() && std::abs (merged.back().beat - c.beat) < 1.0e-9)
                merged.back().bpm = c.bpm;          // 同じ拍: 後のもの
            else if (merged.empty() || std::abs (merged.back().bpm - c.bpm) > 1.0e-9)
                merged.push_back (c);
        }
        if (! merged.empty())
            merged.front().beat = 0.0;
        changes = std::move (merged);
    }

    // bpm: つまみの BPM (曲の最初のテンポ)。拍 → 秒 (曲の頭から。マイナスの拍は最初のテンポで)
    double secondsAt (double beats, double bpm) const
    {
        if (! (bpm > 0.0))
            return 0.0;
        if (isConstant() || beats <= 0.0)
            return beats * 60.0 / bpm;

        const auto scale = bpm / firstBpm();
        double seconds = 0.0;
        for (size_t i = 0; i < changes.size(); ++i)
        {
            const auto start = changes[i].beat;
            const auto end = i + 1 < changes.size() ? changes[i + 1].beat : beats;
            const auto upTo = std::min (beats, end);
            if (upTo > start)
                seconds += (upTo - start) * 60.0 / (changes[i].bpm * scale);
            if (beats <= end)
                break;
        }
        return seconds;
    }

    // 秒 → 拍 (secondsAt の逆)
    double beatsAt (double seconds, double bpm) const
    {
        if (! (bpm > 0.0))
            return 0.0;
        if (isConstant() || seconds <= 0.0)
            return seconds * bpm / 60.0;

        const auto scale = bpm / firstBpm();
        double elapsed = 0.0;
        for (size_t i = 0; i < changes.size(); ++i)
        {
            const auto rate = changes[i].bpm * scale / 60.0;   // 1秒の拍の数
            const bool last = i + 1 == changes.size();
            const auto segmentSeconds = last ? 0.0 : (changes[i + 1].beat - changes[i].beat) / rate;
            if (last || seconds < elapsed + segmentSeconds)
                return changes[i].beat + (seconds - elapsed) * rate;
            elapsed += segmentSeconds;
        }
        return 0.0;
    }

    // その拍のテンポ (つまみの BPM の割合を掛けたもの)
    double bpmAt (double beats, double bpm) const
    {
        if (isConstant() || ! (bpm > 0.0))
            return bpm;
        double tempo = changes.front().bpm;
        for (const auto& c : changes)
            if (c.beat <= beats)
                tempo = c.bpm;
        return tempo * bpm / firstBpm();
    }
};
