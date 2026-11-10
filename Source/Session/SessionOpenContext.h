#pragma once

#include <juce_core/juce_core.h>

// セットリストの通し再生で、次の曲へ移っても保つ画面の状態 (作者の指示・開発者用メモ)
// 前の曲の画面から取り出し、次の曲の画面に当てる。セッションには保存しない
struct SetlistCarryOver
{
    bool metronomeOn = false;        // メトロノームの「曲に合わせる」
    bool sidePanelVisible = true;    // 右のパネルを出しているか
};

// セッション画面がどこから開かれたか (setlist-mode.md 3.5)
// 「次の曲」はセッション自体ではなく、これで決まる
struct SessionOpenContext
{
    enum class Kind { Standalone, Setlist };
    Kind kind = Kind::Standalone;
    juce::String setlistId; // Setlist のときのみ
    juce::String entryId;   // Setlist のときのみ

    bool isSetlist() const noexcept { return kind == Kind::Setlist; }

    static SessionOpenContext forSetlist (const juce::String& setlistId, const juce::String& entryId)
    {
        return { Kind::Setlist, setlistId, entryId };
    }
};
