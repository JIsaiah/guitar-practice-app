#include "../Source/Library/SetlistStore.h"
#include "../Source/Model/SessionStore.h"
#include "../Source/Session/PlaybackSettingsResolver.h"

// setlist-mode.md 10章のテスト 3・4 (フェーズ3)
// 練習画面と同じ流れ (開く → 画面の値を変える → 保存) を、ファイルへの保存も含めて確かめる
class PlaybackSettingsResolverTests final : public juce::UnitTest
{
public:
    PlaybackSettingsResolverTests() : juce::UnitTest ("PlaybackSettingsResolver", "Setlist") {}

    void runTest() override
    {
        folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                     .getChildFile ("GuitarPracticeTests").getChildFile (juce::Uuid().toString());

        testOverridesAreIndependent();
        testSharedSettings();
        testLoopSuspendedInSetlistPlay();

        folder.deleteRecursively();
    }

private:
    struct Fixture
    {
        explicit Fixture (const juce::File& dir)
            : sessions (dir.getChildFile ("Sessions")), setlists (dir.getChildFile ("Setlists"))
        {
            Session song;
            song.title = "Song";
            song.speedPercent = 100;
            song.loopStartMs = 1000;
            song.loopEndMs = 5000;
            song.loopEnabled = true;
            sessions.save (song);
            sessionFileId = song.id;

            setlist = setlists.createNew();
            entryId = setlist.addEntry (song.sessionId, &song).entryId;
            setlists.save (setlist);
        }

        Session loadSession() { return *sessions.load (sessionFileId); }
        SetlistEntry::Overrides loadOverrides() { return setlists.load (setlist.setlistId)->findEntry (entryId)->overrides; }

        // 練習画面の保存と同じ: 分けてから、それぞれのファイルに保存する
        void save (const PlaybackSettingsResolver& resolver, const Session& working)
        {
            auto split = resolver.split (working);
            sessions.save (split.session);

            if (split.overrides.has_value())
            {
                auto latest = *setlists.load (setlist.setlistId);
                latest.findEntry (entryId)->overrides = *split.overrides;
                setlists.save (latest);
            }
        }

        SessionStore sessions;
        SetlistStore setlists;
        Setlist setlist;
        juce::String sessionFileId, entryId;
    };

    // 3. 専用設定の独立: Setlist で速度・ループを変えても単体セッションは変わらない。逆も同じ
    void testOverridesAreIndependent()
    {
        beginTest ("3. Speed and loop in a setlist are independent of the session");

        Fixture f (folder.getChildFile ("t3"));

        // 追加したときの専用設定: 原速、ループは無効 (範囲は写してある)
        {
            PlaybackSettingsResolver resolver (f.loadOverrides());
            expect (resolver.usesOverrides());
            const auto working = resolver.makeWorkingCopy (f.loadSession());
            expectEquals (working.speedPercent, 100);
            expect (! working.loopEnabled);
            expectEquals (working.loopStartMs, 1000);
            expectEquals (working.loopEndMs, 5000);
        }

        // Setlist で変える
        {
            PlaybackSettingsResolver resolver (f.loadOverrides());
            auto working = resolver.makeWorkingCopy (f.loadSession());
            working.speedPercent = 80;
            working.loopEnabled = true;
            working.loopStartMs = 2000;
            f.save (resolver, working);
        }

        auto session = f.loadSession();
        expectEquals (session.speedPercent, 100);   // 単体セッションはそのまま
        expectEquals (session.loopStartMs, 1000);
        expect (session.loopEnabled);

        auto overrides = f.loadOverrides();
        expectEquals (overrides.speed, 0.8);
        expect (overrides.loopEnabled);
        expectEquals (overrides.loopStartSec, 2.0);

        // 単体 (Standalone) で変える
        {
            PlaybackSettingsResolver resolver;
            expect (! resolver.usesOverrides());
            auto working = resolver.makeWorkingCopy (f.loadSession());
            expectEquals (working.speedPercent, 100);
            working.speedPercent = 120;
            working.loopEnabled = false;
            f.save (resolver, working);
        }

        session = f.loadSession();
        expectEquals (session.speedPercent, 120);
        expect (! session.loopEnabled);

        overrides = f.loadOverrides();   // セットリストのほうはそのまま
        expectEquals (overrides.speed, 0.8);
        expect (overrides.loopEnabled);

        // もう一度 Setlist で開くと、専用設定が出る
        PlaybackSettingsResolver resolver (f.loadOverrides());
        const auto working = resolver.makeWorkingCopy (f.loadSession());
        expectEquals (working.speedPercent, 80);
        expect (working.loopEnabled);
        expectEquals (working.loopStartMs, 2000);
    }

    // 4. 共有設定: Setlist で音色などを変えると、単体セッションにも反映される
    void testSharedSettings()
    {
        beginTest ("4. Tone and mixer changes in a setlist are saved to the session");

        Fixture f (folder.getChildFile ("t4"));

        {
            PlaybackSettingsResolver resolver (f.loadOverrides());
            auto working = resolver.makeWorkingCopy (f.loadSession());
            working.tonePresetId = "nam";
            working.namModelPath = "C:/Models/amp.nam";
            working.mixGuitar = 0.5;
            working.syncOffsetMs = 40;
            f.save (resolver, working);
        }

        PlaybackSettingsResolver standalone;
        const auto session = standalone.makeWorkingCopy (f.loadSession());
        expectEquals (session.tonePresetId, juce::String ("nam"));
        expectEquals (session.namModelPath, juce::String ("C:/Models/amp.nam"));
        expectEquals (session.mixGuitar, 0.5);
        expectEquals (session.syncOffsetMs, 40);
    }

    // 通し再生ではループを外して開く。触らなければ、エントリの「有効」は元のまま (作者の指示)
    void testLoopSuspendedInSetlistPlay()
    {
        beginTest ("Loop is turned off in setlist play, and kept unless toggled");

        SetlistEntry::Overrides overrides;
        overrides.loopEnabled = true;
        overrides.loopStartSec = 10.0;
        overrides.loopEndSec = 20.0;

        Session stored;
        stored.speedPercent = 100;

        // 開くと外れている。範囲は残る
        {
            PlaybackSettingsResolver resolver (overrides);
            const auto working = resolver.makeWorkingCopy (stored, true);
            expect (! working.loopEnabled);
            expectEquals (working.loopStartMs, 10000);
            expectEquals (working.loopEndMs, 20000);

            // 速度だけ変えて保存しても、ループの「有効」は消えない
            auto changed = working;
            changed.speedPercent = 80;
            const auto split = resolver.split (changed);
            expect (split.overrides->loopEnabled);
            expectEquals (split.overrides->speed, 0.8);
        }

        // 開いている間に自分でループを入れたら、それを保存する
        {
            PlaybackSettingsResolver resolver (overrides);
            auto working = resolver.makeWorkingCopy (stored, true);
            working.loopEnabled = true;
            working.loopStartMs = 12000;
            const auto split = resolver.split (working);
            expect (split.overrides->loopEnabled);
            expectEquals (split.overrides->loopStartSec, 12.0);
        }

        // 外さずに開くとき (suspendLoop = false) は、今までどおり
        {
            PlaybackSettingsResolver resolver (overrides);
            expect (resolver.makeWorkingCopy (stored).loopEnabled);
        }

        // 単体 (Standalone) では外さない
        {
            Session looped;
            looped.loopEnabled = true;
            PlaybackSettingsResolver standalone;
            expect (standalone.makeWorkingCopy (looped, true).loopEnabled);
        }
    }

    juce::File folder;
};

static PlaybackSettingsResolverTests playbackSettingsResolverTests;
