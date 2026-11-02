#pragma once

#include <optional>
#include <vector>
#include "Session.h"

// セッションファイル (.gppractice) の一覧・読み書き
// 保存先: ドキュメント\GuitarPractice\Sessions
class SessionStore
{
public:
    struct Entry
    {
        juce::String id;
        juce::String sessionId;   // 古いファイルでまだ付いていなければ空 (assignMissingSessionIds() で付ける)
        juce::String title;
        juce::Time modified;
        double lengthSeconds = 0.0;   // 0 = 不明
    };

    SessionStore();
    explicit SessionStore (const juce::File& directory);   // テスト用: 保存先を指定する

    std::vector<Entry> list() const;  // 新しい順。ファイルは書き換えない
    std::optional<Session> load (const juce::String& id);   // session_id が無ければ付けて保存し直す
    bool save (Session& session);  // id・sessionId が空なら採番する
    bool remove (const juce::String& id);

    // session_id の無い古いファイルに ID を付けて保存し直す (setlist-mode.md 3.1 のマイグレーション)
    void assignMissingSessionIds();

    const juce::File& getDirectory() const noexcept { return directory; }

    static constexpr const char* fileExtension = ".gppractice";

private:
    juce::File fileFor (const juce::String& id) const;
    juce::String ensureSessionId (const juce::File& file, juce::var& parsed);

    juce::File directory;
};
