#pragma once

#include <atomic>
#include <memory>
#include <vector>
#include <juce_audio_utils/juce_audio_utils.h>
#include "../Model/TonePresets.h"

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
    void setBpm (double bpm) noexcept { metronomeBpm.store (juce::jlimit (20.0, 400.0, bpm)); }
    void setSyncOffsetSeconds (double seconds) noexcept { syncOffsetSeconds.store (seconds); }

    //==== ミキサー (3系統) と音色 ====
    void setSourceGain (float gain) noexcept    { sourceGain.store (gain); }
    void setGuitarGain (float gain) noexcept    { guitarGain.store (gain); }
    void setMetronomeGain (float gain) noexcept { metronomeGain.store (gain); }
    void setTone (ToneType type) noexcept       { tone.store ((int) type); }

    //==== チューナー ====
    void setTunerActive (bool active) noexcept { tunerActive.store (active); }
    int readTunerInput (float* destination, int maxSamples) noexcept;  // UI スレッドから呼ぶ

private:
    struct LoadedAudio;

    struct RetiredSource
    {
        std::shared_ptr<LoadedAudio> audio;
        juce::uint64 callbackCountAtRetire;
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
    void pushTunerInput (const float* data, int numSamples) noexcept;
    void triggerClick (bool accent, double sampleRate) noexcept;
    float renderClick() noexcept;

    juce::AudioDeviceManager deviceManager;
    juce::AudioFormatManager formatManager;

    // メッセージスレッドだけが触る
    std::shared_ptr<LoadedAudio> ownedSource;
    std::vector<RetiredSource> retired;
    int loadGeneration = 0;

    // スレッド間で共有する値
    std::atomic<LoadedAudio*> currentSource { nullptr };
    std::atomic<juce::uint64> callbackCount { 0 };
    std::atomic<double> deviceSampleRate { 48000.0 };
    std::atomic<bool> playing { false };
    std::atomic<double> pendingSeekSeconds { -1.0 };
    std::atomic<double> positionSeconds { 0.0 };
    std::atomic<double> speed { 1.0 };
    std::atomic<double> loopStartSeconds { 0.0 }, loopEndSeconds { 0.0 };
    std::atomic<bool> loopEnabled { false };
    std::atomic<bool> metronomeEnabled { false }, metronomeResetRequested { false };
    std::atomic<double> metronomeBpm { 120.0 };
    std::atomic<double> syncOffsetSeconds { 0.0 };
    std::atomic<float> sourceGain { 1.0f }, guitarGain { 1.0f }, metronomeGain { 0.8f };
    std::atomic<int> tone { (int) ToneType::clean };
    std::atomic<bool> tunerActive { false };

    // チューナー用のロックフリー FIFO (オーディオスレッド → UI スレッド)
    static constexpr int tunerFifoSize = 16384;
    juce::AbstractFifo tunerFifo { tunerFifoSize };
    std::vector<float> tunerBuffer = std::vector<float> ((size_t) tunerFifoSize);

    // オーディオスレッドだけが触る
    double readPosition = 0.0;  // 音源のサンプル位置
    double freeClockSeconds = 0.0;  // 音源を再生していないときのメトロノームの時計
    juce::int64 lastBeat = -1;
    int clickSamplesRemaining = 0;
    double clickPhase = 0.0, clickPhaseDelta = 0.0;
    float clickEnvelope = 0.0f, clickDecay = 0.0f;

    juce::ThreadPool loaderPool { 1 };

    JUCE_DECLARE_WEAK_REFERENCEABLE (AudioEngine)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEngine)
};
