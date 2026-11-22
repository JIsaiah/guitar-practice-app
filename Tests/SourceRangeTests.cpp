#include <juce_core/juce_core.h>
#include "../Source/Session/PlaybackSettingsResolver.h"

// 音源の開始・終了位置と、メトロノームの拍子 (手で決めたもの) の保存。
// 開始・終了位置は、セッションとセットリストの曲で別々に覚える (作者の指示)
class SourceRangeTests final : public juce::UnitTest
{
public:
    SourceRangeTests() : juce::UnitTest ("SourceRange", "Model") {}

    void runTest() override
    {
        beginTest ("Start/end positions and the meter are saved and read back");
        {
            Session s;
            s.sourceStartMs = 12345;
            s.sourceEndMs = 200000;
            s.meterNumerator = 8;
            s.meterDenominator = 9;
            const auto loaded = Session::fromVar (s.toVar());
            expectEquals (loaded.sourceStartMs, 12345);
            expectEquals (loaded.sourceEndMs, 200000);
            expectEquals (loaded.meterNumerator, 8);
            expectEquals (loaded.meterDenominator, 9);

            // 前からのファイル (書いていない): 指定なし・4/4
            const auto old = Session::fromVar (juce::JSON::parse ("{\"title\":\"x\"}"));
            expectEquals (old.sourceStartMs, 0);
            expectEquals (old.sourceEndMs, 0);
            expectEquals (old.meterNumerator, 4);
            expectEquals (old.meterDenominator, 4);
        }

        beginTest ("Tone changes: sorted, replaced at the same place, saved, and found by position");
        {
            Session s;
            Session::addToneChange (s.toneChanges, { 30000, 2 });
            Session::addToneChange (s.toneChanges, { 10000, 1 });
            Session::addToneChange (s.toneChanges, { 30100, 3 });   // 0.2 秒以内: 置き換え
            expectEquals ((int) s.toneChanges.size(), 2);
            expectEquals (s.toneChanges[0].ms, 10000);
            expectEquals (s.toneChanges[1].preset, 3);

            expectEquals (Session::toneChangeAt (s.toneChanges, 5000), -1);    // 最初より前
            expectEquals (Session::toneChangeAt (s.toneChanges, 10000), 0);
            expectEquals (Session::toneChangeAt (s.toneChanges, 29999), 0);
            expectEquals (Session::toneChangeAt (s.toneChanges, 60000), 1);

            const auto loaded = Session::fromVar (s.toVar());
            expectEquals ((int) loaded.toneChanges.size(), 2);
            expectEquals (loaded.toneChanges[1].ms, 30100);
            expectEquals (loaded.toneChanges[1].preset, 3);
        }

        beginTest ("A setlist entry keeps its own start/end, separate from the session");
        {
            Session stored;
            stored.sourceStartMs = 1000;
            stored.sourceEndMs = 5000;

            SetlistEntry::Overrides overrides;
            overrides.sourceStartSec = 2.0;
            overrides.sourceEndSec = 0.0;   // 終了位置は指定なし → セッションの終了位置に従う (作者の指示)

            PlaybackSettingsResolver resolver (overrides);
            auto working = resolver.makeWorkingCopy (stored);
            expectEquals (working.sourceStartMs, 2000);
            expectEquals (working.sourceEndMs, 5000);

            working.sourceEndMs = 9000;   // セットリストで弾いているときに変えた
            const auto split = resolver.split (working);
            expectEquals (split.session.sourceStartMs, 1000);   // セッションの値はそのまま
            expectEquals (split.session.sourceEndMs, 5000);
            expect (split.overrides.has_value());
            expectEquals (split.overrides->sourceStartSec, 2.0);
            expectEquals (split.overrides->sourceEndSec, 9.0);

            // 単体で開いたとき: セッションの値をそのまま読み書きする
            PlaybackSettingsResolver standalone;
            expectEquals (standalone.makeWorkingCopy (stored).sourceEndMs, 5000);
        }

        beginTest ("Setlist live date and finished flag");
        {
            expectEquals (Setlist::parseLiveDate ("2026/11/21"), juce::String ("2026-11-21"));
            expectEquals (Setlist::parseLiveDate ("2026-1-5"), juce::String ("2026-01-05"));
            expectEquals (Setlist::parseLiveDate (" 2024.02.29 "), juce::String ("2024-02-29"));   // うるう年
            expect (Setlist::parseLiveDate ("2025/02/29").isEmpty());
            expect (Setlist::parseLiveDate ("2026/13/01").isEmpty());
            expect (Setlist::parseLiveDate ("あした").isEmpty());
            expect (Setlist::parseLiveDate ("").isEmpty());
            expectEquals (Setlist::parseLiveDate ("11/21", 2027), juce::String ("2027-11-21"));   // 月/日だけ: 年はその年
            expectEquals (Setlist::parseLiveDate ("1/5").substring (4), juce::String ("-01-05"));     // 年を渡さなければ今年
            expect (Setlist::parseLiveDate ("2/30", 2026).isEmpty());
            expectEquals (Setlist::formatLiveDate ("2026-11-21"), juce::String ("2026/11/21"));

            Setlist s;
            s.setlistId = "id";
            s.liveDate = "2026-11-21";
            s.finished = true;
            const auto loaded = Setlist::fromVar (s.toVar());
            expect (loaded.has_value() && loaded->liveDate == "2026-11-21" && loaded->finished);

            Setlist old;   // 前からのファイル (書いていない): ライブの日なし・終わっていない
            old.setlistId = "old";
            auto v = old.toVar();
            v.getDynamicObject()->removeProperty ("liveDate");
            v.getDynamicObject()->removeProperty ("finished");
            const auto oldLoaded = Setlist::fromVar (v);
            expect (oldLoaded.has_value() && oldLoaded->liveDate.isEmpty() && ! oldLoaded->finished);
        }

        beginTest ("Preset names and part names are saved");
        {
            Session s;
            s.toneSlots[1].tonePresetId = "clean-01";
            s.toneSlots[1].name = "ソロ用";
            s.leadName = "ギター1";
            s.backingName = "ギター2";
            const auto loaded = Session::fromVar (s.toVar());
            expect (loaded.toneSlots[1].name == s.toneSlots[1].name);
            expect (loaded.leadName == s.leadName && loaded.backingName == s.backingName);

            auto v = s.toVar();   // 長すぎる名前 (手で書き換えられたファイル) は 16 字までにする
            v["score"].getDynamicObject()->setProperty ("lead_name", juce::String::repeatedString ("a", 40));
            expectEquals (Session::fromVar (v).leadName.length(), Session::maxPartNameLength);
        }

        beginTest ("Adding a session to a setlist copies its start/end");
        {
            Session source;
            source.sourceStartMs = 3000;
            source.sourceEndMs = 7000;
            Setlist setlist;
            setlist.setlistId = "list";
            setlist.name = "Live";
            setlist.addEntry ("id", &source);
            expectEquals (setlist.entries.back().overrides.sourceStartSec, 3.0);
            expectEquals (setlist.entries.back().overrides.sourceEndSec, 7.0);

            // セットリストのファイルにも保存される
            const auto loaded = Setlist::fromVar (setlist.toVar());
            expect (loaded.has_value());
            if (loaded.has_value())
            {
                expectEquals (loaded->entries.back().overrides.sourceStartSec, 3.0);
                expectEquals (loaded->entries.back().overrides.sourceEndSec, 7.0);
            }
        }
    }
};

static SourceRangeTests sourceRangeTests;
