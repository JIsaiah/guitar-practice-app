#include "../Source/Model/ScoreParts.h"
#include "../Source/Model/Session.h"

// リード・バッキングの決め方と、保存・読み込み
class ScorePartsTests final : public juce::UnitTest
{
public:
    ScorePartsTests() : juce::UnitTest ("ScoreParts", "Score") {}

    void runTest() override
    {
        using ScoreParts::Track;

        beginTest ("Two guitar tracks: lead and backing");
        {
            const std::vector<Track> tracks { { "Vocals", false }, { "Lead", true }, { "Bass", false }, { "Rhythm", true } };
            const auto a = ScoreParts::assign (tracks);
            expectEquals (a.lead, 1);
            expectEquals (a.backing, 3);
            expect (! a.needsChoice);
        }

        beginTest ("Three or more guitar tracks: the author chooses");
        {
            const std::vector<Track> tracks { { "G1", true }, { "G2", true }, { "G3", true } };
            const auto a = ScoreParts::assign (tracks);
            expectEquals (a.lead, 0);
            expectEquals (a.backing, 1);
            expect (a.needsChoice);
            expectEquals ((int) ScoreParts::guitarTracks (tracks).size(), 3);
        }

        beginTest ("One guitar track: lead only. No guitar: the first track");
        {
            const auto one = ScoreParts::assign ({ { "Drums", false }, { "Guitar", true } });
            expectEquals (one.lead, 1);
            expectEquals (one.backing, -1);

            const auto none = ScoreParts::assign ({ { "Piano", false } });
            expectEquals (none.lead, 0);
            expectEquals (none.backing, -1);

            const auto empty = ScoreParts::assign ({});
            expectEquals (empty.lead, -1);
        }

        beginTest ("Display name");
        {
            const std::vector<Track> tracks { { "", true }, { "Lead", true } };
            expectEquals (ScoreParts::displayName (tracks, 1), juce::String ("2. Lead"));
            expect (ScoreParts::displayName (tracks, 0).startsWith ("1. "));
            expect (ScoreParts::displayName (tracks, 5).isEmpty());
        }

        beginTest ("Parts and tone slots are saved and loaded");
        {
            Session s;
            s.leadTrack = 2;
            s.backingTrack = 4;
            s.backingActive = true;
            s.leadPreset = 1;
            s.backingPreset = 3;
            s.toneSlots[0].tonePresetId = "nam";
            s.toneSlots[0].namModelPath = "C:/amp.nam";
            s.toneSlots[0].inputGainDb = -3.5;
            s.toneSlots[2].tonePresetId = "tone3000";
            s.toneSlots[2].pluginPath = "C:/plug.vst3";
            s.toneSlots[2].pluginState = "AAAA";

            const auto loaded = Session::fromVar (juce::JSON::parse (juce::JSON::toString (s.toVar())));
            expectEquals (loaded.leadTrack, 2);
            expectEquals (loaded.backingTrack, 4);
            expect (loaded.backingActive);
            expectEquals (loaded.leadPreset, 1);
            expectEquals (loaded.backingPreset, 3);
            expectEquals (loaded.toneSlots[0].namModelPath, juce::String ("C:/amp.nam"));
            expectEquals (loaded.toneSlots[0].inputGainDb, -3.5);
            expect (loaded.toneSlots[1].isEmpty());
            expectEquals (loaded.toneSlots[2].pluginState, juce::String ("AAAA"));
        }

        beginTest ("Old files: parts are not decided yet");
        {
            const auto old = Session::fromVar (juce::JSON::parse (R"({ "score": { "path": "a.gp", "track_index": 1 } })"));
            expectEquals (old.leadTrack, -1);
            expectEquals (old.backingTrack, -1);
            expect (! old.backingActive);
            expect (old.toneSlots[0].isEmpty());
        }

        beginTest ("Mixer mutes are saved and loaded (old files: not muted)");
        {
            Session s;
            s.mixSource = 0.7;
            s.muteSource = true;
            s.muteBackingTab = true;

            const auto loaded = Session::fromVar (juce::JSON::parse (juce::JSON::toString (s.toVar())));
            expectEquals (loaded.mixSource, 0.7);
            expect (loaded.muteSource);
            expect (! loaded.muteGuitar);
            expect (! loaded.muteLeadTab);
            expect (loaded.muteBackingTab);

            const auto old = Session::fromVar (juce::JSON::parse (R"({ "mixer": { "source": 0.5 } })"));
            expect (! old.muteSource && ! old.muteGuitar && ! old.muteLeadTab && ! old.muteBackingTab);
        }
    }
};

static ScorePartsTests scorePartsTests;
