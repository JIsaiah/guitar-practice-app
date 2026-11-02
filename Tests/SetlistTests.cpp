#include "../Source/Library/SessionLibrary.h"
#include "../Source/Library/SetlistStore.h"
#include "../Source/Text.h"

// setlist-mode.md 10章のテスト 1・2・7 (フェーズ1)
namespace
{
    // テストごとに空の一時フォルダを作り、終わったら消す
    struct TempFolder
    {
        TempFolder() : folder (juce::File::getSpecialLocation (juce::File::tempDirectory)
                                   .getChildFile ("GuitarPracticeTests")
                                   .getChildFile (juce::Uuid().toString()))
        {
            folder.createDirectory();
        }

        ~TempFolder() { folder.deleteRecursively(); }

        juce::File folder;
    };
}

class SetlistTests final : public juce::UnitTest
{
public:
    SetlistTests() : juce::UnitTest ("Setlist", "Setlist") {}

    void runTest() override
    {
        testJsonRoundTrip();
        testDuplicates();
        testMigration();
        testLibraryFollowsFileChanges();
    }

private:
    // 1. JSON の往復: 保存 → 読み込みで内容が一致する
    void testJsonRoundTrip()
    {
        beginTest ("1. JSON round trip");

        TempFolder temp;
        SetlistStore store (temp.folder);

        auto setlist = store.createNew();
        expectEquals (setlist.name, "セットリスト 1"_jp);
        setlist.name = "11月ライブ"_jp;

        auto& first = setlist.addEntry ("session-a");
        first.overrides.speed = 0.8;
        first.overrides.loopEnabled = true;
        first.overrides.loopStartSec = 12.5;
        first.overrides.loopEndSec = 30.25;
        setlist.addEntry ("session-b");

        expect (store.save (setlist));

        const auto loaded = store.load (setlist.setlistId);
        expect (loaded.has_value());
        if (! loaded.has_value())
            return;

        expectEquals (loaded->setlistId, setlist.setlistId);
        expectEquals (loaded->name, setlist.name);
        expect (loaded->entries == setlist.entries);
        expectEquals (loaded->createdAt.toMilliseconds(), setlist.createdAt.toMilliseconds());
        expectEquals (loaded->updatedAt.toMilliseconds(), setlist.updatedAt.toMilliseconds());
        expectEquals (juce::JSON::toString (loaded->toVar()), juce::JSON::toString (setlist.toVar()));

        // 一覧にも出る。番号は使われていないものを付ける
        expectEquals ((int) store.list().size(), 1);
        expectEquals (store.createNew().name, "セットリスト 1"_jp);   // 「11月ライブ」に改名したので 1 は空いている

        // 形の違うファイルは読まない
        expect (! Setlist::fromVar (juce::JSON::parse ("{\"name\": \"x\"}")).has_value());
    }

    // 2. 重複: 同じセッションを3回追加し、それぞれ別の entryId で、個別に並び替え・削除・専用設定の変更ができる
    void testDuplicates()
    {
        beginTest ("2. Duplicate entries");

        Session source;
        source.sessionId = "same";
        source.loopStartMs = 4000;
        source.loopEndMs = 8000;
        source.loopEnabled = true;

        Setlist setlist;
        const auto id1 = setlist.addEntry ("same", &source).entryId;
        const auto id2 = setlist.addEntry ("same", &source).entryId;
        setlist.addEntry ("other");
        const auto id3 = setlist.addEntry ("same", &source).entryId;

        expect (id1 != id2 && id2 != id3 && id1 != id3);
        expectEquals (setlist.countOf ("same"), 3);

        // 追加したときの専用設定: 速度は原速、ループは無効、範囲は単体セッションから写す (3.4)
        const auto* entry1 = setlist.findEntry (id1);
        expect (entry1 != nullptr);
        expectEquals (entry1->overrides.speed, 1.0);
        expect (! entry1->overrides.loopEnabled);
        expectEquals (entry1->overrides.loopStartSec, 4.0);
        expectEquals (entry1->overrides.loopEndSec, 8.0);

        // 専用設定の変更は、そのエントリだけ
        setlist.findEntry (id2)->overrides.speed = 0.75;
        expectEquals (setlist.findEntry (id1)->overrides.speed, 1.0);
        expectEquals (setlist.findEntry (id2)->overrides.speed, 0.75);
        expectEquals (setlist.findEntry (id3)->overrides.speed, 1.0);

        // 並び替え: 3つ目の "same" を先頭へ
        expect (setlist.moveEntry (id3, 0));
        expectEquals (setlist.indexOf (id3), 0);
        expectEquals (setlist.indexOf (id1), 1);
        expectEquals (setlist.indexOf (id2), 2);

        // 削除: 2つ目だけを消す
        expect (setlist.removeEntry (id2));
        expectEquals (setlist.countOf ("same"), 2);
        expect (setlist.findEntry (id2) == nullptr);
        expect (setlist.findEntry (id1) != nullptr);
        expect (setlist.findEntry (id3) != nullptr);
        expect (! setlist.removeEntry (id2));
    }

    // 7. マイグレーション: sessionId の無い古いセッションを読むと ID が付き、2回目でも同じ ID
    void testMigration()
    {
        beginTest ("7. Migration of sessions without sessionId");

        TempFolder temp;
        const auto oldFile = temp.folder.getChildFile (juce::String ("old") + SessionStore::fileExtension);
        oldFile.replaceWithText (R"({ "version": 2, "title": "Old song", "playback": { "speed_percent": 90 } })");

        const auto modified = juce::Time (2024, 0, 1, 12, 0);
        oldFile.setLastModificationTime (modified);

        SessionStore store (temp.folder);
        expect (store.list().front().sessionId.isEmpty());   // list() は書き換えない

        const auto first = store.load ("old");
        expect (first.has_value());
        if (! first.has_value())
            return;

        expect (first->sessionId.isNotEmpty());
        expectEquals (first->speedPercent, 90);   // ほかの値はそのまま

        const auto second = store.load ("old");
        expectEquals (second->sessionId, first->sessionId);
        expectEquals (store.list().front().sessionId, first->sessionId);

        // 一覧の順番が変わらないよう、更新日時は元のまま
        expectEquals (oldFile.getLastModificationTime().toMilliseconds() / 1000, modified.toMilliseconds() / 1000);

        // バージョンの無いファイルには、バージョンも付ける
        const auto noVersion = temp.folder.getChildFile (juce::String ("older") + SessionStore::fileExtension);
        noVersion.replaceWithText (R"({ "title": "Older song" })");
        store.assignMissingSessionIds();
        const auto parsed = juce::JSON::parse (noVersion.loadFileAsString());
        expect (parsed["session_id"].toString().isNotEmpty());
        expect (parsed.hasProperty ("version"));

        // 新しく保存するセッションには ID が付き、保存し直しても変わらない
        Session fresh;
        fresh.title = "New";
        expect (store.save (fresh));
        const auto freshId = fresh.sessionId;
        expect (freshId.isNotEmpty());
        expect (store.save (fresh));
        expectEquals (store.load (fresh.id)->sessionId, freshId);
    }

    // 3.2: ファイル名が変わっても sessionId で引ける。消えたらリンク切れ
    void testLibraryFollowsFileChanges()
    {
        beginTest ("SessionLibrary follows renamed and removed files");

        TempFolder temp;
        SessionStore store (temp.folder);
        SessionLibrary library (store);

        Session session;
        session.title = "Song";
        expect (store.save (session));
        library.rescan();

        expectEquals (library.findFileId (session.sessionId), session.id);
        expectEquals (library.getTitle (session.sessionId), juce::String ("Song"));

        // アプリの外でファイル名を変える
        const auto renamed = temp.folder.getChildFile (juce::String ("renamed") + SessionStore::fileExtension);
        expect (temp.folder.getChildFile (session.id + SessionStore::fileExtension).moveFileTo (renamed));
        expectEquals (library.findFileId (session.sessionId), juce::String ("renamed"));
        expect (library.load (session.sessionId).has_value());

        // 消すとリンク切れ
        expect (store.remove ("renamed"));
        library.noteRemoved (session.sessionId);
        expect (! library.contains (session.sessionId));
        expect (! library.load (session.sessionId).has_value());
        expect (! library.contains ("does-not-exist"));
    }
};

static SetlistTests setlistTests;
