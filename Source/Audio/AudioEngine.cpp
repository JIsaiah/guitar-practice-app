#include "AudioEngine.h"
#include "../AppLog.h"
#include "../Text.h"

struct AudioEngine::LoadedAudio
{
    juce::AudioBuffer<float> buffer;
    double sampleRate = 44100.0;
};

namespace
{
    // 簡易な音色 (ファイルなしで使える)。tanh で大きい値をなめらかに押さえ込む。
    // 「クリーン」は、前の「歪み」の式 (作者の指示: 前のクリーン (そのまま出す) は音が小さすぎ、
    // 前の歪みが、ちょうどよい音量のクリーンの音になっていた)。「歪み」は同梱の NAM で鳴らす (processGuitar)
    float applySimpleTone (float sample, int toneType) noexcept
    {
        switch ((ToneType) toneType)
        {
            case ToneType::clean:  return std::tanh (sample * 20.0f) * 0.4f;
            case ToneType::crunch: return std::tanh (sample * 4.0f) * 0.6f;
            case ToneType::drive:
            case ToneType::nam:
            case ToneType::plugin: break;
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
    juce::addDefaultFormatsToManager (pluginFormats);   // VST3 (JUCE_PLUGINHOST_VST3=1)
    startTimerHz (10);
}

AudioEngine::~AudioEngine()
{
    stopTimer();
    deviceManager.removeAudioCallback (this);
    deviceManager.closeAudioDevice();
    loaderPool.removeAllJobs (true, 5000);

    // オーディオが止まったあとで、プラグインを確実にここ (メッセージスレッド) で消す
    currentPluginTone.store (nullptr);
    ownedPluginTone.reset();
    retired.clear();
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
    loadNamSlotAsync (userNam, ampModel, cabinetIr, std::move (onDone));
}

void AudioEngine::loadDriveToneAsync (const juce::File& ampModel, const juce::File& cabinetIr, LoadCallback onDone)
{
    // 同じファイルを読み込み済み・読み込み中なら、そのまま使う (練習画面を開くたびに読み込み直さない)
    if (driveNam.ampFile == ampModel && driveNam.cabinetFile == cabinetIr && (driveNam.owned != nullptr || driveNam.loading))
    {
        if (onDone != nullptr)
            onDone (true, {});
        return;
    }

    loadNamSlotAsync (driveNam, ampModel, cabinetIr, std::move (onDone));
}

void AudioEngine::loadNamSlotAsync (NamSlot& slot, const juce::File& ampModel, const juce::File& cabinetIr, LoadCallback onDone)
{
    slot.ampFile = ampModel;
    slot.cabinetFile = cabinetIr;
    slot.error = {};

    const auto generation = ++slot.generation;

    if (ampModel == juce::File() && cabinetIr == juce::File())
    {
        slot.loading = false;
        installNamTone (slot, nullptr);
        if (onDone != nullptr)
            onDone (true, {});
        return;
    }

    slot.loading = true;

    // 今のデバイスの設定で準備する (違うサンプルレートで準備したモデルは、オーディオスレッドで使わない)
    const auto sampleRate = deviceSampleRate.load();
    const auto blockSize = namBlockSizeFor (deviceBufferSize.load());
    juce::WeakReference<AudioEngine> weakThis (this);
    auto* slotPtr = &slot;   // AudioEngine のメンバー。使う前に weakThis で、エンジンがまだあるか確かめる

    loaderPool.addJob ([weakThis, slotPtr, ampModel, cabinetIr, sampleRate, blockSize, generation, onDone]
    {
        auto result = NamTone::load (ampModel, cabinetIr, sampleRate, blockSize);

        juce::MessageManager::callAsync ([weakThis, slotPtr, result, generation, onDone]
        {
            auto* self = weakThis.get();
            if (self == nullptr || generation != slotPtr->generation)
                return;   // 破棄済み、または後から別のファイルが選ばれた

            slotPtr->loading = false;
            slotPtr->error = result.error;

            if (result.tone != nullptr)
                self->installNamTone (*slotPtr, result.tone);

            if (onDone != nullptr)
                onDone (result.tone != nullptr, result.error);
        });
    });
}

void AudioEngine::clearNamTone()
{
    loadNamToneAsync ({}, {}, nullptr);
}

//==============================================================================
// VST3 プラグインの音色
void AudioEngine::loadPluginToneAsync (const juce::File& vst3File, const juce::MemoryBlock& state, LoadCallback onDone)
{
    const auto generation = ++pluginToneGeneration;
    pendingPluginLoad.reset();   // 前に頼まれて待っていたものは、やめる

    if (vst3File == juce::File())
    {
        pluginToneLoading = false;
        installPluginTone (nullptr);
        if (onDone != nullptr)
            onDone (true, {});
        return;
    }

    // 同じプラグインが読み込まれていて、戻す状態があるなら、作り直さずに状態だけ戻す
    if (ownedPluginTone != nullptr && ownedPluginTone->getFile() == vst3File && state.getSize() > 0)
    {
        pluginToneLoading = false;
        ownedPluginTone->getInstance().setStateInformation (state.getData(), (int) state.getSize());
        if (onPluginToneChanged != nullptr)
            onPluginToneChanged();
        if (onDone != nullptr)
            onDone (true, {});
        return;
    }

    // 同時に1つだけ: 今のプラグインを外し (編集画面も消してもらう)、消し終えてから新しいものを作る
    pluginToneLoading = true;
    installPluginTone (nullptr);
    pendingPluginLoad = PendingPluginLoad { vst3File, state, std::move (onDone), generation };
    startPendingPluginLoad();
}

bool AudioEngine::hasRetiredPlugin() const
{
    return std::any_of (retired.begin(), retired.end(), [] (const RetiredSource& r) { return r.isPlugin; });
}

void AudioEngine::startPendingPluginLoad()
{
    if (! pendingPluginLoad.has_value() || hasRetiredPlugin())
        return;   // まだ古いプラグインが残っている (timerCallback で、また呼ぶ)

    auto load = std::move (*pendingPluginLoad);
    pendingPluginLoad.reset();

    const auto generation = load.generation;
    auto onDone = std::move (load.onDone);
    juce::WeakReference<AudioEngine> weakThis (this);
    AppLog::write ("プラグインを読み込む: "_jp + load.file.getFileName());

    PluginTone::createAsync (pluginFormats, load.file, load.state,
                             deviceSampleRate.load(), deviceBufferSize.load(),
                             [weakThis, generation, onDone] (std::shared_ptr<PluginTone> tone, const juce::String& error)
    {
        auto* self = weakThis.get();
        if (self == nullptr || generation != self->pluginToneGeneration)
            return;   // 破棄済み、または後から別のプラグインが選ばれた (できたものはここで消える: メッセージスレッド)

        AppLog::write (tone != nullptr ? "プラグインを読み込んだ"_jp : "プラグインを読み込めなかった: "_jp + error);
        self->pluginToneLoading = false;

        if (tone != nullptr)
            self->installPluginTone (tone);

        if (onDone != nullptr)
            onDone (tone != nullptr, error);
    });
}

void AudioEngine::clearPluginTone()
{
    loadPluginToneAsync ({}, {}, nullptr);
}

void AudioEngine::installPluginTone (std::shared_ptr<PluginTone> newTone)
{
    // 今のプラグインの編集画面を、画面に消してもらう (プラグインより先に消す必要がある)
    if (ownedPluginTone != nullptr && onPluginToneAboutToReplace != nullptr)
        onPluginToneAboutToReplace();

    // 古いものは、オーディオスレッドが使い終わってから解放する (音源・NAM と同じ)
    // プラグインは、編集画面を消してから少し (1秒) 待って消す。編集画面を消したあとに、
    // プラグイン自身の後片付けのメッセージが届くことがあるため
    if (ownedPluginTone != nullptr)
        retired.push_back ({ ownedPluginTone, callbackCount.load(), juce::Time::getMillisecondCounter() + 1000, true });

    ownedPluginTone = std::move (newTone);
    pluginDetachedAtCallback = -1;
    currentPluginTone.store (ownedPluginTone.get());

    if (onPluginToneChanged != nullptr)
        onPluginToneChanged();
}

void AudioEngine::updatePluginPreparation()
{
    if (ownedPluginTone == nullptr)
        return;

    auto* device = deviceManager.getCurrentAudioDevice();
    const bool deviceRunning = device != nullptr && device->isPlaying();
    const auto sampleRate = deviceSampleRate.load();
    const auto blockSize = deviceBufferSize.load();

    if (pluginDetachedAtCallback < 0)
    {
        // デバイスのサンプルレートやバッファが変わったら、準備し直す。
        // プラグインの準備 (prepareToPlay) は、オーディオスレッドが使っている間にはできないので、まず外す
        const bool changed = ! juce::approximatelyEqual (ownedPluginTone->getSampleRate(), sampleRate)
                          || ownedPluginTone->getBlockSize() < blockSize;
        if (! changed)
            return;

        currentPluginTone.store (nullptr);
        pluginDetachedAtCallback = (juce::int64) callbackCount.load();
        if (deviceRunning)
            return;   // 次のタイマーで、オーディオスレッドが使い終わったのを確かめてから準備する
    }

    // 外してからコールバックが2回進めば (またはデバイスが止まっていれば)、もう使われていない
    if (deviceRunning && (juce::int64) callbackCount.load() <= pluginDetachedAtCallback + 1)
        return;

    ownedPluginTone->prepare (sampleRate, blockSize);
    pluginDetachedAtCallback = -1;
    currentPluginTone.store (ownedPluginTone.get());
}

void AudioEngine::installNamTone (NamSlot& slot, std::shared_ptr<NamTone> newTone)
{
    // 音源と同じく、古いものはオーディオスレッドが使い終わってから解放する
    if (slot.owned != nullptr)
        retired.push_back ({ slot.owned, callbackCount.load() });

    slot.owned = std::move (newTone);
    slot.current.store (slot.owned.get());
}

AudioEngine::NamToneInfo AudioEngine::getNamToneInfo() const   { return getNamSlotInfo (userNam); }
AudioEngine::NamToneInfo AudioEngine::getDriveToneInfo() const { return getNamSlotInfo (driveNam); }

AudioEngine::NamToneInfo AudioEngine::getNamSlotInfo (const NamSlot& slot) const
{
    NamToneInfo info;
    info.loading = slot.loading;
    info.error = slot.error;
    info.ampFile = slot.ampFile;
    info.cabinetFile = slot.cabinetFile;

    if (slot.owned != nullptr)
    {
        info.modelSampleRate = slot.owned->getModelSampleRate();
        info.active = juce::approximatelyEqual (slot.owned->getSampleRate(), deviceSampleRate.load());
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

    const auto nowMs = juce::Time::getMillisecondCounter();
    retired.erase (std::remove_if (retired.begin(), retired.end(),
                                   [&] (const RetiredSource& r)
                                   {
                                       return (! deviceRunning || now > r.callbackCountAtRetire + 1)
                                              && (r.keepUntilMs == 0 || (int) (nowMs - r.keepUntilMs) >= 0);
                                   }),
                   retired.end());

    startPendingPluginLoad();   // 古いプラグインが消えたら、待っていた新しいプラグインを作る

    // デバイスのサンプルレートやバッファの大きさが変わったら (設定画面で ASIO に切り替えたときなど)、
    // NAM の音色を今の設定で読み込み直す。それまでは、ギターの音をそのまま (音色なしで) 出す
    if (deviceRunning)
    {
        reloadNamSlotIfDeviceChanged (userNam);
        reloadNamSlotIfDeviceChanged (driveNam);
    }

    updatePluginPreparation();
}

void AudioEngine::reloadNamSlotIfDeviceChanged (NamSlot& slot)
{
    if (slot.owned == nullptr || slot.loading)
        return;

    const bool rateChanged = ! juce::approximatelyEqual (slot.owned->getSampleRate(), deviceSampleRate.load());
    const bool blockTooSmall = slot.owned->getMaxBlockSize() < deviceBufferSize.load();

    if (rateChanged || blockTooSmall)
        loadNamSlotAsync (slot, slot.ampFile, slot.cabinetFile, nullptr);
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

    // ノイズゲート: 弾いていないときの雑音を、音色 (アンプ) で大きくなる前に消す。
    // VST プラグインのときは、いつもオフ (作者の指示: プラグインの中のゲートを使う。二重にかけると音の消え際が切れる)。
    // オフで呼ぶと、ゲートの状態が「開いている」に戻るので、ほかの音色に戻したときに音が急に消えない
    const auto gateDb = (ToneType) toneType == ToneType::plugin ? NoiseGate::offDb : noiseGateDb.load();
    noiseGate.process (buffer, numSamples, gateDb, deviceSampleRate.load());

    if ((ToneType) toneType == ToneType::plugin)
    {
        // VST3 プラグイン。準備し直している最中 (外している間) は、音色なしでそのまま出す
        auto* pluginTone = currentPluginTone.load();
        if (pluginTone != nullptr && juce::approximatelyEqual (pluginTone->getSampleRate(), deviceSampleRate.load()))
        {
            const auto block = pluginTone->getBlockSize();
            for (int start = 0; start < numSamples; start += block)
                pluginTone->process (buffer + start, juce::jmin (block, numSamples - start));
        }
    }
    else if ((ToneType) toneType == ToneType::nam)
    {
        processNam (userNam, buffer, numSamples);
    }
    else if ((ToneType) toneType == ToneType::drive)
    {
        // 「歪み」: 同梱の NAM のモデル。まだ読み込めていないときは、クリーンの音で出す (音が消えないように)
        if (! processNam (driveNam, buffer, numSamples))
            for (int i = 0; i < numSamples; ++i)
                buffer[i] = applySimpleTone (buffer[i], (int) ToneType::clean);
    }
    else
    {
        for (int i = 0; i < numSamples; ++i)
            buffer[i] = applySimpleTone (buffer[i], toneType);
    }
}

bool AudioEngine::processNam (const NamSlot& slot, float* buffer, int numSamples) noexcept
{
    // 今のデバイスと同じサンプルレートで準備したモデルだけを使う。
    // 違うとき (読み込み直している最中) は、何もしない (false を返す)
    auto* namTone = slot.current.load();
    if (namTone == nullptr || ! juce::approximatelyEqual (namTone->getSampleRate(), deviceSampleRate.load()))
        return false;

    const auto block = namTone->getMaxBlockSize();
    for (int start = 0; start < numSamples; start += block)
        namTone->process (buffer + start, juce::jmin (block, numSamples - start));
    return true;
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
    // ギターの音は、練習画面を開いているときだけ出す (一覧などでは鳴らさない。音色の処理もしない)
    const int guitarSamples = guitarIn != nullptr && guitarMonitoring.load() ? juce::jmin (numSamples, maxGuitarBlock) : 0;
    if (guitarSamples > 0)
        processGuitar (guitarIn, guitarSamples, tone.load());

    float* outLeft = outputs[0];
    float* outRight = numOutputs > 1 ? outputs[1] : nullptr;

    // このブロックで使う値を読み出しておく
    const double sampleRate = deviceSampleRate.load();
    auto* source = currentSource.load();
    const auto gainGuitar = guitarGain.load() * guitarMasterGain.load();
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

    double blockBeatClock = 0.0;   // ブロックの最後のサンプルでの、メトロノームの時計 (ビジュアライザーに渡す)
    bool blockClockRunning = false;

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

        blockClockRunning = sourcePlaying || externalRunning || soloRunning;
        if (blockClockRunning)
            blockBeatClock = musicTime;

        // 「曲に合わせる」(metronomeOn) は再生中だけ。「単体」は「曲に合わせる」と独立して鳴る (作者の指示)
        if ((metronomeOn && (sourcePlaying || externalRunning)) || soloRunning)
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

    beatClockSeconds.store (blockBeatClock);
    beatClockRunning.store (blockClockRunning);

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
    clickCount.fetch_add (1, std::memory_order_relaxed);
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
