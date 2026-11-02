#include "../Source/Setlist/SetlistPlayer.h"

// setlist-mode.md 10章のテスト 5・6 (フェーズ4)
class SetlistPlayerTests final : public juce::UnitTest
{
public:
    SetlistPlayerTests() : juce::UnitTest ("SetlistPlayer", "Setlist") {}

    void runTest() override
    {
        // 曲順: A, (リンク切れ), B, A (同じ曲をもう一度), (リンク切れ)
        Setlist setlist;
        const auto a1 = setlist.addEntry ("A").entryId;
        const auto missing1 = setlist.addEntry ("missing").entryId;
        const auto b = setlist.addEntry ("B").entryId;
        const auto a2 = setlist.addEntry ("A").entryId;
        setlist.addEntry ("missing");

        const auto exists = [] (const juce::String& sessionId) { return sessionId != "missing"; };

        beginTest ("5. Broken links are skipped by next and previous");
        {
            SetlistPlayer player (setlist, exists, a1);
            expectEquals (player.getCurrentIndex(), 0);
            expectEquals (player.findNext(), 2);   // リンク切れの 1 を飛ばす
            expect (player.getNext() != nullptr && player.getNext()->entryId == b);

            expect (player.goNext());
            expectEquals (player.getCurrentIndex(), 2);
            expectEquals (player.findPrevious(), 0);   // 戻るときも飛ばす

            expect (player.goNext());
            expect (player.getCurrent()->entryId == a2);   // 同じ曲の2回目は、別のエントリとして進む
            expectEquals (player.getCurrentIndex(), 3);
        }

        beginTest ("5. Automatic advance skips broken links (same rule as next)");
        {
            // 自動送りも goNext() を使う。最後の開ける曲の後ろがリンク切れだけなら、そこで終わる
            SetlistPlayer player (setlist, exists, a2);
            expect (! player.hasNext());
            expect (player.getNext() == nullptr);
            expect (! player.goNext());
            expectEquals (player.getCurrentIndex(), 3);
        }

        beginTest ("6. Previous is disabled on the first song, next on the last");
        {
            SetlistPlayer first (setlist, exists, a1);
            expect (! first.hasPrevious());
            expect (! first.goPrevious());
            expect (first.hasNext());

            SetlistPlayer last (setlist, exists, a2);
            expect (last.hasPrevious());
            expect (! last.hasNext());

            // 今の曲がリンク切れでも (ふつうは開かないが)、前後の開ける曲は求まる
            SetlistPlayer onMissing (setlist, exists, missing1);
            expectEquals (onMissing.findPrevious(), 0);
            expectEquals (onMissing.findNext(), 2);
        }

        beginTest ("Start position and progress");
        {
            SetlistPlayer player (setlist, exists, {});
            expectEquals (player.findPlayableFrom (0), 0);
            expectEquals (player.findPlayableFrom (1), 2);
            expectEquals (player.findPlayableFrom (4), -1);
            expectEquals (player.getProgress (0.5), 0.0);   // 今の曲が無い

            SetlistPlayer third (setlist, exists, b);
            expectWithinAbsoluteError (third.getProgress (0.0), 2.0 / 5.0, 1.0e-9);
            expectWithinAbsoluteError (third.getProgress (0.5), 2.5 / 5.0, 1.0e-9);
            expectWithinAbsoluteError (third.getProgress (2.0), 3.0 / 5.0, 1.0e-9);   // 割合は 1 まで
        }

        beginTest ("The current song is found by entryId after the order changes");
        {
            auto reordered = setlist;
            reordered.moveEntry (a2, 0);
            SetlistPlayer player (reordered, exists, a2);
            expectEquals (player.getCurrentIndex(), 0);
            expect (! player.hasPrevious());
            expectEquals (player.findNext(), 1);   // a1
        }
    }
};

static SetlistPlayerTests setlistPlayerTests;
