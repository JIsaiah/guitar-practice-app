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
        juce::String title;
        juce::Time modified;
    };

    SessionStore();

    std::vector<Entry> list() const;  // 新しい順
    std::optional<Session> load (const juce::String& id) const;
    bool save (Session& session);  // id が空なら採番する
    bool remove (const juce::String& id);

    const juce::File& getDirectory() const noexcept { return directory; }

    static constexpr const char* fileExtension = ".gppractice";

private:
    juce::File fileFor (const juce::String& id) const;

    juce::File directory;
};
