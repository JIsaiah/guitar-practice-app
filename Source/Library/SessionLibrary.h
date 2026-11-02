#pragma once

#include <map>
#include <set>
#include "../Model/SessionStore.h"

// sessionId → セッションファイルの対応表 (setlist-mode.md 3.2)
// セットリストはセッションを sessionId で指す。ファイル名ではなくこの表で引くので、
// ファイルを移動・改名してもリンクが切れない
//
// - 起動時に rescan() で保存フォルダを走査する。session_id の無い古いファイルには、ここで ID を付ける
// - 保存・削除のあとは noteSaved() / noteRemoved() で表を直す。
//   呼び忘れや、アプリの外でのファイルの変更があっても、引けなかったときに一度だけ走査し直す
//   (リンク切れの ID は覚えておき、次に rescan() するまで走査し直さない。一覧の描画のたびに走査しないため)
class SessionLibrary
{
public:
    explicit SessionLibrary (SessionStore& store);

    void rescan();

    // 見つからなければ空 (リンク切れ)
    juce::String findFileId (const juce::String& sessionId);
    bool contains (const juce::String& sessionId) { return findFileId (sessionId).isNotEmpty(); }
    std::optional<Session> load (const juce::String& sessionId);
    juce::String getTitle (const juce::String& sessionId);   // 見つからなければ空
    double getLengthSeconds (const juce::String& sessionId);   // 不明・見つからなければ 0

    void noteSaved (const Session& session);
    void noteRemoved (const juce::String& sessionId);

    SessionStore& getStore() noexcept { return store; }

private:
    struct Item
    {
        juce::String fileId;
        juce::String title;
        double lengthSeconds = 0.0;
    };

    const Item* find (const juce::String& sessionId);

    SessionStore& store;
    std::map<juce::String, Item> items;
    std::set<juce::String> knownMissing;
};
