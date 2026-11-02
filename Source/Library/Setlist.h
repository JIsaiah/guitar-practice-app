#pragma once

#include <optional>
#include <vector>
#include "../Model/Session.h"

// 【仮決め】の値 (setlist-mode.md)。後から変えやすいように、ここにまとめる
namespace SetlistDefaults
{
    // 3.4: エントリを追加したとき、単体セッションのループ範囲を (無効のまま) 写しておく
    inline constexpr bool copyLoopRangeFromSession = true;
}

// セットリストの1曲 (エントリ)。セッションのデータは持たず、sessionId で参照する (setlist-mode.md 3.3)
// 同じセッションが何度入ってもよいので、選択・並び替え・削除・専用設定はすべて entryId で扱う
struct SetlistEntry
{
    // エントリ専用のループ・速度。単体セッションの設定とは独立 (3.4、3.6)
    struct Overrides
    {
        double speed = 1.0;   // 1.0 = 原速
        bool loopEnabled = false;
        double loopStartSec = 0.0, loopEndSec = 0.0;

        bool isDefault() const noexcept { return speed == 1.0 && ! loopEnabled; }   // 一覧の「速度80%」などの印を出すか
        bool operator== (const Overrides&) const = default;
    };

    juce::String entryId;
    juce::String sessionId;
    Overrides overrides;

    bool operator== (const SetlistEntry&) const = default;
};

// セットリスト (.gpsetlist)。曲順は entries の順番
struct Setlist
{
    static constexpr int formatVersion = 1;

    juce::String setlistId;
    juce::String name;
    juce::Time createdAt, updatedAt;
    std::vector<SetlistEntry> entries;

    //==== 編集 ====
    // 末尾に1件追加する。source があれば、ループ範囲を写す (SetlistDefaults)
    SetlistEntry& addEntry (const juce::String& sessionId, const Session* source = nullptr);
    bool removeEntry (const juce::String& entryId);
    bool moveEntry (const juce::String& entryId, int newIndex);   // newIndex: 移動後の位置

    int indexOf (const juce::String& entryId) const;   // 無ければ -1
    SetlistEntry* findEntry (const juce::String& entryId);
    const SetlistEntry* findEntry (const juce::String& entryId) const;
    int countOf (const juce::String& sessionId) const;   // 「追加済み×2」の表示用

    //==== 保存 ====
    juce::var toVar() const;
    static std::optional<Setlist> fromVar (const juce::var& value);   // 形が違えば nullopt
};
