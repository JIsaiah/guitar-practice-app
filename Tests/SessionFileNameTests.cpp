#include <juce_core/juce_core.h>
#include "../Source/Model/SessionStore.h"
#include "../Source/Text.h"

// セッションのファイル名 (SessionStore): 「曲名 - アーティスト名 (作った日時).gppractice」
class SessionFileNameTests final : public juce::UnitTest
{
public:
    SessionFileNameTests() : juce::UnitTest ("SessionFileName", "Model") {}

    void runTest() override
    {
        const auto made = juce::Time (2026, 10, 11, 14, 32);   // 2026年11月11日 14:32 (月は 0 から)

        beginTest ("Readable base name");
        {
            expectEquals (SessionStore::baseNameFor ("Hotel California", "Eagles", made),
                          juce::String ("Hotel California - Eagles (2026-11-11 14-32)"));
            expectEquals (SessionStore::baseNameFor ("Solo", {}, made), juce::String ("Solo (2026-11-11 14-32)"));
            expectEquals (SessionStore::baseNameFor ({}, {}, made), "無題 (2026-11-11 14-32)"_jp);
            // ファイル名に使えない文字は「_」に
            expectEquals (SessionStore::baseNameFor ("AC/DC: Live?", "a<b>", made),
                          juce::String ("AC_DC_ Live_ - a_b_ (2026-11-11 14-32)"));
        }

        const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                .getChildFile ("GuitarPracticeTests").getChildFile (juce::Uuid().toString());
        {
            SessionStore store (folder);

            beginTest ("Saving names the file after the song, and renames it when the title changes");
            Session s;
            s.title = "Song";
            s.artist = "Band";
            s.created = made;
            expect (store.save (s));
            expectEquals (s.id, juce::String ("Song - Band (2026-11-11 14-32)"));
            expect (folder.getChildFile (s.id + SessionStore::fileExtension).existsAsFile());

            const auto sessionId = s.sessionId;
            s.title = "Song 2";
            expect (store.save (s));
            expectEquals (s.id, juce::String ("Song 2 - Band (2026-11-11 14-32)"));
            expectEquals (s.sessionId, sessionId);   // セットリストが指す ID は変わらない
            expectEquals ((int) store.list().size(), 1);   // 古い名前のファイルは残らない

            beginTest ("Same name: a number is added, and saving again keeps it");
            Session twin;
            twin.title = "Song 2";
            twin.artist = "Band";
            twin.created = made;
            expect (store.save (twin));
            expectEquals (twin.id, juce::String ("Song 2 - Band (2026-11-11 14-32) 2"));
            expect (store.save (twin));
            expectEquals (twin.id, juce::String ("Song 2 - Band (2026-11-11 14-32) 2"));
            expect (store.save (s));
            expectEquals (s.id, juce::String ("Song 2 - Band (2026-11-11 14-32)"));

            beginTest ("New sessions get the time they were made");
            Session fresh;
            fresh.title = "Fresh";
            expect (store.save (fresh));
            expect (fresh.created.toMilliseconds() > 0);
            expect (fresh.id.startsWith ("Fresh ("));

            beginTest ("Old files with ID names are renamed, keeping the list order");
            const auto old = folder.getChildFile ("2b6faa7a23c343f8b79347ba2ef1edaf" + juce::String (SessionStore::fileExtension));
            expect (old.replaceWithText (R"({ "version": 3, "session_id": "abc", "title": "Old Song", "artist": "X", "custom": 1 })"));
            const auto modified = juce::Time::getCurrentTime() - juce::RelativeTime::days (3);
            old.setLastModificationTime (modified);

            store.renameToReadableNames();
            expect (! old.exists());

            bool found = false;
            for (const auto& e : store.list())
                if (e.sessionId == "abc")
                {
                    found = true;
                    expect (e.id.startsWith ("Old Song - X ("));
                    expect (std::abs ((e.modified - modified).inSeconds()) < 2.0);   // 一覧の並びは変わらない
                    const auto parsed = juce::JSON::parse (folder.getChildFile (e.id + SessionStore::fileExtension).loadFileAsString());
                    expect (parsed["created"].toString().isNotEmpty());   // 作った日時を書き足した
                    expectEquals ((int) parsed["custom"], 1);              // 知らない項目も消さない
                }
            expect (found);

            // もう一度呼んでも、名前は変わらない
            const auto before = store.list().size();
            store.renameToReadableNames();
            expectEquals (store.list().size(), before);
        }
        folder.deleteRecursively();
    }
};

static SessionFileNameTests sessionFileNameTests;
