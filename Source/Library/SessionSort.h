#pragma once

#include "../Model/SessionStore.h"

// 一覧の並び替え (列の見出しの ↓↑ をクリック)。画面に依存しないので、単体テストで確かめる
namespace SessionSort
{
    enum class Key { title, artist, difficulty, tuning, length, lastPracticed };

    // 最後の練習日: 開いた日時 (last_opened) が無い古いセッションは、ファイルを保存した日時で代える
    inline juce::Time lastPracticed (const SessionStore::Entry& e)
    {
        return e.lastOpened.toMilliseconds() > 0 ? e.lastOpened : e.modified;
    }

    // その列を初めて選んだときの向き。文字の列は A→Z (昇順)、数・日付の列は大きい順 (降順)
    inline bool defaultAscending (Key key)
    {
        return key == Key::title || key == Key::artist || key == Key::tuning;
    }

    inline juce::String toString (Key key)
    {
        switch (key)
        {
            case Key::title:         return "title";
            case Key::artist:        return "artist";
            case Key::difficulty:    return "difficulty";
            case Key::tuning:        return "tuning";
            case Key::length:        return "length";
            case Key::lastPracticed: break;
        }
        return "lastPracticed";
    }

    inline Key fromString (const juce::String& text)
    {
        for (auto key : { Key::title, Key::artist, Key::difficulty, Key::tuning, Key::length })
            if (text == toString (key))
                return key;
        return Key::lastPracticed;   // 既定は「最後に開いた順」(作者の指示)
    }

    // 並べる。値が無いもの (アーティストが空、長さが不明、難易度が 0) は、向きにかかわらず最後に置く。
    // 同じ値どうしは、最後の練習日の新しい順
    inline void sort (std::vector<SessionStore::Entry>& entries, Key key, bool ascending)
    {
        const auto compareText = [] (const juce::String& a, const juce::String& b) { return a.compareNatural (b, false); };

        std::stable_sort (entries.begin(), entries.end(), [&] (const SessionStore::Entry& a, const SessionStore::Entry& b)
        {
            int order = 0;   // a が先なら負
            bool aMissing = false, bMissing = false;

            switch (key)
            {
                case Key::title:      order = compareText (a.title, b.title); aMissing = a.title.isEmpty(); bMissing = b.title.isEmpty(); break;
                case Key::artist:     order = compareText (a.artist, b.artist); aMissing = a.artist.isEmpty(); bMissing = b.artist.isEmpty(); break;
                case Key::tuning:     order = compareText (a.tuning, b.tuning); aMissing = a.tuning.isEmpty(); bMissing = b.tuning.isEmpty(); break;
                case Key::difficulty: order = a.difficulty - b.difficulty; aMissing = a.difficulty == 0; bMissing = b.difficulty == 0; break;
                case Key::length:
                    order = a.lengthSeconds < b.lengthSeconds ? -1 : (a.lengthSeconds > b.lengthSeconds ? 1 : 0);
                    aMissing = a.lengthSeconds <= 0.0; bMissing = b.lengthSeconds <= 0.0;
                    break;
                case Key::lastPracticed:
                {
                    const auto ta = lastPracticed (a).toMilliseconds(), tb = lastPracticed (b).toMilliseconds();
                    order = ta < tb ? -1 : (ta > tb ? 1 : 0);
                    break;
                }
            }

            if (aMissing != bMissing)
                return bMissing;   // 値があるほうが先

            if (order != 0)
                return ascending ? order < 0 : order > 0;

            return lastPracticed (a) > lastPracticed (b);
        });
    }
}
