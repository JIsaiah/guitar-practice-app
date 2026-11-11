#include "Session.h"

namespace
{
    juce::String toString (SourceType type)
    {
        switch (type)
        {
            case SourceType::local:   return "local";
            case SourceType::youtube: return "youtube";
            case SourceType::none:    break;
        }
        return "none";
    }

    SourceType sourceTypeFromString (const juce::String& text)
    {
        if (text == "local")   return SourceType::local;
        if (text == "youtube") return SourceType::youtube;
        return SourceType::none;
    }

    juce::String toString (TempoOrigin origin)
    {
        switch (origin)
        {
            case TempoOrigin::score:  return "score";
            case TempoOrigin::online: return "online";
            case TempoOrigin::tap:    return "tap";
            case TempoOrigin::manual: break;
        }
        return "manual";
    }

    TempoOrigin tempoOriginFromString (const juce::String& text)
    {
        if (text == "score")  return TempoOrigin::score;
        if (text == "online") return TempoOrigin::online;
        if (text == "tap")    return TempoOrigin::tap;
        return TempoOrigin::manual;
    }

    juce::var makeObject (std::initializer_list<std::pair<juce::Identifier, juce::var>> properties)
    {
        auto* object = new juce::DynamicObject();

        for (const auto& [key, value] : properties)
            object->setProperty (key, value);

        return juce::var (object);
    }

    juce::var emptyArray()
    {
        return juce::var (juce::Array<juce::var>());
    }

    template <typename T>
    T getOr (const juce::var& value, T fallback)
    {
        if (value.isVoid())
            return fallback;

        if constexpr (std::is_same_v<T, juce::String>)
            return value.toString();
        else
            return static_cast<T> (value);
    }
}

juce::var Session::toVar() const
{
    auto source = sourceType == SourceType::youtube
                    ? makeObject ({ { "type", toString (sourceType) }, { "url", youtubeUrl }, { "length_sec", lengthSeconds } })
                    : makeObject ({ { "type", toString (sourceType) }, { "path", sourcePath }, { "length_sec", lengthSeconds } });

    return makeObject ({
        { "version",    currentVersion },
        { "session_id", sessionId },
        { "title",      title },
        { "artist",     artist },
        { "tuning",     tuning },
        { "difficulty", difficulty },
        { "folder",     folder },
        { "last_opened", lastOpened.toMilliseconds() > 0 ? lastOpened.toISO8601 (true) : juce::String() },
        { "source",   source },
        { "score",    makeObject ({ { "path", scorePath }, { "track_index", scoreTrackIndex },
                                    // リード・バッキング
                                    { "lead_track", leadTrack }, { "backing_track", backingTrack },
                                    { "active", backingActive ? "backing" : "lead" },
                                    { "lead_preset", leadPreset }, { "backing_preset", backingPreset } }) },
        { "tone_slots", [this]
            {
                // 音色のプリセット 1・2・3 (空きは null)
                juce::Array<juce::var> slots;
                for (const auto& slot : toneSlots)
                    slots.add (slot.isEmpty() ? juce::var()
                                              : makeObject ({ { "preset_id", slot.tonePresetId }, { "nam_model", slot.namModelPath },
                                                              { "ir", slot.irPath }, { "input_gain_db", slot.inputGainDb },
                                                              { "plugin_path", slot.pluginPath }, { "plugin_state", slot.pluginState } }));
                return juce::var (slots);
            }() },
        { "tempo",    makeObject ({ { "bpm", bpm }, { "origin", toString (tempoOrigin) } }) },
        { "playback", makeObject ({ { "speed_percent", speedPercent } }) },
        { "sync",     makeObject ({ { "offset_ms", syncOffsetMs }, { "score_offset_ms", scoreOffsetMs },
                                    { "metronome_start_ms", metronomeStartMs }, { "points", emptyArray() } }) },
        { "layout",   makeObject ({ { "video_in_main_area", videoInMainArea } }) },
        { "loop",     makeObject ({ { "a_ms", loopStartMs }, { "b_ms", loopEndMs }, { "enabled", loopEnabled } }) },
        { "mixer",    makeObject ({ { "source", mixSource }, { "guitar", mixGuitar }, { "metronome", mixMetronome },
                                    { "lead_tab", mixLeadTab }, { "backing_tab", mixBackingTab },
                                    { "source_muted", muteSource }, { "guitar_muted", muteGuitar },
                                    { "lead_tab_muted", muteLeadTab }, { "backing_tab_muted", muteBackingTab } }) },
        { "tone",     makeObject ({ { "type", tonePresetId == "nam" ? "nam" : tonePresetId == "tone3000" ? "vst3" : "builtin" },
                                    { "preset_id", tonePresetId },
                                    { "nam_model", namModelPath },
                                    { "ir", irPath },
                                    { "input_gain_db", inputGainDb },
                                    // 設計書 6章の "vst3": プラグインのファイルと状態 (今は1つだけ)
                                    { "vst3", pluginPath.isEmpty() ? emptyArray()
                                                                   : juce::var (juce::Array<juce::var> {
                                                                         makeObject ({ { "path", pluginPath },
                                                                                       { "state", pluginState } }) }) } }) },
    });
}

Session Session::fromVar (const juce::var& value)
{
    Session s;

    s.sessionId = getOr<juce::String> (value["session_id"], {});
    s.title = getOr<juce::String> (value["title"], {});
    s.artist = getOr<juce::String> (value["artist"], {});
    s.tuning = getOr<juce::String> (value["tuning"], {});
    s.difficulty = juce::jlimit (0, maxDifficulty, getOr<int> (value["difficulty"], 0));
    s.folder = getOr<juce::String> (value["folder"], {});
    if (const auto opened = getOr<juce::String> (value["last_opened"], {}); opened.isNotEmpty())
        s.lastOpened = juce::Time::fromISO8601 (opened);

    const auto& source = value["source"];
    s.sourceType = sourceTypeFromString (getOr<juce::String> (source["type"], "none"));
    s.sourcePath = getOr<juce::String> (source["path"], {});
    s.youtubeUrl = getOr<juce::String> (source["url"], {});
    s.lengthSeconds = getOr<double> (source["length_sec"], 0.0);

    const auto& score = value["score"];
    s.scorePath = getOr<juce::String> (score["path"], {});
    s.scoreTrackIndex = getOr<int> (score["track_index"], 0);

    const auto& tempo = value["tempo"];
    s.bpm = getOr<double> (tempo["bpm"], 120.0);
    s.tempoOrigin = tempoOriginFromString (getOr<juce::String> (tempo["origin"], "manual"));

    s.speedPercent = getOr<int> (value["playback"]["speed_percent"], 100);
    s.syncOffsetMs = getOr<int> (value["sync"]["offset_ms"], 0);
    s.scoreOffsetMs = getOr<int> (value["sync"]["score_offset_ms"], 0);
    s.metronomeStartMs = juce::jmax (0, getOr<int> (value["sync"]["metronome_start_ms"], 0));
    s.videoInMainArea = getOr<bool> (value["layout"]["video_in_main_area"], false);

    const auto& loop = value["loop"];
    s.loopStartMs = getOr<int> (loop["a_ms"], 0);
    s.loopEndMs = getOr<int> (loop["b_ms"], 0);
    s.loopEnabled = getOr<bool> (loop["enabled"], false);

    const auto& mixer = value["mixer"];
    s.mixSource = getOr<double> (mixer["source"], 1.0);
    s.mixGuitar = getOr<double> (mixer["guitar"], 1.0);
    s.mixMetronome = getOr<double> (mixer["metronome"], 0.8);
    s.mixLeadTab = getOr<double> (mixer["lead_tab"], 1.0);
    s.mixBackingTab = getOr<double> (mixer["backing_tab"], 1.0);
    s.muteSource = getOr<bool> (mixer["source_muted"], false);
    s.muteGuitar = getOr<bool> (mixer["guitar_muted"], false);
    s.muteLeadTab = getOr<bool> (mixer["lead_tab_muted"], false);
    s.muteBackingTab = getOr<bool> (mixer["backing_tab_muted"], false);

    const auto& tone = value["tone"];
    s.tonePresetId = getOr<juce::String> (tone["preset_id"], "clean-01");
    s.namModelPath = getOr<juce::String> (tone["nam_model"], {});
    s.irPath = getOr<juce::String> (tone["ir"], {});
    s.inputGainDb = getOr<double> (tone["input_gain_db"], 0.0);

    // リード・バッキング (無い古いファイルは -1 = これから決める)
    const auto& parts = value["score"];
    s.leadTrack = getOr<int> (parts["lead_track"], -1);
    s.backingTrack = getOr<int> (parts["backing_track"], -1);
    s.backingActive = getOr<juce::String> (parts["active"], "lead") == "backing";
    s.leadPreset = juce::jlimit (0, numToneSlots, getOr<int> (parts["lead_preset"], 0));
    s.backingPreset = juce::jlimit (0, numToneSlots, getOr<int> (parts["backing_preset"], 0));

    if (const auto* slots = value["tone_slots"].getArray())
        for (int i = 0; i < juce::jmin (numToneSlots, slots->size()); ++i)
        {
            const auto& slot = slots->getReference (i);
            if (! slot.isObject())
                continue;

            auto& t = s.toneSlots[(size_t) i];
            t.tonePresetId = getOr<juce::String> (slot["preset_id"], {});
            t.namModelPath = getOr<juce::String> (slot["nam_model"], {});
            t.irPath = getOr<juce::String> (slot["ir"], {});
            t.inputGainDb = getOr<double> (slot["input_gain_db"], 0.0);
            t.pluginPath = getOr<juce::String> (slot["plugin_path"], {});
            t.pluginState = getOr<juce::String> (slot["plugin_state"], {});
        }

    if (const auto* plugins = tone["vst3"].getArray(); plugins != nullptr && ! plugins->isEmpty())
    {
        const auto& plugin = plugins->getReference (0);
        s.pluginPath = getOr<juce::String> (plugin["path"], {});
        s.pluginState = getOr<juce::String> (plugin["state"], {});
    }

    return s;
}
