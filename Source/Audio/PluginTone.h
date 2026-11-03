#pragma once

#include <memory>
#include <juce_audio_processors/juce_audio_processors.h>

// VST3 プラグインの音色 (TONE3000 プラグインなど。設計書 #6 の「VST3」)
//
// - 作る (createAsync) のはメッセージスレッド。VST3 はメッセージスレッドで作る決まりのため、
//   非同期の生成 (createPluginInstanceAsync) を使い、メッセージスレッドを長く止めない (setlist-mode.md 6章)
// - prepare() はメッセージスレッドで、オーディオスレッドがこのプラグインを使っていないときに呼ぶ
// - process() はオーディオスレッドで呼ぶ。メモリ確保もロックもしない
// - 消す (デストラクタ) のはメッセージスレッド。編集画面 (エディタ) を先に消しておくこと
class PluginTone
{
public:
    ~PluginTone();

    using CreateCallback = std::function<void (std::shared_ptr<PluginTone> tone, const juce::String& error)>;

    // vst3File: .vst3 (フォルダかファイル)。state: 前に保存した状態 (空なら初期状態)
    static void createAsync (juce::AudioPluginFormatManager& formats, const juce::File& vst3File,
                             const juce::MemoryBlock& state, double sampleRate, int blockSize,
                             CreateCallback onDone);

    // オーディオデバイスのサンプルレートやバッファが変わったときに、準備し直す
    void prepare (double sampleRate, int blockSize);

    // samples をその場で書き換える (ギター → プラグイン)。numSamples <= getBlockSize()
    void process (float* samples, int numSamples) noexcept;

    juce::AudioPluginInstance& getInstance() noexcept { return *instance; }
    juce::MemoryBlock getState() const;
    juce::String getName() const;
    const juce::File& getFile() const noexcept { return file; }
    double getSampleRate() const noexcept { return sampleRate; }
    int getBlockSize() const noexcept { return blockSize; }

private:
    PluginTone() = default;

    std::unique_ptr<juce::AudioPluginInstance> instance;
    juce::File file;
    double sampleRate = 48000.0;
    int blockSize = 512;

    // process() で使う場所。prepare() で大きさを決める
    juce::AudioBuffer<float> buffer;
    juce::MidiBuffer midi;

    JUCE_DECLARE_NON_COPYABLE (PluginTone)
};
