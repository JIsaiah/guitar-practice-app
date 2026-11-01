#pragma once

#include <memory>
#include <juce_core/juce_core.h>

// NAM (Neural Amp Modeler) の音色: アンプのモデル (.nam) と、キャビネットの IR (.wav) の1組 (設計書 #6)
//
// - load() はワーカースレッドで呼ぶ (ファイルの読み込みと、モデルの準備でメモリを確保するため)
// - process() はオーディオスレッドで呼ぶ。メモリ確保もロックもしない
// - NAM のライブラリ (NeuralAmpModelerCore) の型は、NamTone.cpp の中だけで使う。
//   ほかのファイルが Eigen などの重いヘッダーを読まずに済むように (pimpl)
class NamTone
{
public:
    ~NamTone();

    struct LoadResult
    {
        std::shared_ptr<NamTone> tone;   // 失敗したら nullptr
        juce::String error;
    };

    // ampModel / cabinetIr は、どちらかが空 (juce::File()) でもよい
    // sampleRate・maxBlockSize: オーディオデバイスの設定。process() に渡す数は maxBlockSize 以下にする
    static LoadResult load (const juce::File& ampModel, const juce::File& cabinetIr,
                            double sampleRate, int maxBlockSize);

    // samples をその場で書き換える (入力 → アンプ → キャビネット)。numSamples <= getMaxBlockSize()
    void process (float* samples, int numSamples) noexcept;

    double getSampleRate() const noexcept { return sampleRate; }
    int getMaxBlockSize() const noexcept  { return maxBlockSize; }
    const juce::File& getAmpFile() const noexcept     { return ampFile; }
    const juce::File& getCabinetFile() const noexcept { return cabinetFile; }

    // モデルを作ったときのサンプルレート (Hz)。デバイスと違うと音が変わる。分からなければ 0
    double getModelSampleRate() const noexcept;

private:
    NamTone();

    struct Impl;
    std::unique_ptr<Impl> impl;

    double sampleRate = 48000.0;
    int maxBlockSize = 512;
    juce::File ampFile, cabinetFile;

    JUCE_DECLARE_NON_COPYABLE (NamTone)
};
