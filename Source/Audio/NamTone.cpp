#include "NamTone.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "../Text.h"

// NAM のライブラリは外部のコードで、JUCE の厳しい警告の設定では大量の警告が出るため、ここだけ警告を止める
#if JUCE_MSVC
 #pragma warning (push, 0)
#endif
#include <filesystem>
#include "NAM/get_dsp.h"
#if JUCE_MSVC
 #pragma warning (pop)
#endif

struct NamTone::Impl
{
    std::unique_ptr<nam::DSP> amp, cabinet;
    float ampGain = 1.0f;          // モデルの音の大きさをそろえる倍率
    float cabinetGain = 1.0f;      // IR を通しても、音の大きさが変わらないようにする倍率
    double modelSampleRate = 0.0;

    // 計算の途中の結果を入れる場所。load() で大きさを決め、process() では確保しない
    std::vector<float> ampOut;
    std::vector<std::vector<float>> cabinetOut;   // IR の出力 (ステレオの IR なら2つ)
    std::vector<float*> cabinetOutPointers;
};

NamTone::NamTone() : impl (std::make_unique<Impl>()) {}
NamTone::~NamTone() = default;

double NamTone::getModelSampleRate() const noexcept
{
    return impl->modelSampleRate;
}

namespace
{
    // Windows では、日本語を含むパスも正しく開けるよう、ワイド文字のパスにする
    std::filesystem::path toPath (const juce::File& file)
    {
        return std::filesystem::path (file.getFullPathName().toWideCharPointer());
    }

    // NAM のモデルが持つ「音の大きさ」(dB) を、NAM の公式プラグインと同じ -18 dB にそろえる
    constexpr double targetLoudnessDb = -18.0;

    // IR の .wav を、NAM が確実に読める形 (1チャンネル・32bit float の標準的な WAV) に書き直す。
    // NAM の WAV の読み込みは簡易なもので、余分な情報 (チャンク) を含むファイルを読めないことがある (お手元の IR で確認)。
    // JUCE はほとんどの WAV を読めるので、JUCE で読んでから書き直す。書き直したものは一時フォルダに取っておく
    juce::File makeReadableImpulseResponse (const juce::File& ir, juce::String& error)
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader { formats.createReaderFor (ir) };
        if (reader == nullptr || reader->lengthInSamples <= 0)
        {
            error = "IR のファイルを読み込めませんでした: "_jp + ir.getFileName();
            return {};
        }

        // 同じファイル (場所・更新日時・大きさ) なら、前に書き直したものを使う
        const auto key = ir.getFullPathName() + juce::String (ir.getLastModificationTime().toMilliseconds())
                       + juce::String (ir.getSize());
        const auto cache = juce::File::getSpecialLocation (juce::File::tempDirectory)
                               .getChildFile ("GuitarPractice").getChildFile ("ir-cache")
                               .getChildFile (juce::String::toHexString (key.hashCode64()) + ".wav");
        if (cache.existsAsFile())
            return cache;

        // キャビネットの IR は短い。長いファイルでも 1 秒までにする (処理を軽く保つため)
        const auto length = (int) juce::jmin<juce::int64> (reader->lengthInSamples, (juce::int64) reader->sampleRate);
        juce::AudioBuffer<float> buffer (1, length);
        reader->read (&buffer, 0, length, 0, true, false);   // 左 (1チャンネル目) だけ。ギターは1チャンネルのため

        cache.getParentDirectory().createDirectory();
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (cache);
        juce::WavAudioFormat wav;
        auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                       .withSampleRate (reader->sampleRate)
                                                       .withNumChannels (1)
                                                       .withBitsPerSample (32)
                                                       .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
        if (writer == nullptr || ! writer->writeFromAudioSampleBuffer (buffer, 0, length))
        {
            error = "IR のファイルを準備できませんでした: "_jp + ir.getFileName();
            return {};
        }

        writer.reset();   // ここでファイルが閉じられ、書き終わる
        return cache;
    }

    // 音量の合わせ込みに使う、ギターの代わりの音: 110 Hz (A 弦) と倍音を、0.25 秒ごとに弾き直して減衰させる
    std::vector<float> makeTestSignal (double sampleRate)
    {
        std::vector<float> signal ((size_t) (sampleRate * 0.75));
        for (size_t i = 0; i < signal.size(); ++i)
        {
            const auto t = (double) i / sampleRate;
            const auto sinceNote = std::fmod (t, 0.25);
            double s = 0.0;
            for (int harmonic = 1; harmonic <= 6; ++harmonic)
                s += std::sin (juce::MathConstants<double>::twoPi * 110.0 * harmonic * t) / harmonic;
            signal[i] = (float) (0.1 * s * std::exp (-sinceNote * 6.0));
        }
        return signal;
    }

    double rms (const std::vector<float>& x)
    {
        double sum = 0.0;
        for (auto s : x)
            sum += (double) s * s;
        return x.empty() ? 0.0 : std::sqrt (sum / (double) x.size());
    }
}

NamTone::LoadResult NamTone::load (const juce::File& ampModel, const juce::File& cabinetIr,
                                   double sampleRateToUse, int maxBlockSizeToUse)
{
    LoadResult result;

    std::shared_ptr<NamTone> tone (new NamTone());
    tone->sampleRate = sampleRateToUse;
    tone->maxBlockSize = juce::jmax (32, maxBlockSizeToUse);
    tone->ampFile = ampModel;
    tone->cabinetFile = cabinetIr;

    auto& d = *tone->impl;

    try
    {
        if (ampModel != juce::File())
        {
            d.amp = nam::get_dsp (toPath (ampModel));
            if (d.amp == nullptr)
                throw std::runtime_error ("model is empty");

            if (d.amp->NumInputChannels() != 1)
                throw std::runtime_error ("mono input models only");

            d.modelSampleRate = juce::jmax (0.0, d.amp->GetExpectedSampleRate());

            // 処理するサンプルレートと、1回に処理する最大の数を伝える。
            // ここでメモリを確保し、初期状態を落ち着かせる (prewarm) ので、オーディオスレッドでは呼ばない
            d.amp->Reset (tone->sampleRate, tone->maxBlockSize);

            if (d.amp->HasLoudness())
                d.ampGain = (float) juce::Decibels::decibelsToGain (targetLoudnessDb - d.amp->GetLoudness());

            d.ampOut.assign ((size_t) tone->maxBlockSize, 0.0f);
        }

        if (cabinetIr != juce::File())
        {
            juce::String error;
            const auto readable = makeReadableImpulseResponse (cabinetIr, error);
            if (readable == juce::File())
            {
                result.error = error;
                return result;
            }

            // NAM のライブラリは、.wav の IR も同じ仕組み (たたみ込みのモデル) として読み込める
            d.cabinet = nam::get_dsp (toPath (readable));
            if (d.cabinet == nullptr)
                throw std::runtime_error ("impulse response is empty");

            d.cabinet->Reset (tone->sampleRate, tone->maxBlockSize);   // IR のサンプルレートが違えば、ここで合わせる

            d.cabinetOut.assign ((size_t) juce::jmax (1, d.cabinet->NumOutputChannels()),
                                 std::vector<float> ((size_t) tone->maxBlockSize, 0.0f));
            for (auto& channel : d.cabinetOut)
                d.cabinetOutPointers.push_back (channel.data());
        }
    }
    catch (const std::exception& e)
    {
        result.error = "音色のファイルを読み込めませんでした: "_jp + juce::String (e.what());
        return result;
    }

    // IR を通すと、キャビネットの響きの分だけ音が大きく (または小さく) なる。
    // テスト用の音を「アンプだけ」と「アンプ + IR」に通して比べ、IR を通しても音の大きさが変わらないようにする
    if (d.cabinet != nullptr)
    {
        auto signal = makeTestSignal (tone->sampleRate);
        auto withoutCabinet = signal;

        const auto runThrough = [&] (std::vector<float>& x, bool useCabinet)
        {
            for (size_t start = 0; start < x.size(); start += (size_t) tone->maxBlockSize)
            {
                const auto n = (int) juce::jmin (x.size() - start, (size_t) tone->maxBlockSize);
                auto* p = x.data() + start;

                if (d.amp != nullptr)
                {
                    float* input[] { p };
                    float* output[] { d.ampOut.data() };
                    d.amp->process (input, output, n);
                    juce::FloatVectorOperations::multiply (p, d.ampOut.data(), d.ampGain, n);
                }

                if (useCabinet)
                {
                    float* input[] { p };
                    d.cabinet->process (input, d.cabinetOutPointers.data(), n);
                    juce::FloatVectorOperations::copy (p, d.cabinetOut[0].data(), n);
                }
            }
        };

        runThrough (withoutCabinet, false);
        if (d.amp != nullptr)
            d.amp->Reset (tone->sampleRate, tone->maxBlockSize);   // 2回目も同じ状態から始める
        runThrough (signal, true);

        const auto before = rms (withoutCabinet), after = rms (signal);
        if (before > 1.0e-6 && after > 1.0e-6)
            d.cabinetGain = (float) juce::jlimit (0.01, 10.0, before / after);

        // 合わせ込みで動かした状態を、初期状態に戻す
        if (d.amp != nullptr)
            d.amp->Reset (tone->sampleRate, tone->maxBlockSize);
        d.cabinet->Reset (tone->sampleRate, tone->maxBlockSize);
    }

    result.tone = std::move (tone);
    return result;
}

void NamTone::process (float* samples, int numSamples) noexcept
{
    auto& d = *impl;
    numSamples = juce::jmin (numSamples, maxBlockSize);

    if (d.amp != nullptr)
    {
        float* input[] { samples };
        float* output[] { d.ampOut.data() };
        d.amp->process (input, output, numSamples);
        juce::FloatVectorOperations::multiply (samples, d.ampOut.data(), d.ampGain, numSamples);
    }

    if (d.cabinet != nullptr)
    {
        float* input[] { samples };
        d.cabinet->process (input, d.cabinetOutPointers.data(), numSamples);

        // ギターは1チャンネルなので左だけ使う
        juce::FloatVectorOperations::copyWithMultiply (samples, d.cabinetOut[0].data(), d.cabinetGain, numSamples);
    }
}
