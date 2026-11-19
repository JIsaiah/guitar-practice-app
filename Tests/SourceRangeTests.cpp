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

        beginTest ("A setlist entry keeps its own start/end, separate from the session");
        {
            Session stored;
            stored.sourceStartMs = 1000;
            stored.sourceEndMs = 5000;

            SetlistEntry::Overrides overrides;
            overrides.sourceStartSec = 2.0;
            overrides.sourceEndSec = 0.0;   // 終了位置は指定なし

            PlaybackSettingsResolver resolver (overrides);
            auto working = resolver.makeWorkingCopy (stored);
            expectEquals (working.sourceStartMs, 2000);
            expectEquals (working.sourceEndMs, 0);

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
