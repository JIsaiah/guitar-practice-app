#include "AudioEngine.h"
#include "../Text.h"

struct AudioEngine::LoadedAudio
{
    juce::AudioBuffer<float> buffer;
    double sampleRate = 44100.0;
};

namespace
{
    // 簡易な音色 (ファイルなしで使える)。tanh で大きい値をなめらかに押さえ込み、アンプの歪みに似せる
    float applySimpleTone (float sample, int toneType) noexcept
    {
        switch ((ToneType) toneType)
        {
            case ToneType::crunch: return std::tanh (sample * 4.0f) * 0.6f;
            case ToneType::drive:  return std::tanh (sample * 20.0f) * 0.4f;
            case ToneType::clean:
            case ToneType::nam:    break;
        }
        return sample;
    }

    // NAM のモデルに渡す最大のブロックの大きさ。デバイスのバッファより大きければ、分けずに1回で処理できる
    int namBlockSizeFor (int deviceBufferSize)
    {
        return juce::jmax (2048, deviceBufferSize);
    }
}

AudioEngine::AudioEngine()
{
    formatManager.registerBasicFormats();
    startTimerHz (10);
}

AudioEngine::~AudioEngine()
{
    stopTimer();
    deviceManager.removeAudioCallback (this);
    deviceManager.closeAudioDevice();
    loaderPool.removeAllJobs (true, 5000);
}

//==============================================================================
void AudioEngine::initialise (const juce::XmlElement* savedDeviceState)
{
    // 入力 1 チャンネル (ギター)、出力 2 チャンネル。ASIO も選べる (JUCE_ASIO=1、設計書 #13)
    const auto error = deviceManager.initialise (1, 2, savedDeviceState, true);
    juce::ignoreUnused (error);   // 開けなくても起動は続ける (設定画面で選び直せる)
    DBG ("Audio device: " << (error.isEmpty() ? juce::String ("ok") : error));

    deviceManager.addAudioCallback (this);
}

AudioEngine::DeviceInfo AudioEngine::getDeviceInfo() const
{
    DeviceInfo info;
    auto* device = deviceManager.getCurrentAudioDevice();
    if (device == nullptr)
        return info;

    info.typeName = device->getTypeName();
    info.deviceName = device->getName();
    info.sampleRate = device->getCurrentSampleRate();
    info.bufferSize = device->getCurrentBufferSizeSamples();
    info.hasInput = device->getActiveInputChannels().countNumberOfSetBits() > 0;
    info.cpuUsage = deviceManager.getCpuUsage();

    if (info.sampleRate > 0.0)
        info.reportedLatencyMs = (device->getInputLatencyInSamples() + device->getOutputLatencyInSamples())
                                 * 1000.0 / info.sampleRate;
    return info;
}

//==============================================================================
// NAM の音色
void AudioEngine::loadNamToneAsync (const juce::File& ampModel, const juce::File& cabinetIr, LoadCallback onDone)
{
    namAmpFile = ampModel;
    namCabinetFile = cabinetIr;
    namToneError = {};

    const auto generation = ++namToneGeneration;

    if (ampModel == juce::File() && cabinetIr == juce::File())
    {
        namToneLoading = false;
        installNamTone (nullptr);
        if (onDone != nullptr)
            onDone (true, {});
        return;
    }

    namToneLoading = true;

    // 今のデバイスの設定で準備する (違うサンプルレートで準備したモデルは、オーディオスレッドで使わない)
    const auto sampleRate = deviceSampleRate.load();
    const auto blockSize = namBlockSizeFor (deviceBufferSize.load());
    juce::WeakReference<AudioEngine> weakThis (this);

    loaderPool.addJob ([weakThis, ampModel, cabinetIr, sampleRate, blockSize, generation, onDone]
    {
        auto result = NamTone::load (ampModel, cabinetIr, sampleRate, blockSize);

        juce::MessageManager::callAsync ([weakThis, result, generation, onDone]
        {
            auto* self = weakThis.get();
            if (self == nullptr || generation != self->namToneGeneration)
                return;   // 破棄済み、または後から別のファイルが選ばれた

            self->namToneLoading = false;
            self->namToneError = result.error;

            if (result.tone != nullptr)
                self->installNamTone (result.tone);

            if (onDone != nullptr)
                onDone (result.tone != nullptr, result.error);
        });
    });
}

void AudioEngine::clearNamTone()
{
    loadNamToneAsync ({}, {}, nullptr);
}

void AudioEngine::installNamTone (std::shared_ptr<NamTone> newTone)
{
    // 音源と同じく、古いものはオーディオスレッドが使い終わってから解放する
    if (ownedNamTone != nullptr)
        retired.push_back ({ ownedNamTone, callbackCount.load() });

    ownedNamTone = std::move (newTone);
    currentNamTone.store (ownedNamTone.get());
}

AudioEngine::NamToneInfo AudioEngine::getNamToneInfo() const
{
    NamToneInfo info;
    info.loading = namToneLoading;
    info.error = namToneError;
    info.ampFile = namAmpFile;
    info.cabinetFile = namCabinetFile;

    if (ownedNamTone != nullptr)
    {
        info.modelSampleRate = ownedNamTone->getModelSampleRate();
        info.active = juce::approximatelyEqual (ownedNamTone->getSampleRate(), deviceSampleRate.load());
    }
    return info;
}

std::unique_ptr<juce::XmlElement> AudioEngine::createDeviceState() const
{
    return deviceManager.createStateXml();
}

//==============================================================================
void AudioEngine::loadSourceAsync (const juce::File& file, LoadCallback onDone)
{
    const auto generation = ++loadGeneration;
    juce::WeakReference<AudioEngine> weakThis (this);

    loaderPool.addJob ([this, weakThis, file, generation, onDone]
    {
        juce::String error;
        auto loaded = readAudioFile (file, error);

        juce::MessageManager::callAsync ([weakThis, generation, loaded, error, onDone]
        {
            auto* self = weakThis.get();
            if (self == nullptr || generation != self->loadGeneration)
                return;  // 破棄済み、または後から別の読み込みが始まった

            if (loaded != nullptr)
                self->installSource (loaded);

            if (onDone != nullptr)
                onDone (loaded != nullptr, error);
        });
    });
}

void AudioEngine::clearSource()
{
    ++loadGeneration;  // 読み込み中のものがあれば無視させる
    installSource (nullptr);
}

double AudioEngine::getSourceLengthSeconds() const noexcept
{
    if (ownedSource == nullptr)
        return 0.0;

    return ownedSource->buffer.getNumSamples() / ownedSource->sampleRate;
}

std::shared_ptr<AudioEngine::LoadedAudio> AudioEngine::readAudioFile (const juce::File& file, juce::String& error)
{
    std::unique_ptr<juce::AudioFormatReader> reader { formatManager.createReaderFor (file) };
    if (reader == nullptr)
    {
        error = "この形式のファイルは読み込めません: "_jp + file.getFileName();
        return {};
    }

    constexpr double maxSeconds = 30.0 * 60.0;
    if (reader->lengthInSamples <= 0 || reader->lengthInSamples > (juce::int64) (maxSeconds * reader->sampleRate))
    {
        error = "音源の長さが対応範囲外です (30分まで)"_jp;
        return {};
    }

    auto loaded = std::make_shared<LoadedAudio>();
    loaded->sampleRate = reader->sampleRate;

    const auto numChannels = (int) juce::jlimit (1u, 2u, reader->numChannels);
    const auto numSamples = (int) reader->lengthInSamples;
    loaded->buffer.setSize (numChannels, numSamples);
    reader->read (&loaded->buffer, 0, numSamples, 0, true, numChannels > 1);

    return loaded;
}

void AudioEngine::installSource (std::shared_ptr<LoadedAudio> newSource)
{
    playing.store (false);

    if (ownedSource != nullptr)
        retired.push_back ({ ownedSource, callbackCount.load() });

    ownedSource = std::move (newSource);
    currentSource.store (ownedSource.get());
    pendingSeekSeconds.store (0.0);
    positionSeconds.store (0.0);
}

void AudioEngine::timerCallback()
{
    // コールバックは1つずつ順に呼ばれるため、差し替え後にカウンタが2進めば
    // 古いポインタを読んだコールバックは確実に終わっている
    const auto now = callbackCount.load();
    auto* device = deviceManager.getCurrentAudioDevice();
    const bool deviceRunning = device != nullptr && device->isPlaying();

    retired.erase (std::remove_if (retired.begin(), retired.end(),
                                   [&] (const RetiredSource& r)
                                   { return ! deviceRunning || now > r.callbackCountAtRetire + 1; }),
                   retired.end());

    // デバイスのサンプルレートやバッファの大きさが変わったら (設定画面で ASIO に切り替えたときなど)、
    // NAM の音色を今の設定で読み込み直す。それまでは、ギターの音をそのまま (音色なしで) 出す
    if (ownedNamTone != nullptr && ! namToneLoading && deviceRunning)
    {
        const bool rateChanged = ! juce::approximatelyEqual (ownedNamTone->getSampleRate(), deviceSampleRate.load());
        const bool blockTooSmall = ownedNamTone->getMaxBlockSize() < deviceBufferSize.load();

        if (rateChanged || blockTooSmall)
            loadNamToneAsync (namAmpFile, namCabinetFile, nullptr);
    }
}

//==============================================================================
void AudioEngine::seekSeconds (double seconds) noexcept
{
    seconds = juce::jmax (0.0, seconds);
    pendingSeekSeconds.store (seconds);
    positionSeconds.store (seconds);  // 表示をすぐ追従させる
}

void AudioEngine::setLoop (double startSeconds, double endSeconds, bool enabled) noexcept
{
    loopStartSeconds.store (startSeconds);
    loopEndSeconds.store (endSeconds);
    loopEnabled.store (enabled);
}

void AudioEngine::setMetronomeEnabled (bool enabled) noexcept
{
    if (enabled && ! metronomeEnabled.load())
        metronomeResetRequested.store (true);

    metronomeEnabled.store (enabled);
}

void AudioEngine::setMetronomeSolo (bool solo) noexcept
{
    if (solo && ! metronomeSolo.load())
        metronomeResetRequested.store (true);   // 1拍目から鳴らし始める

    metronomeSolo.store (solo);
}

int AudioEngine::readTunerInput (float* destination, int maxSamples) noexcept
{
    const auto scope = tunerFifo.read (maxSamples);

    if (scope.blockSize1 > 0)
        std::copy_n (tunerBuffer.data() + scope.startIndex1, scope.blockSize1, destination);

    if (scope.blockSize2 > 0)
        std::copy_n (tunerBuffer.data() + scope.startIndex2, scope.blockSize2, destination + scope.blockSize1);

    return scope.blockSize1 + scope.blockSize2;
}

//==============================================================================
void AudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    deviceSampleRate.store (device->getCurrentSampleRate());
    deviceBufferSize.store (device->getCurrentBufferSizeSamples());
}

// ギター: 入力の大きさ → 音色 (NAM か、簡易な音色)。結果は guitarBuffer に入る
void AudioEngine::processGuitar (const float* input, int numSamples, int toneType) noexcept
{
    auto* buffer = guitarBuffer.data();
    juce::FloatVectorOperations::copyWithMultiply (buffer, input, inputGain.load(), numSamples);

    // メーター用 (入力の大きさを合わせるときに見る)
    const auto inRange = juce::FloatVectorOperations::findMinAndMax (buffer, numSamples);
    const auto inPeak = juce::jmax (std::abs (inRange.getStart()), std::abs (inRange.getEnd()));
    if (inPeak > inputPeak.load())
        inputPeak.store (inPeak);

    if ((ToneType) toneType == ToneType::nam)
    {
        // 今のデバイスと同じサンプルレートで準備したモデルだけを使う。
        // 違うとき (読み込み直している最中) は、音色なしでそのまま出す
        auto* namTone = currentNamTone.load();
        if (namTone != nullptr && juce::approximatelyEqual (namTone->getSampleRate(), deviceSampleRate.load()))
        {
            const auto block = namTone->getMaxBlockSize();
            for (int start = 0; start < numSamples; start += block)
                namTone->process (buffer + start, juce::jmin (block, numSamples - start));
        }
    }
    else
    {
        for (int i = 0; i < numSamples; ++i)
            buffer[i] = applySimpleTone (buffer[i], toneType);
    }
}

void AudioEngine::setExternalClock (double position, double rate, bool isPlaying) noexcept
{
    externalPosition.store (position);
    externalRate.store (juce::jlimit (0.25, 2.0, rate));
    externalPlaying.store (isPlaying);
    externalActive.store (true);
    externalSerial.fetch_add (1);   // 最後に増やす: オーディオスレッドは、これが変わったら新しい値を読む
}

void AudioEngine::audioDeviceIOCallbackWithContext (const float* const* inputs, int numInputs,
                                                    float* const* outputs, int numOutputs, int numSamples,
                                                    const juce::AudioIODeviceCallbackContext&)
{
    callbackCount.fetch_add (1);

    for (int ch = 0; ch < numOutputs; ++ch)
        if (outputs[ch] != nullptr)
            juce::FloatVectorOperations::clear (outputs[ch], numSamples);

    const float* guitarIn = numInputs > 0 ? inputs[0] : nullptr;

    if (guitarIn != nullptr && tunerActive.load())
        pushTunerInput (guitarIn, numSamples);

    if (numOutputs == 0 || outputs[0] == nullptr)
        return;

    // ギターは先にブロックごと処理しておく (NAM はブロック単位で計算するため)。
    // バッファより大きなブロックは来ない想定だが、来たらはみ出した分は鳴らさない
    const int guitarSamples = guitarIn != nullptr ? juce::jmin (numSamples, maxGuitarBlock) : 0;
    if (guitarSamples > 0)
        processGuitar (guitarIn, guitarSamples, tone.load());

    float* outLeft = outputs[0];
    float* outRight = numOutputs > 1 ? outputs[1] : nullptr;

    // このブロックで使う値を読み出しておく
    const double sampleRate = deviceSampleRate.load();
    auto* source = currentSource.load();
    const auto gainGuitar = guitarGain.load();
    float guitarBlockPeak = 0.0f;
    const auto gainSource = sourceGain.load();
    const auto gainMetronome = metronomeGain.load();
    const bool metronomeOn = metronomeEnabled.load();
    const double bpm = metronomeBpm.load();
    const double offset = syncOffsetSeconds.load();

    if (metronomeResetRequested.exchange (false))
    {
        lastBeat = -1;
        soloClockSeconds = 0.0;
    }

    const bool solo = metronomeSolo.load();

    const double seek = pendingSeekSeconds.exchange (-1.0);
    if (source != nullptr && seek >= 0.0)
        readPosition = juce::jlimit (0.0, (double) (source->buffer.getNumSamples() - 1), seek * source->sampleRate);

    bool sourcePlaying = source != nullptr && playing.load();

    // 外部の時計 (YouTube)
    const bool externalRunning = externalActive.load() && externalPlaying.load();
    const double externalStep = externalRate.load() / sampleRate;

    if (const auto serial = externalSerial.load(); serial != lastExternalSerial)
    {
        lastExternalSerial = serial;
        const auto reported = externalPosition.load();
        const auto difference = reported - externalClockSeconds;

        // シークや停止のときは合わせ直し、小さなずれ (通信の遅れなど) は2割ずつ寄せる
        if (! externalRunning || std::abs (difference) > 0.15)
            externalClockSeconds = reported;
        else
            externalClockSeconds += difference * 0.2;
    }

    double step = 0.0, loopStart = 0.0, loopEnd = 0.0, lastIndex = 0.0;
    bool loopOn = false;
    const float* sourceLeft = nullptr;
    const float* sourceRight = nullptr;

    if (source != nullptr)
    {
        step = speed.load() * source->sampleRate / sampleRate;
        loopStart = loopStartSeconds.load() * source->sampleRate;
        loopEnd = loopEndSeconds.load() * source->sampleRate;
        loopOn = loopEnabled.load() && loopEnd > loopStart;
        lastIndex = (double) (source->buffer.getNumSamples() - 1);
        sourceLeft = source->buffer.getReadPointer (0);
        sourceRight = source->buffer.getReadPointer (source->buffer.getNumChannels() > 1 ? 1 : 0);
    }

    for (int i = 0; i < numSamples; ++i)
    {
        float left = 0.0f, right = 0.0f;

        // ギター (音色をかけたもの)
        if (i < guitarSamples)
        {
            const auto guitar = guitarBuffer[(size_t) i] * gainGuitar;
            guitarBlockPeak = juce::jmax (guitarBlockPeak, std::abs (guitar));
            left += guitar;
            right += guitar;
        }

        // ローカル音源 (線形補間)
        double musicTime = 0.0;

        if (sourcePlaying)
        {
            if (loopOn && readPosition >= loopEnd)
                readPosition = loopStart;

            if (readPosition >= lastIndex)
            {
                sourcePlaying = false;
                playing.store (false);
            }
            else
            {
                const auto index = (int) readPosition;
                const auto frac = (float) (readPosition - index);
                left  += (sourceLeft[index]  + frac * (sourceLeft[index + 1]  - sourceLeft[index]))  * gainSource;
                right += (sourceRight[index] + frac * (sourceRight[index + 1] - sourceRight[index])) * gainSource;

                musicTime = readPosition / source->sampleRate - offset;
                readPosition += step;
            }
        }

        // メトロノーム: 音源の再生中は音源の時計に、YouTube の再生中はその時計に合わせる (設計書 5.2)。
        // 再生していないときは鳴らさない。ボタンでオンにしただけで鳴らすと、
        // 自前の時計で鳴り始めてしまい、後から再生した音源と拍がずれるため (作者の指定)
        if (externalRunning)
        {
            musicTime = externalClockSeconds - offset;
            externalClockSeconds += externalStep;
        }

        // 音源を再生していないときに「メトロノームだけ」なら、自前の時計で鳴らす
        const bool soloRunning = solo && ! sourcePlaying && ! externalRunning;
        if (soloRunning)
        {
            musicTime = soloClockSeconds;
            soloClockSeconds += 1.0 / sampleRate;
        }

        if (metronomeOn && (sourcePlaying || externalRunning || soloRunning))
        {
            const auto beat = (juce::int64) std::floor (musicTime * bpm / 60.0);

            // 1拍進んだときだけ鳴らす (シークやループで飛んだときは鳴らさない)
            if (musicTime >= 0.0 && beat == lastBeat + 1)
                triggerClick (beat % 4 == 0, sampleRate);

            lastBeat = beat;
        }
        else
        {
            lastBeat = -1;   // 次に再生したとき、曲の頭なら1拍目から鳴らす
        }

        const auto click = renderClick() * gainMetronome;
        left += click;
        right += click;

        // 耳とスピーカーを守るため、出力は ±1 を超えないようにする (NAM のモデルによっては大きな音が出る)
        outLeft[i] = juce::jlimit (-1.0f, 1.0f, left);
        if (outRight != nullptr)
            outRight[i] = juce::jlimit (-1.0f, 1.0f, right);
    }

    if (guitarBlockPeak > guitarPeak.load())
        guitarPeak.store (guitarBlockPeak);

    if (source != nullptr)
        positionSeconds.store (readPosition / source->sampleRate);
}

void AudioEngine::pushTunerInput (const float* data, int numSamples) noexcept
{
    const auto scope = tunerFifo.write (numSamples);  // 空きが足りない分は捨てる

    if (scope.blockSize1 > 0)
        std::copy_n (data, scope.blockSize1, tunerBuffer.data() + scope.startIndex1);

    if (scope.blockSize2 > 0)
        std::copy_n (data + scope.blockSize1, scope.blockSize2, tunerBuffer.data() + scope.startIndex2);
}

void AudioEngine::triggerClick (bool accent, double sampleRate) noexcept
{
    const double frequency = accent ? 1500.0 : 1000.0;
    clickSamplesRemaining = (int) (0.03 * sampleRate);
    clickPhase = 0.0;
    clickPhaseDelta = juce::MathConstants<double>::twoPi * frequency / sampleRate;
    clickEnvelope = accent ? 1.0f : 0.7f;
    clickDecay = (float) std::exp (-1.0 / (0.008 * sampleRate));
}

float AudioEngine::renderClick() noexcept
{
    if (clickSamplesRemaining <= 0)
        return 0.0f;

    const auto value = (float) std::sin (clickPhase) * clickEnvelope;
    clickPhase += clickPhaseDelta;
    clickEnvelope *= clickDecay;
    --clickSamplesRemaining;
    return value;
}
