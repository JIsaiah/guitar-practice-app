#pragma once

#include "Setlist.h"

// セットリストファイル (.gpsetlist) の一覧・読み書き (setlist-mode.md 3.3)
// 保存先: ドキュメント\suguhiku\Setlists (AppPaths.h。前は ドキュメント\GuitarPractice)。ファイル名は setlistId
// セットリストを消しても、中のセッションは消さない
class SetlistStore
{
public:
    SetlistStore();
    explicit SetlistStore (const juce::File& directory);   // テスト用: 保存先を指定する

    std::vector<Setlist> list() const;   // 更新日時の新しい順
    std::optional<Setlist> load (const juce::String& setlistId) const;
    bool save (Setlist& setlist);         // setlistId が空なら採番する。updatedAt を今にする
    bool remove (const juce::String& setlistId);

    // 新しいセットリスト。名前は「セットリスト 1」のように、使われていない番号で付ける (4.5)
    Setlist createNew() const;

    // そのセッションを使っているセットリストの数 (セッションを消すときの確認。4.6)
    int countSetlistsUsing (const juce::String& sessionId) const;

    const juce::File& getDirectory() const noexcept { return directory; }

    static constexpr const char* fileExtension = ".gpsetlist";

private:
    juce::File fileFor (const juce::String& setlistId) const;

    juce::File directory;
};
