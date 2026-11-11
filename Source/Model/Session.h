#pragma once

#include <array>
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

// 音色のプリセット (1・2・3) に保存した音色。リード・バッキングに違う音色を割り当てるのに使う (作者の指示)
struct ToneSlot
{
    juce::String tonePresetId;   // 空なら、まだ保存していない
    juce::String namModelPath, irPath, pluginPath, pluginState;
    double inputGainDb = 0.0;
    double noiseGateDb = -90.0;   // ノイズゲートのしきい値 (-90 = オフ)

    bool isEmpty() const noexcept { return tonePresetId.isEmpty(); }
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
    juce::Time lastOpened;   // 最後に練習画面で開いた日時 (一覧の「最後の練習日」)。開いたことが無ければ 0

    SourceType sourceType = SourceType::none;
    juce::String sourcePath;  // local のとき
    juce::String youtubeUrl;  // youtube のとき
    double lengthSeconds = 0.0;  // 音源の長さ。練習画面で分かったときに記録する (0 = 不明)。セットリストの合計時間に使う

    juce::String scorePath;
    int scoreTrackIndex = 0;   // 今表示しているトラック (リード・バッキングのどちらか)

    // リード・バッキング (タブ譜のトラックの番号。-1 = 決めていない・なし)
    int leadTrack = -1, backingTrack = -1;
    bool backingActive = false;          // バッキングを表示している
    int leadPreset = 0, backingPreset = 0;   // それぞれが使う音色のプリセット (1〜3。0 = 割り当てなし)

    static constexpr int numToneSlots = 3;
    std::array<ToneSlot, numToneSlots> toneSlots;   // 音色のプリセット 1・2・3

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
    double mixLeadTab = 1.0, mixBackingTab = 1.0;   // タブ譜の音 (MIDI) の音量: リード・バッキングのトラック (1.0 = 100%)
    // ミュート (作者の指示で、開き直しても残るように保存する)。ミュートしても音量の値は変えない
    // タブ譜の音 (リード・バッキング) は、はじめはミュート (作者の指示)
    bool muteSource = false, muteGuitar = false, muteLeadTab = true, muteBackingTab = true;

    juce::String tonePresetId = "clean-01";   // 簡易な音色の ID か、"nam"、"tone3000"
    juce::String pluginPath;                   // VST3 プラグイン (TONE3000) の .vst3
    juce::String pluginState;                  // プラグインの状態 (選んだ音色・つまみ)。Base64
    juce::String namModelPath;                 // NAM のアンプのモデル (.nam)
    juce::String irPath;                       // キャビネットの IR (.wav)
    double inputGainDb = 0.0;                  // ギターの入力の大きさ (dB)
    double noiseGateDb = -90.0;                // ノイズゲートのしきい値 (dB。-90 = オフ)

    juce::var toVar() const;
    static Session fromVar (const juce::var& value);
};
