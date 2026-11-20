#include <juce_core/juce_core.h>
#include "../Source/Model/SessionStore.h"

// セキュリティ: この PC で保存したセッションか (SessionTrust.h・SessionStore)
class SessionTrustTests final : public juce::UnitTest
{
public:
    SessionTrustTests() : juce::UnitTest ("SessionTrust", "Model") {}

    struct TempFolder
    {
        TempFolder()
        {
            root = juce::File::getSpecialLocation (juce::File::tempDirectory)
                       .getChildFile ("SessionTrustTests-" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64()));
            root.createDirectory();
        }
        ~TempFolder() { root.deleteRecursively(); }

        juce::File sessions() const { return root.getChildFile ("Sessions"); }
        juce::File record() const   { return root.getChildFile ("known-sessions.json"); }
        juce::File root;
    };

    static Session makeSession (const juce::String& title)
    {
        Session s;
        s.title = title;
        return s;
    }

    // 人から受け取ったファイルを、セッションのフォルダに入れる
    static juce::String dropForeignFile (const juce::File& folder, const juce::String& name, const juce::String& sessionId = {})
    {
        auto s = makeSession (name);
        s.sessionId = sessionId;
        s.created = juce::Time::getCurrentTime();
        s.pluginPath = "C:\\Users\\x\\evil.vst3";
        auto v = s.toVar();
        if (sessionId.isEmpty())
            if (auto* o = v.getDynamicObject())
                o->removeProperty ("session_id");
        folder.getChildFile (name + SessionStore::fileExtension).replaceWithText (juce::JSON::toString (v));
        return name;
    }

    void runTest() override
    {
        beginTest ("A session saved here is from this PC; changed outside the app, it is not");
        {
            TempFolder temp;
            SessionStore store (temp.sessions(), temp.record());
            auto s = makeSession ("Song");
            expect (store.save (s));
            expect (store.isFromThisPc (s.id));

            // アプリの外で書き換えた (1文字でも)
            const auto file = temp.sessions().getChildFile (s.id + SessionStore::fileExtension);
            file.appendText (" ");
            expect (! store.isFromThisPc (s.id));

            // 作者が保存し直したら、また この PC のもの
            expect (store.save (s));
            expect (store.isFromThisPc (s.id));
        }

        beginTest ("A file dropped into the folder is not from this PC, even after the app rewrites it");
        {
            TempFolder temp;
            SessionStore store (temp.sessions(), temp.record());
            const auto id = dropForeignFile (temp.sessions(), "Shared");
            expect (! store.isFromThisPc (id));

            // 読み込むと session_id を足して書き直す。一覧で星を付ける (update)。開いたことを記録する。どれも信じない
            expect (store.load (id).has_value());
            expect (! store.isFromThisPc (id));
            auto updated = store.update (id, [] (Session& s) { s.difficulty = 3; });
            expect (updated.has_value());
            expect (! store.isFromThisPc (updated->id));
            const auto opened = store.markOpened (updated->id);
            expect (! store.isFromThisPc (opened));

            // 確認で「信頼して開く」を選んだ
            store.trustSession (opened);
            expect (store.isFromThisPc (opened));
            expect (store.isFromThisPc (store.markOpened (opened)));   // 開いた日時を書いても、信じたまま
        }

        beginTest ("Sessions that existed before this version are trusted once, on the first run");
        {
            TempFolder temp;
            temp.sessions().createDirectory();
            const auto id = dropForeignFile (temp.sessions(), "Old");
            SessionStore store (temp.sessions(), temp.record());   // 記録が無い = この仕組みが入る前の版から上げた
            expect (store.isFromThisPc (id));

            const auto later = dropForeignFile (temp.sessions(), "Later");   // そのあとに入れたものは信じない
            SessionStore again (temp.sessions(), temp.record());          // 記録を読み直す
            expect (! again.isFromThisPc (later));
            expect (again.isFromThisPc (id));
        }

        beginTest ("A shared session with the same session_id cannot take the place of the author's");
        {
            TempFolder temp;
            SessionStore store (temp.sessions(), temp.record());
            auto mine = makeSession ("Mine");
            expect (store.save (mine));
            const auto copied = dropForeignFile (temp.sessions(), "Copy", mine.sessionId);

            store.assignMissingSessionIds();
            expectEquals (store.load (mine.id)->sessionId, mine.sessionId);   // 作者のものは、そのまま
            expect (store.load (copied)->sessionId != mine.sessionId);       // 受け取ったものに新しい ID
            expect (store.isFromThisPc (mine.id));
            expect (! store.isFromThisPc (copied));
        }
    }
};

static SessionTrustTests sessionTrustTests;
