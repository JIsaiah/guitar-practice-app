#include "../Source/Model/MusicInfo.h"
#include "../Source/Model/SessionStore.h"
#include "../Source/Text.h"

// 曲の詳細 (チューニングの名前、拍の位置、詳細の保存)
class MusicInfoTests final : public juce::UnitTest
{
public:
    MusicInfoTests() : juce::UnitTest ("MusicInfo", "MusicInfo") {}

    void runTest() override
    {
        testTuningNames();
        testLocateBeat();
        testDetailsRoundTrip();
    }

private:
    void testTuningNames()
    {
        beginTest ("チューニングの名前"_jp);
        using MusicInfo::describeTuning;

        expectEquals (describeTuning ({ 64, 59, 55, 50, 45, 40 }), juce::String ("E Standard"));
        expectEquals (describeTuning ({ 63, 58, 54, 49, 44, 39 }), juce::String ("Eb Standard"));
        expectEquals (describeTuning ({ 64, 59, 55, 50, 45, 38 }), juce::String ("Drop D"));
        expectEquals (describeTuning ({ 62, 57, 53, 48, 43, 36 }), juce::String ("Drop C"));
        expectEquals (describeTuning ({ 64, 59, 55, 50, 45, 40, 35 }), "B Standard (7弦)"_jp);
        expectEquals (describeTuning ({ 43, 38, 33, 28 }), "E Standard (4弦)"_jp);
        expectEquals (describeTuning ({ 62, 57, 55, 50, 45, 38 }), juce::String ("D A D G A D"));
        expectEquals (describeTuning ({}), juce::String());
    }

    void testLocateBeat()
    {
        beginTest ("小節の何拍目か"_jp);
        using namespace MusicInfo;

        // タブ譜なし: 4/4
        auto p = locateBeat (5.5, {}, {});
        expectEquals (p.bar, 1);
        expectEquals (p.beatInBar, 1);
        expectEquals (p.signature.numerator, 4);

        // 4/4 → 3/4 → 6/8 (6/8 は 3拍分の長さで、8分音符ごとに進む)
        const std::vector<double> bars { 0.0, 4.0, 7.0 };
        const std::vector<TimeSignature> sigs { { 4, 4 }, { 3, 4 }, { 6, 8 } };

        p = locateBeat (4.0, bars, sigs);
        expectEquals (p.bar, 1);
        expectEquals (p.beatInBar, 0);
        expectEquals (p.signature.numerator, 3);

        p = locateBeat (6.9, bars, sigs);
        expectEquals (p.beatInBar, 2);

        p = locateBeat (8.6, bars, sigs);   // 6/8 の小節の頭から 1.6 拍 = 8分音符 3.2 個
        expectEquals (p.bar, 2);
        expectEquals (p.beatInBar, 3);
        expectEquals (p.signature.denominator, 8);

        // 最後の小節より後は、同じ拍子が続く
        p = locateBeat (10.5, bars, sigs);  // 7 + 3 = 10 から次の小節
        expectEquals (p.bar, 3);
        expectEquals (p.beatInBar, 1);
    }

    void testDetailsRoundTrip()
    {
        beginTest ("曲の詳細の保存と、一覧の並びを変えない更新"_jp);

        const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                .getChildFile ("GuitarPracticeTests").getChildFile (juce::Uuid().toString());
        {
            SessionStore store (folder);

            Session older;
            older.title = "Older";
            expect (store.save (older));
            juce::File (folder.getChildFile (older.id + SessionStore::fileExtension))
                .setLastModificationTime (juce::Time::getCurrentTime() - juce::RelativeTime::hours (1));

            Session newer;
            newer.title = "Newer";
            expect (store.save (newer));

            const auto updated = store.update (older.id, [] (Session& s)
            {
                s.artist = "Artist";
                s.tuning = "Drop D";
                s.difficulty = 4;
                s.bpm = 150.0;
            });
            expect (updated.has_value());

            const auto entries = store.list();
            expectEquals ((int) entries.size(), 2);
            expectEquals (entries[0].title, juce::String ("Newer"));   // 更新しても上に来ない
            expectEquals (entries[1].artist, juce::String ("Artist"));
            expectEquals (entries[1].tuning, juce::String ("Drop D"));
            expectEquals (entries[1].difficulty, 4);
            expectEquals (juce::roundToInt (entries[1].bpm), 150);
        }
        folder.deleteRecursively();
    }
};

static MusicInfoTests musicInfoTests;
