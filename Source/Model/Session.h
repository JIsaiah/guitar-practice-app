#pragma once

#include <juce_core/juce_core.h>

enum class SourceType
{
    none,
    local,
    youtube
};

// BPM の出どころ (設計書 #11)
enum class TempoOrigin
{
    score,
    online,
    tap,
    manual
};

// 1曲分の練習状態。保存・復元の単位 (設計書 6章)
struct Session
{
    static constexpr int currentVersion = 2;

    juce::String id;  // 保存ファイル名。未保存なら空
    juce::String title;

    SourceType sourceType = SourceType::none;
    juce::String sourcePath;  // local のとき
    juce::String youtubeUrl;  // youtube のとき

    juce::String scorePath;
    int scoreTrackIndex = 0;

    double bpm = 120.0;
    TempoOrigin tempoOrigin = TempoOrigin::manual;

    int speedPercent = 100;
    int syncOffsetMs = 0;

    int loopStartMs = 0;
    int loopEndMs = 0;
    bool loopEnabled = false;

    double mixSource = 1.0;
    double mixGuitar = 1.0;
    double mixMetronome = 0.8;

    juce::String tonePresetId = "clean-01";

    juce::var toVar() const;
    static Session fromVar (const juce::var& value);
};
