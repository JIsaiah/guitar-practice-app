#include "../Source/Library/SessionSort.h"

// 一覧の並び替え (見出しの ↓↑)
class SessionSortTests final : public juce::UnitTest
{
public:
    SessionSortTests() : juce::UnitTest ("SessionSort", "List") {}

    void runTest() override
    {
        const auto day = [] (int d) { return juce::Time (2026, 10, d, 12, 0); };

        const auto make = [] (const char* id, const char* title, const char* artist, int difficulty, double length,
                              juce::Time modified, juce::Time opened)
        {
            SessionStore::Entry e;
            e.id = id;
            e.title = title;
            e.artist = artist;
            e.difficulty = difficulty;
            e.lengthSeconds = length;
            e.modified = modified;
            e.lastOpened = opened;
            return e;
        };

        // c は一度も開いていない (保存した日時 = 5日 で代える)
        const std::vector<SessionStore::Entry> all {
            make ("a", "Beta",  "Zed", 3, 200.0, day (1), day (10)),
            make ("b", "alpha", "",    0, 0.0,   day (2), day (3)),
            make ("c", "Gamma", "Ann", 5, 100.0, day (5), juce::Time()),
        };

        const auto ids = [] (const std::vector<SessionStore::Entry>& list)
        {
            juce::StringArray result;
            for (const auto& e : list)
                result.add (e.id);
            return result.joinIntoString (",");
        };

        beginTest ("Default: last practiced, newest first (uses the save time when never opened)");
        {
            auto list = all;
            SessionSort::sort (list, SessionSort::fromString ({}), false);
            expectEquals (ids (list), juce::String ("a,c,b"));
            SessionSort::sort (list, SessionSort::Key::lastPracticed, true);
            expectEquals (ids (list), juce::String ("b,c,a"));
        }

        beginTest ("Text columns ignore case; empty values go last in both directions");
        {
            auto list = all;
            SessionSort::sort (list, SessionSort::Key::title, true);
            expectEquals (ids (list), juce::String ("b,a,c"));   // alpha, Beta, Gamma

            SessionSort::sort (list, SessionSort::Key::artist, true);
            expectEquals (ids (list), juce::String ("c,a,b"));   // Ann, Zed, (空)
            SessionSort::sort (list, SessionSort::Key::artist, false);
            expectEquals (ids (list), juce::String ("a,c,b"));   // Zed, Ann, (空)
        }

        beginTest ("Number columns: unset difficulty and unknown length go last");
        {
            auto list = all;
            SessionSort::sort (list, SessionSort::Key::difficulty, false);
            expectEquals (ids (list), juce::String ("c,a,b"));
            SessionSort::sort (list, SessionSort::Key::length, true);
            expectEquals (ids (list), juce::String ("c,a,b"));
        }

        beginTest ("Key names round-trip; first click directions");
        {
            for (auto key : { SessionSort::Key::title, SessionSort::Key::artist, SessionSort::Key::difficulty,
                              SessionSort::Key::tuning, SessionSort::Key::length, SessionSort::Key::lastPracticed })
                expect (SessionSort::fromString (SessionSort::toString (key)) == key);

            expect (SessionSort::defaultAscending (SessionSort::Key::title));
            expect (! SessionSort::defaultAscending (SessionSort::Key::lastPracticed));
        }
    }
};

static SessionSortTests sessionSortTests;
