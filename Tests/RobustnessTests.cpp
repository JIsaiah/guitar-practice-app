#include <juce_core/juce_core.h>
#include "../Source/Model/SessionStore.h"
#include "../Source/Model/SafeJson.h"
#include "../Source/Library/SetlistStore.h"
#include "../Source/Web/ScoreFileData.h"
#include "../Source/Web/GpScoreEdit.h"
#include "../Source/Audio/SourceLimits.h"
#include "../Source/Text.h"

// 異常系の入力 (開発者用メモ「テストすべきもの」):
// 壊れた・作り変えたファイル、極端な文字列、入れ子が深すぎる JSON、中身の大きさを偽った zip、ありえない音源の長さ
class RobustnessTests final : public juce::UnitTest
{
public:
    RobustnessTests() : juce::UnitTest ("Robustness", "Model") {}

    void runTest() override
    {
        const auto root = juce::File::getSpecialLocation (juce::File::tempDirectory)   // 短い場所 (ファイル名が長いテストで、パスの長さの上限 260 字を超えないように)
                              .getChildFile ("gp-robust-" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt()));

        testBrokenSessionFiles (root.getChildFile ("broken"));
        testExtremeTitles (root.getChildFile ("titles"));
        testLyingZip (root.getChildFile ("zip"));
        testSourceLimits();
        testSetlistIdTraversal (root.getChildFile ("setlist-ids"));
        testDeepJson (root.getChildFile ("json"));   // 直す前は、ここで落ちていた (最後に置く)

        root.deleteRecursively();
    }

private:
    static juce::String nested (int depth)
    {
        return juce::String::repeatedString ("[", depth) + juce::String::repeatedString ("]", depth);
    }

    void testDeepJson (const juce::File& dir)
    {
        beginTest ("Nesting depth is measured, ignoring brackets inside strings");
        {
            expect (SafeJson::isNestingWithin (nested (10)));
            expect (! SafeJson::isNestingWithin (nested (SafeJson::maxDepth + 1)));
            expect (SafeJson::isNestingWithin ("{\"title\": \"" + juce::String::repeatedString ("[", 1000) + "\"}"));
            expect (SafeJson::isNestingWithin ("{\"t\": \"\\\"[[[[\"}", 3));   // 文字列の中の \" の後も文字列のまま
            expect (SafeJson::parse ("{\"a\": [1, {\"b\": 2}]}").isObject());
            expect (SafeJson::parse (nested (100000)).isVoid());
        }

        beginTest ("A deeply nested session file does not crash the list (it is skipped)");
        {
            SessionStore store (dir.getChildFile ("Sessions"), dir.getChildFile ("known.json"));
            Session ok;
            ok.title = "OK";
            expect (store.save (ok));

            // 「[」を 20 万重ねた 400 KB のファイル。そのまま JSON::parse に渡すと、スタックがあふれて落ちる
            dir.getChildFile ("Sessions").getChildFile ("deep" + juce::String (SessionStore::fileExtension))
                .replaceWithText (nested (200000));

            expectEquals ((int) store.list().size(), 1);
            expect (! store.load ("deep").has_value());
            store.assignMissingSessionIds();
            store.renameToReadableNames();
            expectEquals ((int) store.list().size(), 1);
        }

        beginTest ("A deeply nested setlist file is skipped");
        {
            SetlistStore setlists (dir.getChildFile ("Setlists"));
            auto s = setlists.createNew();
            expect (setlists.save (s));
            dir.getChildFile ("Setlists").getChildFile ("deep" + juce::String (SetlistStore::fileExtension))
                .replaceWithText ("{\"entries\": " + nested (200000) + "}");
            expectEquals ((int) setlists.list().size(), 1);
            expect (! setlists.load ("deep").has_value());
        }

        beginTest ("A deeply nested trust record is ignored");
        {
            const auto record = dir.getChildFile ("known-deep.json");
            record.replaceWithText (nested (200000));
            SessionStore store (dir.getChildFile ("Sessions2"), record);   // 読めなくても落ちない
            Session s;
            s.title = "X";
            expect (store.save (s));
            expect (store.isFromThisPc (s.id));
        }
    }

    void testBrokenSessionFiles (const juce::File& dir)
    {
        beginTest ("Broken session files: empty, binary, wrong types, truncated");
        {
            const auto sessions = dir.getChildFile ("Sessions");
            SessionStore store (sessions, dir.getChildFile ("known.json"));
            const auto write = [&] (const juce::String& fileName, const juce::String& text)
            {
                sessions.getChildFile (fileName + SessionStore::fileExtension).replaceWithText (text);
            };

            write ("empty", {});
            write ("garbage", "\x01\x02 not json at all }{");
            write ("array", "[1, 2, 3]");
            write ("truncated", "{\"title\": \"abc\", \"tempo\": {\"bpm\": 12");
            write ("wrongtypes", "{\"title\": 5, \"tempo\": \"fast\", \"score\": [], \"tone_slots\": {}, \"tone_changes\": \"x\","
                                 " \"difficulty\": 99999999999, \"mixer\": {\"source\": \"loud\"}, \"source\": {\"length_sec\": -1}}");
            sessions.getChildFile ("binary" + juce::String (SessionStore::fileExtension))
                .replaceWithData ("\xff\xfe\x00\x00\x80\x81", 6);

            const auto entries = store.list();
            expectEquals ((int) entries.size(), 1);   // 読めるのは wrongtypes だけ (オブジェクト)
            for (const auto& e : entries)
                expect (e.difficulty >= 0 && e.difficulty <= Session::maxDifficulty);

            for (auto fileName : { "empty", "garbage", "array", "truncated", "binary" })
                expect (! store.load (fileName).has_value());

            if (auto s = store.load ("wrongtypes"))
            {
                expect (s->bpm >= 20.0 && s->bpm <= 400.0);
                expect (s->mixSource >= 0.0 && s->mixSource <= 1.0);
            }
            else
            {
                expect (false, "an object with wrong types should still load with defaults");
            }

            expect (! store.load ("does-not-exist").has_value());
        }
    }

    void testExtremeTitles (const juce::File& dir)
    {
        beginTest ("Extreme titles: the file always stays in the Sessions folder and the title is kept");
        {
            const auto sessions = dir.getChildFile ("Sessions");
            SessionStore store (sessions, dir.getChildFile ("known.json"));

            const juce::StringArray titles {
                {},
                "   ",
                "..",
                "../../../evil",
                "..\\..\\evil",
                "C:\\Windows\\evil",
                "CON",
                "NUL.txt",
                juce::String::repeatedString ("あ", 5000),
                juce::String::fromUTF8 ("\xf0\x9f\x8e\xb8 ギター \xf0\x9f\x94\xa5"),   // 絵文字と日本語
                "line\nbreak\ttab",
                "trailing dots...",
                "%APPDATA% $HOME ' \" ; & | `"
            };

            for (const auto& title : titles)
            {
                Session s;
                s.title = title;
                s.artist = title;
                expect (store.save (s), "save failed for title: " + title.substring (0, 40));

                const auto file = sessions.getChildFile (s.id + SessionStore::fileExtension);
                expect (file.existsAsFile(), "missing file for title: " + title.substring (0, 40));
                expect (file.getParentDirectory() == sessions, "file left the folder: " + title.substring (0, 40));
                expect (file.getFileName().length() < 200);

                const auto loaded = store.load (s.id);
                expect (loaded.has_value() && loaded->title == title, "title changed: " + title.substring (0, 40));
            }

            expectEquals ((int) store.list().size(), titles.size());
            expectEquals ((int) sessions.findChildFiles (juce::File::findFiles, true, "*").size(), titles.size());
            expect (! dir.getParentDirectory().getChildFile ("evil" + juce::String (SessionStore::fileExtension)).exists());
        }

        beginTest ("Changing only upper/lower case of the title keeps exactly one file");
        {
            const auto sessions = dir.getChildFile ("Case");
            SessionStore store (sessions, dir.getChildFile ("known-case.json"));
            Session s;
            s.title = "song";
            s.created = juce::Time (2026, 10, 24, 10, 0);
            expect (store.save (s));
            s.title = "Song";
            expect (store.save (s));
            expectEquals ((int) store.list().size(), 1);
            expect (! s.id.endsWith (" 2"), "the file got a number although there is no other session: " + s.id);
            expect (store.load (s.id).has_value() && store.load (s.id)->title == "Song");
        }
    }

    // 中身の大きさを偽った zip: 目次 (セントラルディレクトリ) には「1000 バイト」と書き、実際は 96 MB に展開されるもの
    static juce::MemoryBlock makeLyingZip (int declaredSize)
    {
        juce::MemoryBlock zeros ((size_t) 96 * 1024 * 1024, true);
        const juce::String gpif ("<GPIF></GPIF>");

        juce::ZipFile::Builder builder;
        builder.addEntry (std::make_unique<juce::MemoryInputStream> (gpif.toRawUTF8(), gpif.getNumBytesAsUTF8(), true), 6, "Content/score.gpif", {});
        builder.addEntry (std::make_unique<juce::MemoryInputStream> ("RIFF", 4, true), 6, "Content/Assets/a.wav", {});
        builder.addEntry (std::make_unique<juce::MemoryInputStream> (zeros, false), 9, "Content/bomb.bin", {});

        juce::MemoryOutputStream out;
        builder.writeToStream (out, nullptr);
        auto data = out.getMemoryBlock();

        // bomb.bin の「展開後の大きさ」を書き換える (ローカルヘッダーは +22、セントラルディレクトリは +24)
        auto* bytes = static_cast<juce::uint8*> (data.getData());
        const juce::String bombName ("Content/bomb.bin");
        const auto writeU32 = [] (juce::uint8* at, juce::uint32 value)
        {
            for (int b = 0; b < 4; ++b)
                at[b] = (juce::uint8) (value >> (8 * b));
        };
        for (size_t i = 0; i + 46 < data.getSize(); ++i)
        {
            const auto sig = juce::ByteOrder::littleEndianInt (bytes + i);
            if (sig == 0x04034b50 && juce::String::fromUTF8 ((const char*) bytes + i + 30, bombName.length()) == bombName)
                writeU32 (bytes + i + 22, (juce::uint32) declaredSize);
            if (sig == 0x02014b50 && juce::String::fromUTF8 ((const char*) bytes + i + 46, bombName.length()) == bombName)
                writeU32 (bytes + i + 24, (juce::uint32) declaredSize);
        }
        return data;
    }

    static juce::int64 unpackedSizeOf (const juce::MemoryBlock& zipData, const juce::String& entryName)
    {
        juce::ZipFile zip (std::make_unique<juce::MemoryInputStream> (zipData, false));
        const auto index = zip.getIndexOfFileName (entryName);
        if (index < 0)
            return -1;
        std::unique_ptr<juce::InputStream> stream (zip.createStreamForEntry (index));
        juce::MemoryOutputStream out;
        out.writeFromInputStream (*stream, -1);
        return (juce::int64) out.getDataSize();
    }

    void testLyingZip (const juce::File& dir)
    {
        beginTest ("A zip that lies about the unpacked size is not unpacked past what it declares");
        {
            dir.createDirectory();
            const auto file = dir.getChildFile ("lying.gp");
            const auto data = makeLyingZip (1000);
            expect (file.replaceWithData (data.getData(), data.getSize()));
            expect (file.getSize() < 2 * 1024 * 1024);

            // 目次を信じると、上限 (64 MB) の内側に見える
            expect (ScoreFileData::isWithinLimits (file));

            // タブ譜のページに渡すもの (音声を抜いた zip): bomb.bin は書いてある大きさまでしか展開しない
            juce::MemoryBlock pageData;
            if (ScoreFileData::load (file, pageData))
                expect (unpackedSizeOf (pageData, "Content/bomb.bin") <= 1000,
                        "unpacked " + juce::String (unpackedSizeOf (pageData, "Content/bomb.bin")) + " bytes");

            // 拍・小節の挿入 (メモリの中で zip を作り直す) も同じ
            juce::MemoryBlock edited;
            if (GpScoreEdit::applyToGpData (data, {}, edited).wasOk())
                expect (unpackedSizeOf (edited, "Content/bomb.bin") <= 1000,
                        "unpacked " + juce::String (unpackedSizeOf (edited, "Content/bomb.bin")) + " bytes");
        }

        beginTest ("An honest .gp with embedded audio is still unpacked in full");
        {
            const auto file = dir.getChildFile ("honest.gp");
            juce::MemoryBlock audio ((size_t) 3 * 1024 * 1024, true);
            const juce::String gpif ("<GPIF></GPIF>");
            juce::ZipFile::Builder builder;
            builder.addEntry (std::make_unique<juce::MemoryInputStream> (gpif.toRawUTF8(), gpif.getNumBytesAsUTF8(), true), 6, "Content/score.gpif", {});
            builder.addEntry (std::make_unique<juce::MemoryInputStream> (audio, false), 6, "Content/Assets/a.wav", {});
            builder.addEntry (std::make_unique<juce::MemoryInputStream> (audio, false), 6, "Content/other.bin", {});
            juce::MemoryOutputStream out;
            expect (builder.writeToStream (out, nullptr));
            expect (file.replaceWithData (out.getData(), out.getDataSize()));

            juce::MemoryBlock pageData;
            expect (ScoreFileData::load (file, pageData));
            expectEquals (unpackedSizeOf (pageData, "Content/other.bin"), (juce::int64) audio.getSize());
            expectEquals (unpackedSizeOf (pageData, "Content/score.gpif"), (juce::int64) gpif.length());
            expectEquals (unpackedSizeOf (pageData, "Content/Assets/a.wav"), (juce::int64) -1);   // 音声は抜く

            juce::MemoryBlock edited;
            expect (GpScoreEdit::applyToGpData (out.getMemoryBlock(), {}, edited).wasOk());
            expectEquals (unpackedSizeOf (edited, "Content/other.bin"), (juce::int64) audio.getSize());
            expectEquals (unpackedSizeOf (edited, "Content/Assets/a.wav"), (juce::int64) audio.getSize());
        }
    }

    void testSetlistIdTraversal (const juce::File& dir)
    {
        beginTest ("A setlist whose ID points outside the folder cannot write or delete files there");
        {
            const auto folder = dir.getChildFile ("Setlists");
            SetlistStore store (folder);

            // フォルダの外に、同じ拡張子の「作者のファイル」を置いておく
            const auto victim = dir.getChildFile ("victim" + juce::String (SetlistStore::fileExtension));
            victim.replaceWithText ("keep me");

            auto evil = store.createNew();
            auto v = evil.toVar();
            v.getDynamicObject()->setProperty ("setlistId", "..\victim");
            folder.getChildFile ("received" + juce::String (SetlistStore::fileExtension)).replaceWithText (juce::JSON::toString (v));

            auto listed = store.list();
            expectEquals ((int) listed.size(), 1);
            expectEquals (listed[0].setlistId, juce::String ("received"));   // ファイル名が ID になる

            expect (store.save (listed[0]));
            expect (! store.remove ("..\victim"));
            expect (! store.load ("..\victim").has_value());
            expectEquals (victim.loadFileAsString(), juce::String ("keep me"));
            expect (folder.getChildFile ("received" + juce::String (SetlistStore::fileExtension)).existsAsFile());
            expect (store.remove ("received"));
            expectEquals ((int) store.list().size(), 0);

            Setlist bad;
            bad.setlistId = "../x";
            expect (store.save (bad));   // 使えない ID は、新しい ID に替えて保存する
            expect (SetlistStore::isSafeId (bad.setlistId));
            expect (! dir.getChildFile ("x" + juce::String (SetlistStore::fileExtension)).exists());
        }
    }

    void testSourceLimits()
    {
        beginTest ("Audio files: impossible sample rates or lengths are not loaded");
        {
            expect (SourceLimits::isLoadable (48000LL * 60 * 5, 48000.0));            // 5 分
            expect (SourceLimits::isLoadable (192000LL * 60 * 29, 192000.0));         // 29 分のハイレゾ
            expect (! SourceLimits::isLoadable (48000LL * 60 * 31, 48000.0));         // 31 分
            expect (! SourceLimits::isLoadable (0, 48000.0));
            expect (! SourceLimits::isLoadable (-5, 48000.0));
            expect (! SourceLimits::isLoadable (1000, 0.0));
            expect (! SourceLimits::isLoadable (1000, std::numeric_limits<double>::quiet_NaN()));
            expect (! SourceLimits::isLoadable (4000000000LL, 4.0e9));                // 作り変えたサンプルレート (1 秒分だが 16 GB)
            expect (! SourceLimits::isLoadable (3000000000LL, 2.0e6));                // int に入らない長さ
        }
    }
};

static RobustnessTests robustnessTests;
