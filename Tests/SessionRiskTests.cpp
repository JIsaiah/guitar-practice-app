#include <juce_core/juce_core.h>
#include "../Source/Library/SessionRisk.h"
#include "../Source/Web/ScoreFileData.h"

// セキュリティ: 人から受け取ったセッションの危ないもの (SessionRisk.h)、読み込んだ値の範囲 (Session::limitValues)、
// 大きすぎるタブ譜のファイル (ScoreFileData)
class SessionRiskTests final : public juce::UnitTest
{
public:
    SessionRiskTests() : juce::UnitTest ("SessionRisk", "Library") {}

    void runTest() override
    {
        using Kind = SessionRisk::Item::Kind;
        const juce::Array<juce::File> standard { juce::File ("C:\\Program Files\\Common Files\\VST3") };

        const auto countOf = [] (const std::vector<SessionRisk::Item>& items, Kind kind)
        {
            return (int) std::count_if (items.begin(), items.end(), [kind] (const SessionRisk::Item& i) { return i.kind == kind; });
        };

        beginTest ("A plugin in the standard folder is fine; elsewhere or on the network it is a risk");
        {
            Session s;
            s.pluginPath = "C:\\Program Files\\Common Files\\VST3\\TONE3000.vst3";
            expect (SessionRisk::collect (s, standard).empty());

            s.toneSlots[0].pluginPath = "C:\\Users\\x\\Downloads\\amp.vst3";
            s.toneSlots[1].pluginPath = "\\\\attacker\\share\\evil.vst3";
            s.toneSlots[2].pluginPath = "C:\\Program Files\\Common Files\\VST3\\..\\..\\..\\Users\\x\\evil.vst3";   // 中に見せかけた外
            expectEquals (countOf (SessionRisk::collect (s, standard), Kind::plugin), 3);
        }

        beginTest ("Network files are risks; local files are not");
        {
            Session s;
            s.sourceType = SourceType::local;
            s.sourcePath = "\\\\nas\\music\\song.mp3";
            s.scorePath = "C:\\Tabs\\song.gp";
            s.namModelPath = "//attacker/x/amp.nam";
            s.toneSlots[1].irPath = "\\\\attacker@SSL@443\\x\\cab.wav";   // WebDAV の形
            s.toneSlots[2].namModelPath = "//attacker/x/amp.nam";         // 同じものは1回だけ
            expectEquals (countOf (SessionRisk::collect (s, standard), Kind::networkFile), 3);

            s.sourceType = SourceType::youtube;   // YouTube のときは、音源のファイルの場所は使わない
            expectEquals (countOf (SessionRisk::collect (s, standard), Kind::networkFile), 2);
        }

        beginTest ("Plugin settings are a risk, even for a plugin in the standard folder");
        {
            Session s;
            s.pluginPath = "C:\\Program Files\\Common Files\\VST3\\TONE3000.vst3";
            s.toneSlots[2].pluginState = "AAAA";
            const auto items = SessionRisk::collect (s, standard);
            expectEquals ((int) items.size(), 1);
            expect (items[0].kind == Kind::pluginState);
            expect (SessionRisk::describe (items).isNotEmpty());
        }

        beginTest ("Opening safely removes every risk and keeps the rest");
        {
            Session s;
            s.title = "Song";
            s.pluginPath = "\\\\attacker\\share\\evil.vst3";
            s.pluginState = "AAAA";
            s.toneSlots[0].pluginPath = "C:\\Program Files\\Common Files\\VST3\\TONE3000.vst3";
            s.toneSlots[0].pluginState = "BBBB";
            s.toneSlots[1].namModelPath = "\\\\attacker\\x\\amp.nam";
            s.sourceType = SourceType::local;
            s.sourcePath = "\\\\nas\\song.mp3";
            s.scorePath = "C:\\Tabs\\song.gp";

            SessionRisk::makeSafe (s, standard);
            expect (SessionRisk::collect (s, standard).empty());
            expect (s.pluginPath.isEmpty() && s.pluginState.isEmpty());
            expectEquals (s.toneSlots[0].pluginPath, juce::String ("C:\\Program Files\\Common Files\\VST3\\TONE3000.vst3"));   // 標準の置き場所は残す
            expect (s.toneSlots[0].pluginState.isEmpty());   // 設定は外す (初めの状態で開く)
            expect (s.sourceType == SourceType::none);
            expectEquals (s.scorePath, juce::String ("C:\\Tabs\\song.gp"));
            expectEquals (s.title, juce::String ("Song"));
        }

        beginTest ("Values read from a session are kept in the range of the controls");
        {
            Session s;
            s.mixGuitar = 1000.0;
            s.mixSource = 5.0;
            s.mixMetronome = -3.0;
            s.inputGainDb = 200.0;
            s.toneSlots[1].tonePresetId = "clean-01";   // 空のプリセットは保存されないので、音色を入れておく
            s.toneSlots[1].inputGainDb = 1.0e300;
            s.noiseGateDb = 50.0;
            s.speedPercent = 100000;
            s.bpm = 0.0;
            auto v = s.toVar();
            if (auto* tone = v["tone"].getDynamicObject())
                tone->setProperty ("input_gain_db", std::numeric_limits<double>::infinity());

            const auto loaded = Session::fromVar (v);
            expectEquals (loaded.mixGuitar, 1.5);
            expectEquals (loaded.mixSource, 1.0);
            expectEquals (loaded.mixMetronome, 0.0);
            expectEquals (loaded.inputGainDb, 0.0);   // 数でない (無限大) は初めの値
            expectEquals (loaded.toneSlots[1].inputGainDb, Session::maxInputGainDb);
            expectEquals (loaded.noiseGateDb, Session::maxNoiseGateDb);
            expectEquals (loaded.speedPercent, 200);
            expectEquals (loaded.bpm, 20.0);
        }

        beginTest ("Too many tone changes are not read");
        {
            juce::Array<juce::var> many;
            for (int i = 0; i < 2000; ++i)
                many.add (juce::Array<juce::var> { i * 10, 1 });
            auto v = Session().toVar();
            v.getDynamicObject()->setProperty ("tone_changes", many);
            expect ((int) Session::fromVar (v).toneChanges.size() <= Session::maxListItems);
        }

        beginTest ("A zip that unpacks to a huge score is not read (zip bomb)");
        {
            const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("SessionRiskTests");
            dir.createDirectory();
            const auto bomb = dir.getChildFile ("bomb.gp");

            {
                juce::MemoryBlock zeros ((size_t) ScoreFileData::maxUnpackedScoreBytes + 1024, true);   // 0 ばかりなので、とても小さく縮む
                juce::ZipFile::Builder builder;
                builder.addEntry (std::make_unique<juce::MemoryInputStream> (zeros, false), 9, "Content/score.gpif", {});
                bomb.deleteFile();
                juce::FileOutputStream out (bomb);
                expect (builder.writeToStream (out, nullptr));
            }
            expect (bomb.getSize() < 1024 * 1024);   // ファイルは小さい
            expect (! ScoreFileData::isWithinLimits (bomb));
            juce::MemoryBlock data;
            expect (! ScoreFileData::load (bomb, data));

            const auto small = dir.getChildFile ("small.gp");
            {
                juce::MemoryBlock text ("<GPIF></GPIF>", 13);
                juce::ZipFile::Builder builder;
                builder.addEntry (std::make_unique<juce::MemoryInputStream> (text, false), 6, "Content/score.gpif", {});
                small.deleteFile();
                juce::FileOutputStream out (small);
                expect (builder.writeToStream (out, nullptr));
            }
            expect (ScoreFileData::isWithinLimits (small));
            dir.deleteRecursively();
        }
    }
};

static SessionRiskTests sessionRiskTests;
