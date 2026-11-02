#pragma once

#include "../Library/Setlist.h"

// セットリストの通し再生での「今の曲」と「次・前の曲」(setlist-mode.md 5.2、7.3)
// - 今の曲は entryId で持つ (同じ曲が何度入っていても区別できる。曲順が変わっても見失わない)
// - リンク切れ (参照先のセッションが無い) の曲は飛ばす
// 画面に依存しない。曲を開くたびに、最新のセットリストから作り直して使う
class SetlistPlayer
{
public:
    using SessionExists = std::function<bool (const juce::String& sessionId)>;

    SetlistPlayer (Setlist setlist, SessionExists exists, const juce::String& currentEntryId);

    const Setlist& getSetlist() const noexcept { return setlist; }
    int getNumEntries() const noexcept { return (int) setlist.entries.size(); }

    int getCurrentIndex() const;                // 今の曲の位置 (0 から)。見つからなければ -1
    const SetlistEntry* getCurrent() const;

    // 次・前の開ける曲の位置。無ければ -1 (最初の曲で「前」、最後の曲で「次」)
    int findNext() const;
    int findPrevious() const;
    bool hasNext() const     { return findNext() >= 0; }
    bool hasPrevious() const { return findPrevious() >= 0; }
    const SetlistEntry* getNext() const;

    // 次・前へ移る。移れなければ false
    bool goNext();
    bool goPrevious();

    // その位置から、開ける最初の曲 (「通しで再生」「この曲から通しで再生」)。無ければ -1
    int findPlayableFrom (int index) const;

    // セットリスト全体の進み具合 (0〜1)。【仮決め】曲数で数える: (終わった曲数 + 今の曲の割合) / 曲数
    double getProgress (double fractionOfCurrentSong) const;

private:
    bool isPlayable (int index) const;

    Setlist setlist;
    SessionExists exists;
    juce::String currentEntryId;
};
