#pragma once

#include <array>
#include <vector>
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
    juce::Time created;      // 作った日時 (ファイル名に入れる)。初めて保存したときに決まる。古いファイルは 0

    SourceType sourceType = SourceType::none;
    juce::String sourcePath;  // local のとき
    juce::String youtubeUrl;  // youtube のとき
    double lengthSeconds = 0.0;  // 音源の長さ。練習画面で分かったときに記録する (0 = 不明)。セットリストの合計時間に使う

    juce::String scorePath;
    int scoreTrackIndex = 0;   // 今表示しているトラック (リード・バッキングのどちらか)
    // PDF のタブ譜: 流れる速さ (%)。100% = 画面1つ分の高さを 12 小節 (4/4 とみなす) で流す (前の「1画面 12 小節」と同じ)。
    // 作者の指示: 小節の数より % のほうが想像しやすい。200% で2倍の速さ
    int pdfSpeedPercent = 100;
    static constexpr int minPdfSpeedPercent = 10, maxPdfSpeedPercent = 400;
    static constexpr int pdfBarsPerScreenAt100 = 12;
    juce::String pdfBackingPath;   // PDF のタブ譜の2枚目 (バッキング)。scorePath の PDF がリード。空 = なし
    bool isPdfScore() const { return scorePath.endsWithIgnoreCase (".pdf"); }
    bool hasPdfBacking() const { return isPdfScore() && pdfBackingPath.isNotEmpty(); }

    // PDF の繰り返し (作者が右クリックで置く)。位置は「ページの番号 (0 から) + ページの中の割合」。
    // count は弾く回数 (2 = 2回弾く)。リード (1つ目の PDF) とバッキング (2つ目) で別々に持つ
    struct PdfRepeat { double start = 0.0, end = 0.0; int count = 2; };
    static constexpr int maxPdfRepeatCount = 8;
    std::vector<PdfRepeat> pdfRepeats, pdfBackingRepeats;
    std::vector<PdfRepeat>& currentPdfRepeats() { return hasPdfBacking() && backingActive ? pdfBackingRepeats : pdfRepeats; }
    // 繰り返しを足す。重なる繰り返しは消し、始めの順に並べる。始めが終わりより後なら何もしない (false)
    static bool addPdfRepeat (std::vector<PdfRepeat>& repeats, PdfRepeat repeat);
    // 今表示するタブ譜のファイル (PDF でバッキングを表示しているときは2枚目)
    juce::String currentScorePath() const { return hasPdfBacking() && backingActive ? pdfBackingPath : scorePath; }
    // 「タブ譜を選択」で選んだファイルを入れる (繰り返しは消す)。PDF を2つ選んだら、名前の順で1つ目をリード・2つ目をバッキングにする
    // (作者の指示: PDF は2枚まで)。それ以外は1つ目だけを使う。PDF を2つ入れたら true
    bool setScoreFiles (juce::Array<juce::File> files);

    // リード・バッキング (タブ譜のトラックの番号。-1 = 決めていない・なし)
    int leadTrack = -1, backingTrack = -1;
    bool backingActive = false;          // バッキングを表示している
    int leadPreset = 0, backingPreset = 0;   // それぞれが使う音色のプリセット (1〜3。0 = 割り当てなし)

    static constexpr int numToneSlots = 3;
    std::array<ToneSlot, numToneSlots> toneSlots;   // 音色のプリセット 1・2・3

    // 音色の自動切り替え (作者の指示): 再生がこの位置 (音源の時刻) に来たら、このプリセットの音色にする。位置の順に並ぶ
    struct ToneChange { int ms = 0; int preset = 1; };
    std::vector<ToneChange> toneChanges;
    // 切り替えを足す。同じ位置 (前後 0.2 秒) にあれば置き換える。位置の順に並べる
    static void addToneChange (std::vector<ToneChange>& changes, ToneChange change);
    // position (ms) で効いている切り替え (その位置より前で、いちばん後ろのもの)。無ければ -1
    static int toneChangeAt (const std::vector<ToneChange>& changes, int positionMs);

    double bpm = 120.0;
    TempoOrigin tempoOrigin = TempoOrigin::manual;
    // メトロノームの拍子 (作者の指示: .gp が無いときに手で決める。例: 8/9 → 1小節 8 拍、1拍目を強く)。
    // .gp のタブ譜があるときは、タブ譜の拍子を使う
    int meterNumerator = 4, meterDenominator = 4;
    static constexpr int maxMeterNumerator = 32, maxMeterDenominator = 32;

    int speedPercent = 100;
    int syncOffsetMs = 0;    // 拍の位置: メトロノームとタブ譜を、音源に対してずらす
    int scoreOffsetMs = 0;   // タブ譜だけをずらす (タブ譜の再生開始の位置合わせ)
    int metronomeStartMs = 0;   // 動画モードで、メトロノームを始める位置 (「ここから開始」)。0 = 指定なし
    int scoreStartMs = 0;       // タブ譜の1小節目を合わせる、音源の位置 (「ここから流す」)。0 = 音源の頭から
    // 音源の開始・終了位置 (作者の指示: この間だけ再生する)。0 = 指定なし (頭から・最後まで)。
    // セットリストから開いたときは、エントリの専用の値を使う (PlaybackSettingsResolver)
    int sourceStartMs = 0, sourceEndMs = 0;

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

    // 読み込んだ値を、画面で選べる範囲に収める (fromVar が呼ぶ。人から受け取ったセッションの、範囲の外の値を防ぐ)
    void limitValues();
    static constexpr double minInputGainDb = -24.0, maxInputGainDb = 24.0;    // 「音色の設定」の入力のスライダーと同じ
    static constexpr double minNoiseGateDb = -90.0, maxNoiseGateDb = -20.0;   // ノイズゲートのスライダーと同じ (-90 = オフ)
    static constexpr int maxOffsetMs = 600000;   // ずらしの上限 (10 分)
    static constexpr int maxListItems = 500;     // 音色の切り替え・PDF の繰り返しの数の上限

    juce::var toVar() const;
    static Session fromVar (const juce::var& value);
};
