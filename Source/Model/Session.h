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
    static constexpr int currentVersion = 3;   // 3: session_id を追加 (setlist-mode.md 3.1)

    juce::String id;         // 保存ファイル名。未保存なら空
    juce::String sessionId;  // ファイルの中に持つ ID (UUID)。セットリストはこれで曲を指す。ファイルを移動・改名しても変わらない
    juce::String title;

    // 曲の詳細 (一覧に表示する。一覧の右クリック「詳細を編集…」で変えられる)
    juce::String artist;
    juce::String tuning;     // 例: "E Standard"、"Drop D"、"D A D G A D"。タブ譜から自動で入ることがある
    int difficulty = 0;      // 難易度 (星の数 0〜5。0 = 未設定)。一覧の星をクリックして決める
    static constexpr int maxDifficulty = 5;
    juce::String folder;     // 一覧のフォルダの名前 (1段だけ)。空ならフォルダなし

    SourceType sourceType = SourceType::none;
    juce::String sourcePath;  // local のとき
    juce::String youtubeUrl;  // youtube のとき
    double lengthSeconds = 0.0;  // 音源の長さ。練習画面で分かったときに記録する (0 = 不明)。セットリストの合計時間に使う

    juce::String scorePath;
    int scoreTrackIndex = 0;

    double bpm = 120.0;
    TempoOrigin tempoOrigin = TempoOrigin::manual;

    int speedPercent = 100;
    int syncOffsetMs = 0;    // 拍の位置: メトロノームとタブ譜を、音源に対してずらす
    int scoreOffsetMs = 0;   // タブ譜だけをずらす (タブ譜の再生開始の位置合わせ)
    int metronomeStartMs = 0;   // 動画モードで、メトロノームを始める位置 (「ここから開始」)。0 = 指定なし

    bool videoInMainArea = false;   // タブ譜と動画を入れ替えて、動画を大きく表示する (タブ譜つきの動画で練習するとき)

    int loopStartMs = 0;
    int loopEndMs = 0;
    bool loopEnabled = false;

    double mixSource = 1.0;
    double mixGuitar = 1.0;
    double mixMetronome = 0.8;

    juce::String tonePresetId = "clean-01";   // 簡易な音色の ID か、"nam"、"tone3000"
    juce::String pluginPath;                   // VST3 プラグイン (TONE3000) の .vst3
    juce::String pluginState;                  // プラグインの状態 (選んだ音色・つまみ)。Base64
    juce::String namModelPath;                 // NAM のアンプのモデル (.nam)
    juce::String irPath;                       // キャビネットの IR (.wav)
    double inputGainDb = 0.0;                  // ギターの入力の大きさ (dB)

    juce::var toVar() const;
    static Session fromVar (const juce::var& value);
};
