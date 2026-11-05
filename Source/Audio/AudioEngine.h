#pragma once

#include <atomic>
#include <memory>
#include <optional>
#include <vector>
#include <juce_audio_utils/juce_audio_utils.h>
#include "../Model/TonePresets.h"
#include "NamTone.h"
#include "PluginTone.h"

// 音声処理の中心 (設計書 5.1)
//
// スレッドの規約 (設計書 5.3):
//  - audioDeviceIOCallbackWithContext() の中では、メモリ確保・ロック・ファイル操作をしない
//  - UI スレッドとのやり取りは、アトミック変数とロックフリー FIFO (チューナー) だけで行う
//  - ファイルの読み込みは loaderPool のワーカースレッドで行い、完成したバッファをポインタの差し替えで渡す
//  - 不要になったバッファは、オーディオスレッドが使い終わったことを確認してから UI スレッドで解放する
class AudioEngine final : private juce::AudioIODeviceCallback,
                          private juce::Timer
{
public:
    AudioEngine();
    ~AudioEngine() override;

    //==== デバイス (メッセージスレッドから呼ぶ) ====
    void initialise (const juce::XmlElement* savedDeviceState);
    std::unique_ptr<juce::XmlElement> createDeviceState() const;
    juce::AudioDeviceManager& getDeviceManager() noexcept { return deviceManager; }
    double getDeviceSampleRate() const noexcept { return deviceSampleRate.load(); }

    //==== ローカル音源 (メッセージスレッドから呼ぶ) ====
    using LoadCallback = std::function<void (bool succeeded, const juce::String& error)>;
    void loadSourceAsync (const juce::File& file, LoadCallback onDone);
    void clearSource();
    bool hasSource() const noexcept { return ownedSource != nullptr; }
    double getSourceLengthSeconds() const noexcept;

    //==== 再生操作 ====
    void setPlaying (bool shouldPlay) noexcept { playing.store (shouldPlay); }
    bool isPlaying() const noexcept { return playing.load(); }
    void seekSeconds (double seconds) noexcept;
    double getPositionSeconds() const noexcept { return positionSeconds.load(); }

    // 仮実装: 再生レートを変えるため音程も変わる。Signalsmith Stretch に置き換える予定
    void setSpeed (double ratio) noexcept { speed.store (juce::jlimit (0.25, 2.0, ratio)); }
    void setLoop (double startSeconds, double endSeconds, bool enabled) noexcept;

    //==== メトロノーム ====
    void setMetronomeEnabled (bool enabled) noexcept;

    // メトロノームだけを鳴らす (音源を再生していないときに、自前の時計で鳴らす)。
    // 基本は音源に同期する。音源や YouTube を再生している間は、そちらの時計が優先される
    void setMetronomeSolo (bool solo) noexcept;
    bool isMetronomeSolo() const noexcept { return metronomeSolo.load(); }
    void setBpm (double bpm) noexcept { metronomeBpm.store (juce::jlimit (20.0, 400.0, bpm)); }
    void setSyncOffsetSeconds (double seconds) noexcept { syncOffsetSeconds.store (seconds); }

    // メトロノームが拍を数えている時計 (秒。拍の位置のずらしを引いたもの)。ビジュアライザー用。
    // 音源・YouTube・「単体で鳴らす」のどれかが動いているときだけ running が true
    struct BeatClock
    {
        double seconds = 0.0;
        bool running = false;
    };
    BeatClock getBeatClock() const noexcept { return { beatClockSeconds.load(), beatClockRunning.load() }; }

    // 確認用: メトロノームのクリックを鳴らした回数 (起動してから)。動作ログに書いて、鳴っていたかを確かめる
    int getClickCount() const noexcept { return clickCount.load(); }
    bool isMetronomeEnabled() const noexcept { return metronomeEnabled.load(); }
    double getSyncOffsetSeconds() const noexcept { return syncOffsetSeconds.load(); }
    double getBpm() const noexcept { return metronomeBpm.load(); }

    //==== 外部の時計 (YouTube。設計書 5.2) ====
    // YouTube の再生位置が届くたびに、メッセージスレッドから呼ぶ。
    // 再生中はメトロノームがこの時計に合わせる。オーディオスレッドは届いた値の間を自分で進め、
    // 小さなずれは少しずつ寄せる (いきなり飛ばすと、拍が二重に鳴ったり抜けたりするため)
    void setExternalClock (double positionSeconds, double rate, bool isPlaying) noexcept;
    void clearExternalClock() noexcept { externalActive.store (false); }

    //==== ミキサー (3系統) と音色 ====
    void setSourceGain (float gain) noexcept    { sourceGain.store (gain); }
    void setGuitarGain (float gain) noexcept    { guitarGain.store (gain); }
    // ギターの音を出すか。練習画面が開いている間だけ true にする (一覧や新規作成の画面では鳴らさない)
    void setGuitarMonitoring (bool on) noexcept { guitarMonitoring.store (on); }
    // ギターの総音量 (設定画面)。ミキサーの「ギター」に掛ける
    void setGuitarMasterGain (float gain) noexcept { guitarMasterGain.store (gain); }
    void setMetronomeGain (float gain) noexcept { metronomeGain.store (gain); }
    void setTone (ToneType type) noexcept       { tone.store ((int) type); }

    //==== NAM の音色 (メッセージスレッドから呼ぶ) ====
    // アンプのモデル (.nam) とキャビネットの IR (.wav) を、ワーカースレッドで読み込んでから差し替える。
    // どちらかが空でもよい。両方空なら、NAM の音色を外す
    void loadNamToneAsync (const juce::File& ampModel, const juce::File& cabinetIr, LoadCallback onDone);
    void clearNamTone();

    struct NamToneInfo
    {
        bool loading = false;
        bool active = false;              // 読み込めていて、今のデバイスのサンプルレートで動いている
        juce::String error;
        juce::File ampFile, cabinetFile;
        double modelSampleRate = 0.0;     // モデルを作ったときのサンプルレート (分からなければ 0)
    };
    NamToneInfo getNamToneInfo() const;

    //==== VST3 プラグインの音色 (TONE3000 など。メッセージスレッドから呼ぶ) ====
    // プラグインを非同期で作り、準備ができたら差し替える。vst3File が空なら外す。
    // 差し替える前に onAboutToReplace が呼ばれる: 画面は、そこで今のプラグインの編集画面を消すこと
    // (編集画面はプラグインより先に消す必要がある)
    //
    // プラグインは **同時に1つだけ** 持つ。新しいものを作る前に、今のものを外して消し終えてから作る。
    // Archetype Misha Mansoor X は、2つ目を作ったり、2つ目の画面を開いたりすると、1つ目が残っている間は落ちる
    // (クラッシュダンプと --plugin-window-test で確認)。
    // 同じファイルで、保存した状態があるときは、作り直さずに状態だけ戻す (セットリストで同じプラグインの曲が続くとき)
    void loadPluginToneAsync (const juce::File& vst3File, const juce::MemoryBlock& state, LoadCallback onDone);
    void clearPluginTone();
    PluginTone* getPluginTone() const noexcept { return ownedPluginTone.get(); }   // 編集画面を作るため
    bool isPluginToneLoading() const noexcept  { return pluginToneLoading; }
   #if JUCE_DEBUG
    // 開発用 (--metronome-check): オーディオデバイスを開かずに、オーディオの処理を1ブロック分だけ動かす
    void renderBlockForTest (float* const* outputs, int numOutputs, int numSamples, double sampleRate)
    {
        deviceSampleRate.store (sampleRate);
        audioDeviceIOCallbackWithContext (nullptr, 0, outputs, numOutputs, numSamples, {});
    }
   #endif
    int getNumRetired() const noexcept { return (int) retired.size(); }   // 確認用: まだ消していない古いもの (音源・NAM・プラグイン)
    std::function<void()> onPluginToneAboutToReplace;
    std::function<void()> onPluginToneChanged;   // 差し替えた後 (画面は編集画面を作り直す)

    // ギターの入力の大きさ (dB)。NAM のモデルは、入る音の大きさで歪み方が変わる
    void setInputGainDb (float db) noexcept { inputGain.store (juce::Decibels::decibelsToGain (db)); }

    // メーター用: 前回読んでから今までの、ギターの入力と出力の最大値 (0〜1)。読むと 0 に戻る
    float takeInputPeak() noexcept  { return inputPeak.exchange (0.0f); }
    float takeOutputPeak() noexcept { return guitarPeak.exchange (0.0f); }

    //==== デバイスの情報 (表示用) ====
    struct DeviceInfo
    {
        juce::String typeName, deviceName;
        double sampleRate = 0.0;
        int bufferSize = 0;
        double reportedLatencyMs = 0.0;   // 入力 + 出力の遅延 (ドライバが報告する値。設計書 1.4)
        double cpuUsage = 0.0;            // 0〜1
        bool hasInput = false;
    };
    DeviceInfo getDeviceInfo() const;

    //==== チューナー ====
    void setTunerActive (bool active) noexcept { tunerActive.store (active); }
    int readTunerInput (float* destination, int maxSamples) noexcept;  // UI スレッドから呼ぶ

private:
    struct LoadedAudio;

    // 差し替えて使わなくなったもの (音源・音色)。オーディオスレッドが使い終わってから解放する
    struct RetiredSource
    {
        std::shared_ptr<void> object;
        juce::uint64 callbackCountAtRetire;
        juce::uint32 keepUntilMs = 0;   // この時刻 (Time::getMillisecondCounter) までは消さない
        bool isPlugin = false;
    };

    // AudioIODeviceCallback
    void audioDeviceIOCallbackWithContext (const float* const* inputs, int numInputs,
                                           float* const* outputs, int numOutputs, int numSamples,
                                           const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceAboutToStart (juce::AudioIODevice* device) override;
    void audioDeviceStopped() override {}

    // Timer: 使い終わったバッファの解放
    void timerCallback() override;

    std::shared_ptr<LoadedAudio> readAudioFile (const juce::File& file, juce::String& error);
    void installSource (std::shared_ptr<LoadedAudio> newSource);
    void installNamTone (std::shared_ptr<NamTone> newTone);
    void installPluginTone (std::shared_ptr<PluginTone> newTone);
    void startPendingPluginLoad();    // 古いプラグインを消し終えていれば、待っている読み込みを始める
    bool hasRetiredPlugin() const;
    void updatePluginPreparation();   // デバイスの設定が変わったら、プラグインを準備し直す
    void processGuitar (const float* input, int numSamples, int toneType) noexcept;
    void pushTunerInput (const float* data, int numSamples) noexcept;
    void triggerClick (bool accent, double sampleRate) noexcept;
    float renderClick() noexcept;

    juce::AudioDeviceManager deviceManager;
    juce::AudioFormatManager formatManager;

    // メッセージスレッドだけが触る
    std::shared_ptr<LoadedAudio> ownedSource;
    std::vector<RetiredSource> retired;
    int loadGeneration = 0;

    std::shared_ptr<NamTone> ownedNamTone;

    juce::AudioPluginFormatManager pluginFormats;   // VST3 (メッセージスレッドだけが使う)
    std::shared_ptr<PluginTone> ownedPluginTone;
    int pluginToneGeneration = 0;
    bool pluginToneLoading = false;

    // 古いプラグインが消えるのを待っている読み込み
    struct PendingPluginLoad
    {
        juce::File file;
        juce::MemoryBlock state;
        LoadCallback onDone;
        int generation = 0;
    };
    std::optional<PendingPluginLoad> pendingPluginLoad;
    // 準備し直し: オーディオスレッドから外した時点のコールバックの回数。-1 なら準備し直していない
    juce::int64 pluginDetachedAtCallback = -1;
    juce::File namAmpFile, namCabinetFile;   // 選ばれているファイル (デバイスの設定が変わったら、読み込み直す)
    int namToneGeneration = 0;
    bool namToneLoading = false;
    juce::String namToneError;

    // スレッド間で共有する値
    std::atomic<LoadedAudio*> currentSource { nullptr };
    std::atomic<NamTone*> currentNamTone { nullptr };
    std::atomic<PluginTone*> currentPluginTone { nullptr };
    std::atomic<int> deviceBufferSize { 512 };
    std::atomic<float> inputGain { 1.0f };
    std::atomic<float> inputPeak { 0.0f }, guitarPeak { 0.0f };
    std::atomic<juce::uint64> callbackCount { 0 };
    std::atomic<double> deviceSampleRate { 48000.0 };
    std::atomic<bool> playing { false };
    std::atomic<double> pendingSeekSeconds { -1.0 };
    std::atomic<double> positionSeconds { 0.0 };
    std::atomic<double> speed { 1.0 };
    std::atomic<double> loopStartSeconds { 0.0 }, loopEndSeconds { 0.0 };
    std::atomic<bool> loopEnabled { false };
    std::atomic<bool> metronomeEnabled { false }, metronomeResetRequested { false }, metronomeSolo { false };
    std::atomic<double> metronomeBpm { 120.0 };
    std::atomic<double> syncOffsetSeconds { 0.0 };
    std::atomic<double> beatClockSeconds { 0.0 };   // オーディオスレッドが1ブロックごとに書く
    std::atomic<bool> beatClockRunning { false };
    std::atomic<int> clickCount { 0 };   // オーディオスレッドが増やすだけ (確認用)
    std::atomic<float> sourceGain { 1.0f }, guitarGain { 1.0f }, metronomeGain { 0.8f };
    std::atomic<int> tone { (int) ToneType::clean };
    std::atomic<bool> tunerActive { false };
    std::atomic<bool> guitarMonitoring { false };
    std::atomic<float> guitarMasterGain { 1.0f };
    std::atomic<bool> externalActive { false }, externalPlaying { false };
    std::atomic<double> externalPosition { 0.0 }, externalRate { 1.0 };
    std::atomic<juce::uint32> externalSerial { 0 };

    // チューナー用のロックフリー FIFO (オーディオスレッド → UI スレッド)
    static constexpr int tunerFifoSize = 16384;
    juce::AbstractFifo tunerFifo { tunerFifoSize };
    std::vector<float> tunerBuffer = std::vector<float> ((size_t) tunerFifoSize);

    // ギターの音を、まとめて (ブロックごとに) 処理するための場所。NAM はブロック単位で処理するため
    static constexpr int maxGuitarBlock = 16384;
    std::vector<float> guitarBuffer = std::vector<float> ((size_t) maxGuitarBlock, 0.0f);

    // オーディオスレッドだけが触る
    double readPosition = 0.0;  // 音源のサンプル位置
    double externalClockSeconds = 0.0;  // 外部の時計 (YouTube) を、届いた値の間で進めたもの
    juce::uint32 lastExternalSerial = 0;
    juce::int64 lastBeat = -1;
    double soloClockSeconds = 0.0;  // メトロノームだけを鳴らすときの時計
    int clickSamplesRemaining = 0;
    double clickPhase = 0.0, clickPhaseDelta = 0.0;
    float clickEnvelope = 0.0f, clickDecay = 0.0f;

    juce::ThreadPool loaderPool { 1 };

    JUCE_DECLARE_WEAK_REFERENCEABLE (AudioEngine)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEngine)
};
