#include "PluginTone.h"
#include "../Text.h"

PluginTone::~PluginTone()
{
    // 編集画面が残っていたら、プラグインより先に消えている必要がある (呼ぶ側の約束)
    jassert (instance == nullptr || instance->getActiveEditor() == nullptr);

    if (instance != nullptr)
        instance->releaseResources();
}

void PluginTone::createAsync (juce::AudioPluginFormatManager& formats, const juce::File& vst3File,
                              const juce::MemoryBlock& state, double sampleRate, int blockSize,
                              CreateCallback onDone)
{
    // .vst3 の中にあるプラグインの種類を調べる (1つのファイルに複数入っていることもあるので、最初のもの)
    juce::OwnedArray<juce::PluginDescription> types;
    for (auto* format : formats.getFormats())
        if (format->fileMightContainThisPluginType (vst3File.getFullPathName()))
            format->findAllTypesForFile (types, vst3File.getFullPathName());

    if (types.isEmpty())
    {
        onDone (nullptr, "プラグインを読み込めませんでした: "_jp + vst3File.getFileName());
        return;
    }

    const auto description = *types[0];

    formats.createPluginInstanceAsync (description, sampleRate, blockSize,
        [vst3File, state, sampleRate, blockSize, onDone] (std::unique_ptr<juce::AudioPluginInstance> instance,
                                                          const juce::String& error)
    {
        if (instance == nullptr)
        {
            onDone (nullptr, "プラグインを開けませんでした: "_jp + error);
            return;
        }

        std::shared_ptr<PluginTone> tone (new PluginTone());
        tone->instance = std::move (instance);
        tone->file = vst3File;

        // 前に保存した状態 (選んだ音色など) を戻す
        if (state.getSize() > 0)
            tone->instance->setStateInformation (state.getData(), (int) state.getSize());

        tone->prepare (sampleRate, blockSize);
        onDone (tone, {});
    });
}

void PluginTone::prepare (double newSampleRate, int newBlockSize)
{
    sampleRate = newSampleRate;
    blockSize = juce::jmax (32, newBlockSize);

    instance->releaseResources();
    instance->setRateAndBufferSizeDetails (sampleRate, blockSize);
    instance->prepareToPlay (sampleRate, blockSize);

    // 入力・出力のうち多いほうのチャンネル数で、処理に使う場所を用意する (最低2: ステレオのプラグインも多いため)
    const auto channels = juce::jmax (2, instance->getTotalNumInputChannels(), instance->getTotalNumOutputChannels());
    buffer.setSize (channels, blockSize, false, true, false);
    midi.ensureSize (256);
}

void PluginTone::process (float* samples, int numSamples) noexcept
{
    numSamples = juce::jmin (numSamples, blockSize);

    // 用意してある場所を、このブロックの長さで使う (チャンネルの一覧を指すだけで、メモリは確保しない)
    juce::AudioBuffer<float> block (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), numSamples);

    // ギターは1チャンネル。プラグインの入力が2チャンネルなら、両方に同じ音を入れる
    const auto inputs = juce::jmax (1, instance->getTotalNumInputChannels());
    for (int ch = 0; ch < block.getNumChannels(); ++ch)
    {
        if (ch < inputs)
            block.copyFrom (ch, 0, samples, numSamples);
        else
            block.clear (ch, 0, numSamples);
    }

    midi.clear();
    instance->processBlock (block, midi);

    // 出力は左 (1チャンネル目) を使う。ギターの音はこのあと、ミキサーで左右に同じように出す
    juce::FloatVectorOperations::copy (samples, block.getReadPointer (0), numSamples);
}

juce::MemoryBlock PluginTone::getState() const
{
    juce::MemoryBlock state;
    instance->getStateInformation (state);
    return state;
}

juce::String PluginTone::getName() const
{
    return instance->getName();
}
