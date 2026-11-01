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
    int syncOffsetMs = 0;    // 拍の位置: メトロノームとタブ譜を、音源に対してずらす
    int scoreOffsetMs = 0;   // タブ譜だけをずらす (タブ譜の再生開始の位置合わせ)

    bool videoInMainArea = false;   // タブ譜と動画を入れ替えて、動画を大きく表示する (タブ譜つきの動画で練習するとき)

    int loopStartMs = 0;
    int loopEndMs = 0;
    bool loopEnabled = false;

    double mixSource = 1.0;
    double mixGuitar = 1.0;
    double mixMetronome = 0.8;

    juce::String tonePresetId = "clean-01";   // 簡易な音色の ID か、"nam"
    juce::String namModelPath;                 // NAM のアンプのモデル (.nam)
    juce::String irPath;                       // キャビネットの IR (.wav)
    double inputGainDb = 0.0;                  // ギターの入力の大きさ (dB)

    juce::var toVar() const;
    static Session fromVar (const juce::var& value);
};
